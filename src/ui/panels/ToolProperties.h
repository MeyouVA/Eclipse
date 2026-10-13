// Eclipse - Tool Properties: what the tool in hand is called, a picture of what it draws,
// its settings, and sizes to pick from.  Only the settings the tool has are shown.
#pragma once

#include "EclipseExport.h"

#include <functional>
#include <QPixmap>
#include <QWidget>

class ActionManager;
class Canvas;
class Document;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLayout;
class QListWidget;
class QSlider;
class QSpinBox;
class StrokePreview;

class ECLIPSE_UI_API ToolProperties : public QWidget {
    Q_OBJECT

public:
    // `envelopePoints`: how many points Create Envelope puts round a drawing, to begin with.
    ToolProperties(Document* document, Canvas* canvas, ActionManager* actions, int envelopePoints, QWidget* parent = nullptr);
    // Shows the settings of a tool (Canvas::Tool), and its sizes to pick from.
    void showFor(int tool);
    // The same for the tool in hand: the presets may have changed.
    void refresh();
    // A stroke of a given thickness on a light card: what a tool will draw.
    static QPixmap strokePicture(QSize size, double thickness, bool taper);

signals:
    void envelopePointsChanged(int count);
    void envelopeOutlineChanged(bool on);

private:
    // The parts of the form, from the top down.
    void addSizeRows();
    void addBrushRows();
    void addRiggingRows();
    void addSelectRows();
    void addAutoFlattenRow();
    void addLineShapeRows();
    void addThicknessRow();
    void addTextRows();
    void addSelectionRows();
    void addPresetRows();
    // A slider with a box beside it, under a label.
    QLayout* addSetting(QString const& label, QString const& tip, int min, int max, int value, QString const& suffix, std::function<void(int)> apply);
    // A box of small pictures: how a line starts (kind 0), ends (1), or turns a corner (2).
    void addShapeBox(QString const& label, QString const& tip, int kind, QStringList const& names, std::function<void(int)> apply);
    // The sizes of the kind of tool in hand, then the ones the user made for it.
    void listPresets(bool brush, bool eraser, bool draws);

    Document* m_document;
    Canvas* m_canvas;
    ActionManager* m_actions;
    int m_envelopeCount;

    QFormLayout* m_form = nullptr;
    QLabel* m_toolTitle = nullptr;
    StrokePreview* m_preview = nullptr;
    QLabel* m_noOptions = nullptr;
    QSlider* m_size = nullptr;
    QLayout* m_sizeRow = nullptr;
    QLayout* m_smoothingRow = nullptr;
    QLayout* m_cornersRow = nullptr;
    QLayout* m_minSizeRow = nullptr;
    QLayout* m_gapRow = nullptr;
    QCheckBox* m_brushShape = nullptr;
    QCheckBox* m_drawBehind = nullptr;
    QComboBox* m_rigMode = nullptr;
    QSpinBox* m_envelopePoints = nullptr;
    QCheckBox* m_applyAll = nullptr;
    QCheckBox* m_dropperAll = nullptr;
    QCheckBox* m_byColour = nullptr;
    QCheckBox* m_paintAll = nullptr;
    QCheckBox* m_permanent = nullptr;
    QCheckBox* m_envelopeOutline = nullptr;
    QCheckBox* m_snapAlign = nullptr;
    QSpinBox* m_stampScale = nullptr;
    QCheckBox* m_autoFlatten = nullptr;
    QHBoxLayout* m_shapeRow = nullptr;
    QLayout* m_thicknessRow = nullptr;
    QWidget* m_fontRow = nullptr;
    QLayout* m_textRow = nullptr;
    QLayout* m_paraRow = nullptr;
    QWidget* m_selection = nullptr;
    QLabel* m_presetsTitle = nullptr;
    QListWidget* m_presets = nullptr;
};
