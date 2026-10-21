// Eclipse - Tool Properties: the Pencil's ways of drawing over lines (Line Pushing, Auto Adjust Thickness) and their highlight
// colour, Automatically Create Colour Art on the tools that draw lines, and the Polyline's Draw Behind.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>

#include "actions/manager/ActionManager.h"
#include "ui/canvas/Canvas.h"
#include "ui/window/MainWindowPrivate.h"

void ToolProperties::addModeRows()
{
    // The Pencil: draw, or push the line drawn along, or make it thicker; and the colour the stroke is shown in then.
    m_pencilModeRow = new QWidget(this);
    {
        QHBoxLayout* const row = new QHBoxLayout(m_pencilModeRow);
        row->setContentsMargins(0, 0, 0, 0);
        QComboBox* const mode = new QComboBox(m_pencilModeRow);
        mode->setObjectName(QStringLiteral("PencilMode"));
        mode->addItems({tr("Draw"), tr("Line Pushing Mode"), tr("Auto Adjust Thickness")});
        mode->setToolTip(tr("Line Pushing Mode: highlight strokes drawn over an existing line section will move that section in the direction of the stroke.\n"
                            "Auto Adjust Thickness: highlight strokes drawn over an existing line section will make that section thicker,\n"
                            "according to the shape and curve of the stroke."));
        mode->setCurrentIndex(std::clamp(settings().value(QStringLiteral("tools/pencilMode"), 0).toInt(), 0, 2));
        m_canvas->setPencilMode(mode->currentIndex());
        connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setPencilMode(i);
            settings().setValue(QStringLiteral("tools/pencilMode"), i);
        });
        QPushButton* const colour = new QPushButton(m_pencilModeRow);
        colour->setObjectName(QStringLiteral("PencilHighlightColour"));
        colour->setToolTip(tr("Highlight Colour: used for Line Pushing and Auto Adjust Thickness modes."));
        auto show = [colour](QColor const& c) {
            colour->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-width: 24px; min-height: 16px;").arg(c.name()));
        };
        QColor const kept(settings().value(QStringLiteral("tools/pushHighlight"), QStringLiteral("#ff8c00")).toString());
        m_canvas->setPushHighlight(kept.isValid() ? kept : QColor(255, 140, 0));
        show(m_canvas->pushHighlight());
        connect(colour, &QPushButton::clicked, this, [this, show] {
            QColor const chosen = QColorDialog::getColor(m_canvas->pushHighlight(), this, tr("Highlight Colour"));
            if (!chosen.isValid())
                return;
            m_canvas->setPushHighlight(chosen);
            settings().setValue(QStringLiteral("tools/pushHighlight"), chosen.name());
            show(chosen);
        });
        row->addWidget(mode, 1);
        row->addWidget(colour);
    }
    m_form->addRow(tr("Mode"), m_pencilModeRow);
    // Automatically Create Colour Art: the menu's switch, shown on every tool that draws lines.
    m_autoColour = new QCheckBox(tr("Automatically Create Colour Art"), this);
    m_autoColour->setObjectName(QStringLiteral("ToolAutoColourArt"));
    m_autoColour->setToolTip(tr("New strokes create corresponding strokes in the Colour Art layer."));
    if (QAction* const action = m_actions->action(QStringLiteral("AutoColourArt"))) {
        m_autoColour->setChecked(action->isChecked());
        connect(m_autoColour, &QCheckBox::toggled, action, [action](bool on) {
            if (action->isChecked() != on)
                action->trigger();
        });
        connect(action, &QAction::toggled, m_autoColour, [this](bool on) {
            QSignalBlocker const quiet(m_autoColour);
            m_autoColour->setChecked(on);
        });
    }
    m_form->addRow(m_autoColour);
    // The Polyline: Draw Behind Current Layers.
    m_polyBehind = new QCheckBox(tr("Draw Behind Current Layers"), this);
    m_polyBehind->setObjectName(QStringLiteral("PolylineBehind"));
    m_polyBehind->setToolTip(tr("The lines drawn go under everything else of the art layer."));
    m_polyBehind->setChecked(settings().value(QStringLiteral("tools/polylineBehind"), false).toBool());
    m_canvas->setPolylineBehind(m_polyBehind->isChecked());
    connect(m_polyBehind, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setPolylineBehind(on);
        settings().setValue(QStringLiteral("tools/polylineBehind"), on);
    });
    m_form->addRow(m_polyBehind);
}
