/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
#include <map>
#include <vector>
#include <iostream>
#include <string>
#include <xercesc/sax2/XMLReaderFactory.hpp>
#include <xercesc/sax2/Attributes.hpp>
#endif

#include <locale>

#include "Reader.h"
#include "Base64.h"
#include "Base64Filter.h"
#include "Console.h"
#include "Exception.h"
#include "InputSource.h"
#include "Persistence.h"
#include "Sequencer.h"
#include "Stream.h"
#include "XMLTools.h"

#ifdef _MSC_VER
#include <zipios++/zipios-config.h>
#endif
#include <zipios++/zipinputstream.h>
#include <boost/iostreams/filtering_stream.hpp>

#ifndef XERCES_CPP_NAMESPACE_BEGIN
#define XERCES_CPP_NAMESPACE_QUALIFIER
using namespace XERCES_CPP_NAMESPACE;
#else
XERCES_CPP_NAMESPACE_USE
#endif

using namespace std;


// ---------------------------------------------------------------------------
//  Base::XMLReader: Constructors and Destructor
// ---------------------------------------------------------------------------

Base::XMLReader::XMLReader(const char* FileName, std::istream& str)
    : _File(FileName)
{
#ifdef _MSC_VER
    str.imbue(std::locale::empty());
#else
    str.imbue(std::locale::classic());
#endif

    // create the parser
    parser = XMLReaderFactory::createXMLReader();  // NOLINT

    parser->setContentHandler(this);
    parser->setLexicalHandler(this);
    parser->setErrorHandler(this);

    try {
        StdInputSource file(str, _File.filePath().c_str());
        _valid = parser->parseFirst(file, token);
    }
    catch (const XMLException& toCatch) {
        char* message = XMLString::transcode(toCatch.getMessage());
        cerr << "Exception message is: \n" << message << "\n";
        XMLString::release(&message);
    }
    catch (const SAXParseException& toCatch) {
        char* message = XMLString::transcode(toCatch.getMessage());
        cerr << "Exception message is: \n" << message << "\n";
        XMLString::release(&message);
    }
#ifndef FC_DEBUG
    catch (...) {
        cerr << "Unexpected Exception \n";
    }
#endif
}

Base::XMLReader::~XMLReader()
{
    //  Delete the parser itself.  Must be done prior to calling Terminate, below.
    delete parser;
}

const char* Base::XMLReader::localName() const
{
    return LocalName.c_str();
}

unsigned int Base::XMLReader::getAttributeCount() const
{
    return static_cast<unsigned int>(AttrMap.size());
}

namespace
{
template<typename T>
T readerCast(const char* value)
{
    if constexpr (std::is_same_v<T, const char*>) {
        return value;
    }
    if constexpr (std::is_same_v<T, long>) {
        return stol(value);
    }
    if constexpr (std::is_same_v<T, int>) {
        return stoi(value);
    }
    if constexpr (std::is_same_v<T, unsigned long>) {
        return stoul(value, nullptr);
    }
    if constexpr (std::is_same_v<T, double>) {
        return stod(value, nullptr);
    }
    if constexpr (std::is_same_v<T, bool>) {
        return std::string_view(value) != "0";
    }
}
}  // anonymous namespace

template<typename T>
    requires Base::XMLReader::instantiated<T>
T Base::XMLReader::getAttribute(const char* AttrName, T defaultValue) const
{
    auto pos = AttrMap.find(AttrName);
    if (pos == AttrMap.end()) {
        return defaultValue;
    }
    const char* rawValue = pos->second.c_str();
    return readerCast<T>(rawValue);
}

template<typename T>
    requires Base::XMLReader::instantiated<T>
T Base::XMLReader::getAttribute(const char* AttrName) const
{
    auto pos = AttrMap.find(AttrName);
    if (pos == AttrMap.end()) {
        // wrong name, use hasAttribute if not sure!
        std::string msg = std::string("XML Attribute: \"") + AttrName + "\" not found";
        throw Base::XMLAttributeError(msg);
    }
    const char* rawValue = pos->second.c_str();
    return readerCast<T>(rawValue);
}

