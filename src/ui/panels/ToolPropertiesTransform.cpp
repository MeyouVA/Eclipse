// Eclipse - Tool Properties: the Transform tool (selection modes, the pivot, animate modes, operations) and the Drawing Pivot tool.
#include "ui/panels/ToolProperties.h"

#include <QAction>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"

void ToolProperties::addTransformRows()
{
    // A row of buttons, each one a command of the menus (so it shows the same icon, text and state).
    auto row = [this](char const* name, QString const& label, std::initializer_list<char const*> ids) {
        QWidget* const strip = new QWidget(this);
        strip->setObjectName(QLatin1String(name));
        QHBoxLayout* const across = new QHBoxLayout(strip);
        across->setContentsMargins(0, 0, 0, 0);
        if (!label.isEmpty())
            across->addWidget(new QLabel(label, strip));
        QStringList names;
        for (char const* id : ids)
            names << QLatin1String(id);
        strip->setProperty("actions", names);
        m_form->addRow(strip);
        m_transformRows.push_back(strip);
    };
    row("TransformSelectionModes", tr("Selection"), {"PegSelectionMode", "LockSelection", "ShowTransformControls"});
    row("TransformPivotOptions", tr("Pivot"), {"DisablePivotManipulator", "MovePivotToCursor"});
    row("TransformAnimateModes", tr("Animate"), {"AnimateCurrentFrame", "AnimateOnionRange", "AnimateOff"});
    row("TransformOperations", tr("Operations"), {"FlipHorizontalAnimation", "FlipVerticalAnimation", "FlipScaleX", "FlipScaleY", "Rotate90CW", "Rotate90CCW",
                                                  "ResetTransformation", "ResetAll"});

    m_transformFilled = false;

    // The Drawing Pivot tool: one drawing's pivot, or every drawing's of the layer.
    QCheckBox* const all = new QCheckBox(tr("Set the Pivot on All Drawings of the Layer"), this);
    all->setObjectName(QStringLiteral("DrawingPivotAll"));
    all->setToolTip(tr("When enabled, all drawings of the layer share the same pivot location."));
    all->setChecked(settings().value(QStringLiteral("tools/pivotAllDrawings"), false).toBool());
    m_canvas->setPivotAllDrawings(all->isChecked());
    connect(all, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setPivotAllDrawings(on);
        settings().setValue(QStringLiteral("tools/pivotAllDrawings"), on);
    });
    m_form->addRow(all);
    m_pivotRows.push_back(all);
    QPushButton* const reset = new QPushButton(tr("Reset Pivot"), this);
    reset->setObjectName(QStringLiteral("DrawingPivotReset"));
    reset->setToolTip(tr("The drawing goes back to turning about the layer's pivot."));
    connect(reset, &QPushButton::clicked, m_canvas, &Canvas::onActionResetDrawingPivot);
    m_form->addRow(reset);
    m_pivotRows.push_back(reset);
}

void ToolProperties::fillTransformRows()
{
    // (The commands exist once the menus are made: the buttons are put in the first time the tool is chosen.)
    if (m_transformFilled)
        return;
    m_transformFilled = true;
    for (QWidget* const strip : std::as_const(m_transformRows)) {
        QHBoxLayout* const across = qobject_cast<QHBoxLayout*>(strip->layout());
        for (QString const& id : strip->property("actions").toStringList()) {
            if (QAction* const action = m_actions->action(id)) {
                QToolButton* const b = new QToolButton(strip);
                b->setObjectName(QStringLiteral("TransformButton") + id);
                b->setDefaultAction(action);
                b->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
                across->addWidget(b);
            }
        }
        across->addStretch(1);
    }
}
