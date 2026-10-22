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
#include <QSpinBox>
#include <QLabel>

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
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

void ToolProperties::addStylusRows()
{
    // The Pencil: thinner where the pen presses less (into the line's own thickness along it).
    {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/pencilMinSize"), 100).toInt(), 0, 100);
        m_canvas->setPencilMinimumSize(kept);
        m_pencilMinRow = addSetting(tr("Minimum Size"), tr("With a pen tablet: how thick the pencil line is where the pen barely presses, as a percentage of its size.\n"
                                                       "100: the same thickness however hard you press. The thickness stencil is laid over it."),
                                    0, 100, kept, QStringLiteral(" %"), [this](int v) {
                                        m_canvas->setPencilMinimumSize(v);
                                        settings().setValue(QStringLiteral("tools/pencilMinSize"), v);
                                    });
        if (QLayoutItem* const item = m_pencilMinRow->itemAt(1); item && item->widget())
            item->widget()->setObjectName(QStringLiteral("PencilMinimumSize"));
    }
    // The Eraser: how wide it rubs where the pen barely touches.
    {
        int const kept = std::clamp(settings().value(QStringLiteral("tools/eraserMinSize"), 100).toInt(), 0, 100);
        m_canvas->setEraserMinimumSize(kept);
        m_eraserMinRow = addSetting(tr("Eraser Minimum Size"), tr("With a pen tablet: how wide the eraser rubs where the pen barely touches, as a percentage of its size.\n"
                                                              "100: the same size however hard you press."),
                                    0, 100, kept, QStringLiteral(" %"), [this](int v) {
                                        m_canvas->setEraserMinimumSize(v);
                                        settings().setValue(QStringLiteral("tools/eraserMinSize"), v);
                                    });
    }
    // Smoothing with pressure: the Smoothing is the most; Minimum Smoothing, at the lightest pressure, a share of it.
    m_pressureSmoothRow = new QWidget(this);
    {
        QHBoxLayout* const row = new QHBoxLayout(m_pressureSmoothRow);
        row->setContentsMargins(0, 0, 0, 0);
        QCheckBox* const on = new QCheckBox(tr("Smoothing with Pressure"), m_pressureSmoothRow);
        on->setObjectName(QStringLiteral("PressureSmoothing"));
        on->setToolTip(tr("Maximum Smoothing: the Smoothing above, when the pen presses fully.\n"
                          "Minimum Smoothing: the smoothing when the pen barely presses, as a percentage of the maximum."));
        QSpinBox* const least = new QSpinBox(m_pressureSmoothRow);
        least->setObjectName(QStringLiteral("MinimumSmoothing"));
        least->setRange(0, 100);
        least->setSuffix(QStringLiteral(" %"));
        least->setToolTip(tr("Minimum Smoothing: as a percentage of the maximum, at the lightest pressure."));
        on->setChecked(settings().value(QStringLiteral("tools/pressureSmoothing"), false).toBool());
        least->setValue(std::clamp(settings().value(QStringLiteral("tools/minimumSmoothing"), 30).toInt(), 0, 100));
        least->setEnabled(on->isChecked());
        auto send = [this, on, least] {
            m_canvas->setPressureSmoothing(on->isChecked(), least->value());
            least->setEnabled(on->isChecked());
            settings().setValue(QStringLiteral("tools/pressureSmoothing"), on->isChecked());
            settings().setValue(QStringLiteral("tools/minimumSmoothing"), least->value());
        };
        send();
        connect(on, &QCheckBox::toggled, this, send);
        connect(least, &QSpinBox::valueChanged, this, send);
        row->addWidget(on, 1);
        row->addWidget(least);
    }
    m_form->addRow(m_pressureSmoothRow);
    // The Pencil: fainter where the pen presses less, and those changes smoothed along the line.
    m_pencilOpacityRow = new QWidget(this);
    {
        QHBoxLayout* const row = new QHBoxLayout(m_pencilOpacityRow);
        row->setContentsMargins(0, 0, 0, 0);
        QSpinBox* const least = new QSpinBox(m_pencilOpacityRow);
        least->setObjectName(QStringLiteral("PencilMinimumOpacity"));
        least->setRange(0, 100);
        least->setSuffix(QStringLiteral(" %"));
        least->setToolTip(tr("With a pen tablet: how solid the pencil line is where the pen barely presses (100: pressure leaves it solid)."));
        QSpinBox* const smooth = new QSpinBox(m_pencilOpacityRow);
        smooth->setObjectName(QStringLiteral("OpacitySmoothing"));
        smooth->setRange(0, 100);
        smooth->setSuffix(QStringLiteral(" %"));
        smooth->setToolTip(tr("Opacity Smoothing: how much the changes of opacity along the line are smoothed."));
        least->setValue(std::clamp(settings().value(QStringLiteral("tools/pencilMinOpacity"), 100).toInt(), 0, 100));
        smooth->setValue(std::clamp(settings().value(QStringLiteral("tools/opacitySmoothing"), 30).toInt(), 0, 100));
        auto send = [this, least, smooth] {
            m_canvas->setPencilOpacity(least->value(), smooth->value());
            settings().setValue(QStringLiteral("tools/pencilMinOpacity"), least->value());
            settings().setValue(QStringLiteral("tools/opacitySmoothing"), smooth->value());
        };
        send();
        connect(least, &QSpinBox::valueChanged, this, send);
        connect(smooth, &QSpinBox::valueChanged, this, send);
        row->addWidget(new QLabel(tr("Minimum Opacity"), m_pencilOpacityRow));
        row->addWidget(least);
        row->addWidget(new QLabel(tr("Opacity Smoothing"), m_pencilOpacityRow));
        row->addWidget(smooth);
    }
    m_form->addRow(m_pencilOpacityRow);
    // The Smooth Editor and the Pencil Retouch tool: a rub works on one line.
    m_singleStrokeBox = new QCheckBox(tr("Apply to Single Stroke"), this);
    m_singleStrokeBox->setObjectName(QStringLiteral("SingleStroke"));
    m_singleStrokeBox->setToolTip(tr("Snaps to the first pencil line stroke the tool is used on, allowing it to affect only a single stroke.\n"
                                     "Press and hold Shift to temporarily shift between single stroke and multiple strokes modes."));
    m_singleStrokeBox->setChecked(settings().value(QStringLiteral("tools/singleStroke"), false).toBool());
    m_canvas->setSingleStroke(m_singleStrokeBox->isChecked());
    connect(m_singleStrokeBox, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setSingleStroke(on);
        settings().setValue(QStringLiteral("tools/singleStroke"), on);
    });
    m_form->addRow(m_singleStrokeBox);
    // The shape tools on a bitmap layer: smooth or hard edges.
    m_edgeType = new QComboBox(this);
    m_edgeType->setObjectName(QStringLiteral("EdgeType"));
    m_edgeType->addItems({tr("Smooth"), tr("Hard")});
    m_edgeType->setToolTip(tr("Edge Type: on a bitmap layer, the edge of what the shape tools draw is smooth (antialiased) or hard (each pixel all or nothing)."));
    m_edgeType->setCurrentIndex(settings().value(QStringLiteral("tools/hardEdges"), false).toBool() ? 1 : 0);
    m_canvas->setHardEdges(m_edgeType->currentIndex() == 1);
    connect(m_edgeType, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_canvas->setHardEdges(i == 1);
        settings().setValue(QStringLiteral("tools/hardEdges"), i == 1);
    });
    m_form->addRow(tr("Edge Type"), m_edgeType);
    // Overlay Brush Mode: the menu's switch, shown on the Brush and the Pencil.
    m_overlayBox = new QCheckBox(tr("Overlay Brush Mode"), this);
    m_overlayBox->setObjectName(QStringLiteral("OverlayBrushMode"));
    m_overlayBox->setToolTip(tr("Uses the artwork on the selected layers as a mask for the creation of new strokes on the current layer."));
    if (QAction* const action = m_actions->action(QStringLiteral("OverlayBrushMode"))) {
        m_overlayBox->setChecked(action->isChecked());
        connect(m_overlayBox, &QCheckBox::toggled, action, [action](bool on) {
            if (action->isChecked() != on)
                action->trigger();
        });
        connect(action, &QAction::toggled, m_overlayBox, [this](bool on) {
            QSignalBlocker const quiet(m_overlayBox);
            m_overlayBox->setChecked(on);
        });
    }
    m_form->addRow(m_overlayBox);
}