// Explicit template instantiation
template BaseExport bool Base::XMLReader::getAttribute<bool>(const char* AttrName,
                                                             bool defaultValue) const;
template BaseExport bool Base::XMLReader::getAttribute<bool>(const char* AttrName) const;
template BaseExport const char*
Base::XMLReader::getAttribute<const char*>(const char* AttrName, const char* defaultValue) const;
template BaseExport const char*
Base::XMLReader::getAttribute<const char*>(const char* AttrName) const;
template BaseExport double Base::XMLReader::getAttribute<double>(const char* AttrName,
                                                                 double defaultValue) const;
template BaseExport double Base::XMLReader::getAttribute<double>(const char* AttrName) const;
template BaseExport int Base::XMLReader::getAttribute<int>(const char* AttrName,
                                                           int defaultValue) const;
template BaseExport int Base::XMLReader::getAttribute<int>(const char* AttrName) const;
template BaseExport long Base::XMLReader::getAttribute<long>(const char* AttrName,
                                                             long defaultValue) const;
template BaseExport long Base::XMLReader::getAttribute<long>(const char* AttrName) const;
template BaseExport unsigned long
Base::XMLReader::getAttribute<unsigned long>(const char* AttrName,
                                             unsigned long defaultValue) const;
template BaseExport unsigned long
Base::XMLReader::getAttribute<unsigned long>(const char* AttrName) const;

bool Base::XMLReader::hasAttribute(const char* AttrName) const
{
    return AttrMap.find(AttrName) != AttrMap.end();
}

bool Base::XMLReader::read()
{
    ReadType = None;

    try {
        parser->parseNext(token);
    }
    catch (const XMLException& toCatch) {

        char* message = XMLString::transcode(toCatch.getMessage());
        std::string what = message;
        XMLString::release(&message);
        throw Base::XMLBaseException(what);
    }
    catch (const SAXParseException& toCatch) {

        char* message = XMLString::transcode(toCatch.getMessage());
        std::string what = message;
        XMLString::release(&message);
        throw Base::XMLParseException(what);
    }
    catch (...) {
        throw Base::XMLBaseException("Unexpected XML exception");
    }
    return true;
}

void Base::XMLReader::readElement(const char* ElementName)
{
    bool ok {};

    endCharStream();
    int currentLevel = Level;
    std::string currentName = LocalName;
    do {
        ok = read();
        if (!ok) {
            break;
        }
        if (ReadType == EndElement && currentName == LocalName && currentLevel >= Level) {
            // we have reached the end of the element when calling this method
            // thus we must stop reading on.
            break;
        }
        if (ReadType == EndDocument) {
            // the end of the document has been reached but we still try to continue on reading
            throw Base::XMLParseException("End of document reached");
        }
    } while ((ReadType != StartElement && ReadType != StartEndElement)
             || (ElementName && LocalName != ElementName));
}

bool Base::XMLReader::readNextElement()
{
    bool ok {};
    while (true) {
        ok = read();
        if (!ok) {
            break;
        }
        if (ReadType == StartElement) {
            break;
        }
        if (ReadType == StartEndElement) {
            break;
        }
        if (ReadType == EndElement) {
            break;
        }
        if (ReadType == EndDocument) {
            break;
        }
    };

    return (ReadType == StartElement || ReadType == StartEndElement);
}

int Base::XMLReader::level() const
{
    return Level;
}

bool Base::XMLReader::isEndOfElement() const
{
    return (ReadType == EndElement);
}

bool Base::XMLReader::isStartOfDocument() const
{
    return (ReadType == StartDocument);
}

bool Base::XMLReader::isEndOfDocument() const
{
    return (ReadType == EndDocument);
}

