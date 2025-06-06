/***************************************************************************
 *   Copyright (c) 2020 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include <limits>
#endif

#include "VectorListEditor.h"
#include "ui_VectorListEditor.h"
#include "QuantitySpinBox.h"

#include <App/Application.h>
#include <Base/Console.h>

#include <QClipboard>
#include <QMimeData>
#include <QTextStream>


using namespace Gui;

VectorTableModel::VectorTableModel(int decimals, QObject *parent)
    : QAbstractTableModel(parent)
    , decimals(decimals)
{
}

QVariant VectorTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Vertical)
        return section + 1;

    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section == 0)
        return {QLatin1Char('x')};
    if (section == 1)
        return {QLatin1Char('y')};
    if (section == 2)
        return {QLatin1Char('z')};
    else
        return {};
}

int VectorTableModel::columnCount(const QModelIndex&) const
{
    return 3;
}

int VectorTableModel::rowCount(const QModelIndex &) const
{
    return vectors.size();
}

Qt::ItemFlags VectorTableModel::flags (const QModelIndex & index) const
{
    Qt::ItemFlags fl = QAbstractTableModel::flags(index);
    fl = fl | Qt::ItemIsEditable;
    return fl;
}

bool VectorTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    int r = index.row();
    int c = index.column();
    if (role == Qt::EditRole && r < vectors.size()) {
        if (value.canConvert<Base::Vector3d>()) {
            vectors[r] = value.value<Base::Vector3d>();
            Q_EMIT dataChanged(index, index.sibling(index.row(), 2));
            return true;
        }
        else if (c < 3) {
            double d = value.toDouble();
            if (c == 0)
                vectors[r].x = d;
            else if (c == 1)
                vectors[r].y = d;
            else if (c == 2)
                vectors[r].z = d;
            Q_EMIT dataChanged(index, index);
            return true;
        }
    }
    return QAbstractTableModel::setData(index, value, role);
}

QVariant VectorTableModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        int r = index.row();
        int c = index.column();
        if (r < vectors.size() && c < 3) {
            double d = 0.0;
            if (c == 0)
                d = vectors[r].x;
            else if (c == 1)
                d = vectors[r].y;
            else if (c == 2)
                d = vectors[r].z;

            if (role == Qt::DisplayRole) {
                QString str = QStringLiteral("%1").arg(d, 0, 'f', decimals);
                return str;
            }

            return d;
        }
    }

    return {};
}

QModelIndex VectorTableModel::parent(const QModelIndex &) const
{
    return {};
}

void VectorTableModel::setValues(const QList<Base::Vector3d>& d)
{
    vectors = d;
    beginResetModel();
    endResetModel();
}

void Gui::VectorTableModel::copyToClipboard() const
{
    QString clipboardText;
    QTextStream stream(&clipboardText);
    int precision = App::GetApplication()
                         .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Units")
                         ->GetInt("PropertyVectorListCopyPrecision", 16);

    for (const auto& vector : vectors) {
        stream << QString::number(vector.x, 'f', precision) << '\t'
               << QString::number(vector.y, 'f', precision) << '\t'
               << QString::number(vector.z, 'f', precision) << '\n';
    }

    QApplication::clipboard()->setText(clipboardText);
}

void Gui::VectorTableModel::pasteFromClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    QStringList lines = clipboard->text().split(QLatin1Char('\n'));
    bool okAll = !lines.empty();
    QList<Base::Vector3d> newVectors;
    QLatin1Char tab('\t');
    QLatin1Char semicolon(';');
    QLatin1Char comma(',');

    for (const QString& line : lines) {
        if (line.isEmpty()) {
            continue;
        }
        QChar delimiter = line.count(tab) == 2 ? tab
            : line.count(semicolon) == 2 ? semicolon
            : line.count(comma) == 2 ? comma
            : QChar(QChar::Null);

        if (delimiter.isNull()) {
            okAll = false;
            break;
        }

        QStringList components = line.split(delimiter);

        if (components.size() == 3) {
            bool okX, okY, okZ;
            double x = components.at(0).toDouble(&okX);
            double y = components.at(1).toDouble(&okY);
            double z = components.at(2).toDouble(&okZ);

            if (!okX || !okY || !okZ) {
                okAll = false;
                break;
            }
            newVectors.append(Base::Vector3d(x, y, z));
        }
        else {
            okAll = false;
            break;
        }
    }

    if (okAll) {
        setValues(newVectors);
    }
    else {
        QString msg(tr("Unsupported format.  Must be 3 values per row separated by tabs, semicolons, or commas:") + QLatin1String("\n"));
        msg += clipboard->text();
        Base::Console().error(msg.toStdString().c_str());
    }
}

const QList<Base::Vector3d>& VectorTableModel::values() const
{
    return vectors;
}

bool VectorTableModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (vectors.size() >= row) {
        beginInsertRows(parent, row, row+count-1);
        Base::Vector3d v;
        for (int i=0; i<count; i++)
            vectors.insert(row, v);
        endInsertRows();
        return true;
    }

    return false;
}

bool VectorTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (vectors.size() > row) {
        beginRemoveRows(parent, row, row+count-1);
        for (int i=0; i<count; i++)
            vectors.removeAt(row);
        endRemoveRows();
        return true;
    }

    return false;
}

// --------------------------------------------------------------

VectorTableDelegate::VectorTableDelegate(int decimals, QObject *parent)
    : QItemDelegate(parent)
    , decimals(decimals)
{
}

QWidget *VectorTableDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                           const QModelIndex & /*index*/) const
{
    auto editor = new QDoubleSpinBox(parent);
    editor->setDecimals(decimals);
    editor->setMinimum(std::numeric_limits<int>::min());
    editor->setMaximum(std::numeric_limits<int>::max());
    editor->setSingleStep(0.1);

    return editor;
}

void VectorTableDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    double value = index.model()->data(index, Qt::EditRole).toDouble();

    auto spinBox = static_cast<QDoubleSpinBox*>(editor);
    spinBox->setValue(value);
}

void VectorTableDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                       const QModelIndex &index) const
{
    auto spinBox = static_cast<QDoubleSpinBox*>(editor);
    spinBox->interpretText();
    double value = spinBox->value();
    model->setData(index, value, Qt::EditRole);
}

void VectorTableDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                               const QModelIndex &/* index */) const
{
    editor->setGeometry(option.rect);
}

// --------------------------------------------------------------

/* TRANSLATOR Gui::VectorListEditor */

VectorListEditor::VectorListEditor(int decimals, QWidget* parent)
  : QDialog(parent)
  , ui(new Ui_VectorListEditor)
  , model(new VectorTableModel(decimals))
{
    ui->setupUi(this);
    ui->tableWidget->setItemDelegate(new VectorTableDelegate(decimals, this));
    ui->tableWidget->setModel(model);
    ui->widget->hide();

    ui->coordX->setRange(std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max());
    ui->coordX->setDecimals(decimals);
    ui->coordY->setRange(std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max());
    ui->coordY->setDecimals(decimals);
    ui->coordZ->setRange(std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::max());
    ui->coordZ->setDecimals(decimals);

    ui->toolButtonMouse->setDisabled(true);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &VectorListEditor::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &VectorListEditor::reject);

    connect(ui->spinBox, qOverload<int>(&QSpinBox::valueChanged), this, &VectorListEditor::setCurrentRow);
    connect(ui->toolButtonAdd, &QToolButton::clicked, this, &VectorListEditor::addRow);
    connect(ui->toolButtonRemove, &QToolButton::clicked, this, &VectorListEditor::removeRow);
    connect(ui->toolButtonAccept, &QToolButton::clicked, this, &VectorListEditor::acceptCurrent);
    connect(ui->tableWidget, &QTableView::clicked, this, &VectorListEditor::clickedRow);

    ui->tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableWidget, &QWidget::customContextMenuRequested, this, &VectorListEditor::showContextMenu);

}

VectorListEditor::~VectorListEditor() = default;

