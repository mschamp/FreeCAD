/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer                                    *
 *                                <jrheinlaender[at]users.sourceforge.net> *
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
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/nodes/SoSeparator.h>
#endif

#include "Gui/Control.h"
#include "FemGuiTools.h"
#include "TaskFemConstraintBearing.h"
#include "ViewProviderFemConstraintBearing.h"
#include <Base/Console.h>
#include <Mod/Fem/App/FemConstraintBearing.h>


using namespace FemGui;

PROPERTY_SOURCE(FemGui::ViewProviderFemConstraintBearing, FemGui::ViewProviderFemConstraint)


ViewProviderFemConstraintBearing::ViewProviderFemConstraintBearing()
{
    sPixmap = "FEM_ConstraintBearing";
}

ViewProviderFemConstraintBearing::~ViewProviderFemConstraintBearing() = default;

bool ViewProviderFemConstraintBearing::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Default) {
        Gui::Control().closeDialog();
        // clear the selection (convenience)
        Gui::Selection().clearSelection();
        Gui::Control().showDialog(new TaskDlgFemConstraintBearing(this));

        return true;
    }
    else {
        return ViewProviderFemConstraint::setEdit(ModNum);
    }
}

void ViewProviderFemConstraintBearing::updateData(const App::Property* prop)
{
    // Gets called whenever a property of the attached object changes
    Fem::ConstraintBearing* pcConstraint = this->getObject<Fem::ConstraintBearing>();

    if (prop == &pcConstraint->References) {
        Base::Console().error("\n");  // enable a breakpoint here
    }

    if (prop == &pcConstraint->BasePoint) {
        // Remove and recreate the symbol
        Gui::coinRemoveAllChildren(pShapeSep);

        // This should always point outside of the cylinder
        Base::Vector3d normal = pcConstraint->NormalDirection.getValue();
        Base::Vector3d base = pcConstraint->BasePoint.getValue();
        double radius = pcConstraint->Radius.getValue();
        base = base + radius * normal;

        SbVec3f b(base.x, base.y, base.z);
        SbVec3f dir(normal.x, normal.y, normal.z);
        SbRotation rot(SbVec3f(0, -1, 0), dir);

        GuiTools::createPlacement(pShapeSep, b, rot);
        pShapeSep->addChild(GuiTools::createFixed(radius / 2,
                                                  radius / 2 * 1.5,
                                                  pcConstraint->AxialFree.getValue()));
    }
    else if (prop == &pcConstraint->AxialFree) {
        if (pShapeSep->getNumChildren() > 0) {
            // Change the symbol
            Base::Vector3d normal = pcConstraint->NormalDirection.getValue();
            Base::Vector3d base = pcConstraint->BasePoint.getValue();
            double radius = pcConstraint->Radius.getValue();
            base = base + radius * normal;

            SbVec3f b(base.x, base.y, base.z);
            SbVec3f dir(normal.x, normal.y, normal.z);
            SbRotation rot(SbVec3f(0, -1, 0), dir);

            GuiTools::updatePlacement(pShapeSep, 0, b, rot);
            const SoSeparator* sep = static_cast<SoSeparator*>(pShapeSep->getChild(2));
            GuiTools::updateFixed(sep,
                                  0,
                                  radius / 2,
                                  radius / 2 * 1.5,
                                  pcConstraint->AxialFree.getValue());
        }
    }

    ViewProviderFemConstraint::updateData(prop);
}