void Base::XMLReader::readEndElement(const char* ElementName, int level)
{
    endCharStream();

    // if we are already at the end of the current element
    if ((ReadType == EndElement || ReadType == StartEndElement) && ElementName
        && LocalName == ElementName && (level < 0 || level == Level)) {
        return;
    }
    if (ReadType == EndDocument) {
        // the end of the document has been reached but we still try to continue on reading
        throw Base::XMLParseException("End of document reached");
    }

    bool ok {};
    do {
        ok = read();
        if (!ok) {
            break;
        }
        if (ReadType == EndDocument) {
            break;
        }
    } while (ReadType != EndElement
             || (ElementName && (LocalName != ElementName || (level >= 0 && level != Level))));
}

void Base::XMLReader::readCharacters(const char* filename, CharStreamFormat format)
{
    Base::FileInfo fi(filename);
    Base::ofstream to(fi, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!to) {
        throw Base::FileException("XMLReader::readCharacters() Could not open file!");
    }

    beginCharStream(format) >> to.rdbuf();
    to.close();
    endCharStream();
}

std::streamsize Base::XMLReader::read(char_type* s, std::streamsize n)
{

    char_type* buf = s;
    if (CharacterOffset < 0) {
        return -1;
    }

    for (;;) {
        std::streamsize copy_size =
            static_cast<std::streamsize>(Characters.size()) - CharacterOffset;
        if (n < copy_size) {
            copy_size = n;
        }
        std::memcpy(s, Characters.c_str() + CharacterOffset, copy_size);
        n -= copy_size;
        s += copy_size;
        CharacterOffset += copy_size;

        if (!n) {
            break;
        }

        if (ReadType == Chars) {
            read();
        }
        else {
            CharacterOffset = -1;
            break;
        }
    }

    return s - buf;
}

void Base::XMLReader::endCharStream()
{
    CharacterOffset = -1;
    CharStream.reset();
}

std::istream& Base::XMLReader::charStream()
{
    if (!CharStream) {
        throw Base::XMLParseException("no current character stream");
    }
    return *CharStream;
}

std::istream& Base::XMLReader::beginCharStream(CharStreamFormat format)
{
    if (CharStream) {
        throw Base::XMLParseException("recursive character stream");
    }

    // TODO: An XML element can actually contain a mix of child elements and
    // characters. So we should not actually demand 'StartElement' here. But
    // with the current implementation of character stream, we cannot track
    // child elements and character content at the same time.
    if (ReadType == StartElement) {
        CharacterOffset = 0;
        read();
    }
    else if (ReadType == StartEndElement) {
        // If we are currently at a self-closing element, just leave the offset
        // as negative and do not read any characters. This will result in an
        // empty input stream for the caller.
        CharacterOffset = -1;
    }
    else {
        throw Base::XMLParseException("invalid state while reading character stream");
    }

    CharStream = std::make_unique<boost::iostreams::filtering_istream>();
    auto* filteringStream = dynamic_cast<boost::iostreams::filtering_istream*>(CharStream.get());
    if (format == CharStreamFormat::Base64Encoded) {
        filteringStream->push(
            base64_decoder(Base::base64DefaultBufferSize, Base64ErrorHandling::silent));
    }
    filteringStream->push(boost::ref(*this));
    return *CharStream;
}

void Base::XMLReader::readBinFile(const char* filename)
{
    Base::FileInfo fi(filename);
    Base::ofstream to(fi, std::ios::out | std::ios::binary);
    if (!to) {
        throw Base::FileException("XMLReader::readBinFile() Could not open file!");
    }

    bool ok {};
    do {
        ok = read();
        if (!ok) {
            break;
        }
    } while (ReadType != EndCDATA);

    to << Base::base64_decode(Characters);
    to.close();
}