void VectorListEditor::showContextMenu(const QPoint& pos)
{
    QMenu contextMenu(ui->tableWidget);
    QAction *copyAction = contextMenu.addAction(tr("Copy table"));
    connect(copyAction, &QAction::triggered, model, &VectorTableModel::copyToClipboard);
    copyAction->setEnabled(!data.empty());

    QAction *pasteAction = contextMenu.addAction(tr("Paste table"));
    connect(pasteAction, &QAction::triggered, model, &VectorTableModel::pasteFromClipboard);
    pasteAction->setEnabled(QApplication::clipboard()->mimeData()->hasText());

    contextMenu.exec(ui->tableWidget->viewport()->mapToGlobal(pos));
}

void VectorListEditor::setValues(const QList<Base::Vector3d>& v)
{
    data = v;
    model->setValues(v);
    if (v.isEmpty()) {
        ui->spinBox->setRange(1, 1);
        ui->spinBox->setEnabled(false);
        ui->toolButtonRemove->setEnabled(false);
        ui->toolButtonAccept->setEnabled(false);
    }
    else {
        ui->spinBox->setRange(1, v.size());
        ui->coordX->setValue(model->data(model->index(0, 0), Qt::EditRole).toDouble());
        ui->coordY->setValue(model->data(model->index(0, 1), Qt::EditRole).toDouble());
        ui->coordZ->setValue(model->data(model->index(0, 2), Qt::EditRole).toDouble());
    }
}

const QList<Base::Vector3d>& VectorListEditor::getValues() const
{
    return data;
}

void VectorListEditor::accept()
{
    data = model->values();
    QDialog::accept();
}

void VectorListEditor::reject()
{
    QDialog::reject();
}

void VectorListEditor::clickedRow(const QModelIndex& index)
{
    QSignalBlocker blocker(ui->spinBox);
    ui->spinBox->setValue(index.row() + 1);
    ui->coordX->setValue(model->data(model->index(index.row(), 0), Qt::EditRole).toDouble());
    ui->coordY->setValue(model->data(model->index(index.row(), 1), Qt::EditRole).toDouble());
    ui->coordZ->setValue(model->data(model->index(index.row(), 2), Qt::EditRole).toDouble());
}

void VectorListEditor::setCurrentRow(int row)
{
    QModelIndex index = model->index(row - 1, 0);
    ui->tableWidget->setCurrentIndex(index);
    ui->coordX->setValue(model->data(model->index(row - 1, 0), Qt::EditRole).toDouble());
    ui->coordY->setValue(model->data(model->index(row - 1, 1), Qt::EditRole).toDouble());
    ui->coordZ->setValue(model->data(model->index(row - 1, 2), Qt::EditRole).toDouble());
}

void VectorListEditor::acceptCurrent()
{
    int row = ui->spinBox->value();
    double x = ui->coordX->value();
    double y = ui->coordY->value();
    double z = ui->coordZ->value();
    QVariant value = QVariant::fromValue<Base::Vector3d>(Base::Vector3d(x, y, z));
    model->setData(model->index(row - 1, 0), value);
}

void VectorListEditor::addRow()
{
    auto newRow = ui->tableWidget->currentIndex().row() + 1;
    model->insertRow(newRow);
    ui->tableWidget->setCurrentIndex(model->index(newRow, 0));
    QSignalBlocker blocker(ui->spinBox);
    ui->spinBox->setMaximum(model->rowCount());
    ui->spinBox->setValue(newRow + 1);
    ui->spinBox->setEnabled(true);
    ui->toolButtonRemove->setEnabled(true);
    ui->toolButtonAccept->setEnabled(true);
    acceptCurrent(); // The new row gets the values from the spinboxes
}

void VectorListEditor::removeRow()
{
    model->removeRow(ui->tableWidget->currentIndex().row());
    int rowCount = model->rowCount();
    if (rowCount > 0) {
        ui->spinBox->setRange(1, rowCount);
    }
    else {
        ui->spinBox->setEnabled(false);
        ui->toolButtonRemove->setEnabled(false);
    }
}

#include "moc_VectorListEditor.cpp"