void Base::XMLReader::readFiles(zipios::ZipInputStream& zipstream) const
{
    // It's possible that not all objects inside the document could be created, e.g. if a module
    // is missing that would know these object types. So, there may be data files inside the zip
    // file that cannot be read. We simply ignore these files.
    // On the other hand, however, it could happen that a file should be read that is not part of
    // the zip file. This happens e.g. if a document is written without GUI up but is read with GUI
    // up. In this case the associated GUI document asks for its file which is not part of the ZIP
    // file, then.
    // In either case it's guaranteed that the order of the files is kept.
    zipios::ConstEntryPointer entry;
    try {
        entry = zipstream.getNextEntry();
    }
    catch (const std::exception&) {
        // There is no further file at all. This can happen if the
        // project file was created without GUI
        return;
    }
    std::vector<FileEntry>::const_iterator it = FileList.begin();
    Base::SequencerLauncher seq("Importing project files...", FileList.size());
    while (entry->isValid() && it != FileList.end()) {
        std::vector<FileEntry>::const_iterator jt = it;
        // Check if the current entry is registered, otherwise check the next registered files as
        // soon as both file names match
        while (jt != FileList.end() && entry->getName() != jt->FileName) {
            ++jt;
        }
        // If this condition is true both file names match and we can read-in the data, otherwise
        // no file name for the current entry in the zip was registered.
        if (jt != FileList.end()) {
            try {
                Base::Reader reader(zipstream, jt->FileName, FileVersion);
                jt->Object->RestoreDocFile(reader);
                if (reader.getLocalReader()) {
                    reader.getLocalReader()->readFiles(zipstream);
                }
            }
            catch (...) {
                // For any exception we just continue with the next file.
                // It doesn't matter if the last reader has read more or
                // less data than the file size would allow.
                // All what we need to do is to notify the user about the
                // failure.
                Base::Console().error("Reading failed from embedded file: %s\n",
                                      entry->toString().c_str());
                FailedFiles.push_back(jt->FileName);
            }
            // Go to the next registered file name
            it = jt + 1;
        }

        seq.next();

        // In either case we must go to the next entry
        try {
            entry = zipstream.getNextEntry();
        }
        catch (const std::exception&) {
            // there is no further entry
            break;
        }
    }
}

const char* Base::XMLReader::addFile(const char* Name, Base::Persistence* Object)
{
    FileEntry temp;
    temp.FileName = Name;
    temp.Object = Object;

    FileList.push_back(temp);

    return Name;
}

bool Base::XMLReader::hasFilenames() const
{
    return !FileList.empty();
}

bool Base::XMLReader::hasReadFailed(const std::string& filename) const
{
    return std::ranges::find(FailedFiles, filename) != FailedFiles.end();
}

bool Base::XMLReader::isRegistered(Base::Persistence* Object) const
{
    if (Object) {
        for (const auto& it : FileList) {
            if (it.Object == Object) {
                return true;
            }
        }
    }

    return false;
}

void Base::XMLReader::addName(const char* /*unused*/, const char* /*unused*/)
{}

const char* Base::XMLReader::getName(const char* name) const
{
    return name;
}

bool Base::XMLReader::doNameMapping() const
{
    return false;
}

// ---------------------------------------------------------------------------
//  Base::XMLReader: Implementation of the SAX DocumentHandler interface
// ---------------------------------------------------------------------------
void Base::XMLReader::startDocument()
{
    ReadType = StartDocument;
}

void Base::XMLReader::endDocument()
{
    ReadType = EndDocument;
}

void Base::XMLReader::startElement(const XMLCh* const /*uri*/,
                                   const XMLCh* const localname,
                                   const XMLCh* const /*qname*/,
                                   const XERCES_CPP_NAMESPACE_QUALIFIER Attributes& attrs)
{
    Level++;  // new scope
    LocalName = StrX(localname).c_str();

    // saving attributes of the current scope, delete all previously stored ones
    AttrMap.clear();
    for (unsigned int i = 0; i < attrs.getLength(); i++) {
        AttrMap[StrX(attrs.getQName(i)).c_str()] = StrXUTF8(attrs.getValue(i)).c_str();
    }

    ReadType = StartElement;
}

void Base::XMLReader::endElement(const XMLCh* const /*uri*/,
                                 const XMLCh* const localname,
                                 const XMLCh* const /*qname*/)
{
    Level--;  // end of scope
    LocalName = StrX(localname).c_str();

    if (ReadType == StartElement) {
        ReadType = StartEndElement;
    }
    else {
        ReadType = EndElement;
    }
}

void Base::XMLReader::startCDATA()
{
    ReadType = StartCDATA;
}

void Base::XMLReader::endCDATA()
{
    ReadType = EndCDATA;
}

void Base::XMLReader::characters(const XMLCh* const chars, const XMLSize_t length)
{
    Characters = StrX(chars).c_str();
    ReadType = Chars;
    CharacterCount += length;
}

void Base::XMLReader::ignorableWhitespace(const XMLCh* const /*chars*/, const XMLSize_t /*length*/)
{
    // fSpaceCount += length;
}

void Base::XMLReader::resetDocument()
{
    // fAttrCount = 0;
    // fCharacterCount = 0;
    // fElementCount = 0;
    // fSpaceCount = 0;
}


// ---------------------------------------------------------------------------
//  Base::XMLReader: Overrides of the SAX ErrorHandler interface
// ---------------------------------------------------------------------------
void Base::XMLReader::error(const XERCES_CPP_NAMESPACE_QUALIFIER SAXParseException& e)
{
    // print some details to error output and throw an
    // exception to abort the parsing
    cerr << "Error at file " << StrX(e.getSystemId()) << ", line " << e.getLineNumber() << ", char "
         << e.getColumnNumber() << endl;
    throw e;
}

void Base::XMLReader::fatalError(const XERCES_CPP_NAMESPACE_QUALIFIER SAXParseException& e)
{
    // print some details to error output and throw an
    // exception to abort the parsing
    cerr << "Fatal Error at file " << StrX(e.getSystemId()) << ", line " << e.getLineNumber()
         << ", char " << e.getColumnNumber() << endl;
    throw e;
}

void Base::XMLReader::warning(const XERCES_CPP_NAMESPACE_QUALIFIER SAXParseException& e)
{
    // print some details to error output and throw an
    // exception to abort the parsing
    cerr << "Warning at file " << StrX(e.getSystemId()) << ", line " << e.getLineNumber()
         << ", char " << e.getColumnNumber() << endl;
    throw e;
}

void Base::XMLReader::resetErrors()
{}

bool Base::XMLReader::testStatus(ReaderStatus pos) const
{
    return StatusBits.test(static_cast<size_t>(pos));
}

void Base::XMLReader::setStatus(ReaderStatus pos, bool on)
{
    StatusBits.set(static_cast<size_t>(pos), on);
}

void Base::XMLReader::setPartialRestore(bool on)
{
    setStatus(PartialRestore, on);
    setStatus(PartialRestoreInDocumentObject, on);
    setStatus(PartialRestoreInProperty, on);
    setStatus(PartialRestoreInObject, on);
}

void Base::XMLReader::clearPartialRestoreDocumentObject()
{
    setStatus(PartialRestoreInDocumentObject, false);
    setStatus(PartialRestoreInProperty, false);
    setStatus(PartialRestoreInObject, false);
}

void Base::XMLReader::clearPartialRestoreProperty()
{
    setStatus(PartialRestoreInProperty, false);
    setStatus(PartialRestoreInObject, false);
}

void Base::XMLReader::clearPartialRestoreObject()
{
    setStatus(PartialRestoreInObject, false);
}

// ----------------------------------------------------------

// NOLINTNEXTLINE
Base::Reader::Reader(std::istream& str, const std::string& name, int version)
    : std::istream(str.rdbuf())
    , _str(str)
    , _name(name)
    , fileVersion(version)
{}

std::string Base::Reader::getFileName() const
{
    return this->_name;
}

int Base::Reader::getFileVersion() const
{
    return fileVersion;
}

std::istream& Base::Reader::getStream()
{
    return this->_str;
}

void Base::Reader::initLocalReader(std::shared_ptr<Base::XMLReader> reader)
{
    this->localreader = reader;
}

std::shared_ptr<Base::XMLReader> Base::Reader::getLocalReader() const
{
    return (this->localreader);
}
