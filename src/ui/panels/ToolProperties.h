// Eclipse - Tool Properties: what the tool in hand is called, a picture of what it draws,
// its settings, and sizes to pick from.  Only the settings the tool has are shown.
#pragma once

#include "EclipseExport.h"

#include <functional>
#include <QPixmap>
#include <QWidget>

#include "ui/canvas/Canvas.h"

class ActionManager;
class Canvas;
class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
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
    // A thickness stencil of your own, by this name, from the pencil line selected (and chosen). False with none selected.
    bool makeStencil(QString const& name);
    // One of the user's own presets of the tool in hand (by its place among them) made the size and smoothing now set.
    bool updatePreset(int index);
    int importBitmapTips(QString const& path, QComboBox* box);
    // The bitmap brush's tip editor (Angle, Roundness, Spacing, Count, flips, randomness, Minimum Opacity).
    void editTipShape();
    // The presets list: shown with small or large pictures, or as names only (its right-click menu).
    void setPresetView(int view);
    // The same for the tool in hand: the presets may have changed.
    void refresh();
    // A stroke of a given thickness on a light card: what a tool will draw.
    static QPixmap strokePicture(QSize size, double thickness, bool taper);

signals:
    void envelopePointsChanged(int count);
    void envelopeOutlineChanged(bool on);
    // Clear Painted Weights (Rigging tool, Weights).
    void clearWeightsRequested();

private:
    // The parts of the form, from the top down.
    void addSizeRows();
    void addBrushRows();
    void addRiggingRows();
    void addSelectRows();
    void addSnapRows();
    // The Pencil's Line Pushing and Auto Adjust Thickness, Automatically Create Colour Art, the Polyline's Draw Behind.
    void addModeRows();
    void addStylusRows();
    void addAutoFlattenRow();
    void addLineShapeRows();
    void fillTextureBox(class QComboBox* box, int current);
    class QToolButton* textureButton(QString const& name);
    void addThicknessRow();
    void addTextRows();
    void addSelectionRows();
    void addPresetRows();
    void addMorphRows();
    void refreshMorphLayers();
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
    QLayout* m_stabiliserRow = nullptr;
    QLayout* m_lineModeRow = nullptr;
    QLayout* m_stencilRow = nullptr;
    std::function<void(int)> m_showStencil;     // the stencil box set to the stencil of a tool
    QLayout* m_retouchRow = nullptr;
    QLayout* m_eraserTipRow = nullptr;
    QSpinBox* m_stampRandomSize = nullptr;
    void fillStencils(QComboBox* box);
    void fillBitmapTips(QComboBox* box);
    void loadTipShape();
    void saveTipShape(Canvas::TipShape const& shape);
    QCheckBox* m_lineFill = nullptr;
    // The line options: the rows of each (the Brush, the Pencil, the Stroke tool show those that are theirs).
    QVector<QLayout*> m_brushLineRows;
    QVector<QWidget*> m_lineOptionBoxes;
    void addLineOptionRows();
    QCheckBox* m_scaleThickness = nullptr;
    QCheckBox* m_shapeFill = nullptr;
    QLayout* m_cornersRow = nullptr;
    QLayout* m_minSizeRow = nullptr;
    QLayout* m_hardnessRow = nullptr;      // the Brush on a bitmap layer
    QLayout* m_flowRow = nullptr;
    QLayout* m_tipRow = nullptr;
    QLayout* m_gapRow = nullptr;
    QLayout* m_envToolRow = nullptr;
    QCheckBox* m_brushShape = nullptr;
    QCheckBox* m_drawBehind = nullptr;
    QCheckBox* m_paperCrisp = nullptr;
    QWidget* m_paperRamp = nullptr;     // the ramp of the texture: from which grain there is ink, from which it is solid
    QWidget* m_lineRamp = nullptr;
    QWidget* rampRow(QString const& name, bool brush);
    QCheckBox* m_lineCrisp = nullptr;
    QHBoxLayout* m_paperRow = nullptr;      // the paper texture of the brush, and how much of it shows
    QComboBox* m_rigMode = nullptr;
    QSpinBox* m_weightBone = nullptr;           // Rigging, Weights: the bone painted for
    QDoubleSpinBox* m_weightSize = nullptr;     // and how big a dab is
    QPushButton* m_weightClear = nullptr;       // all the painted weights of the layer taken away
    QSpinBox* m_envelopePoints = nullptr;
    QCheckBox* m_applyAll = nullptr;
    QCheckBox* m_dropperAll = nullptr;
    QCheckBox* m_byColour = nullptr;
    QCheckBox* m_paintAll = nullptr;
    QCheckBox* m_paintLayers = nullptr;
    QCheckBox* m_bothArts = nullptr;
    QCheckBox* m_selectBothArts = nullptr;
    QCheckBox* m_permanent = nullptr;
    QCheckBox* m_envelopeOutline = nullptr;
    QCheckBox* m_snapAlign = nullptr;
    QCheckBox* m_snapMaster = nullptr;
    QCheckBox* m_hideManipulator = nullptr;
    QCheckBox* m_ceControls = nullptr;
    QCheckBox* m_envAdvanced = nullptr;
    QWidget* m_cutterRow = nullptr;
    QWidget* m_repositionRow = nullptr;
    QWidget* m_pencilModeRow = nullptr;
    QCheckBox* m_autoColour = nullptr;
    QCheckBox* m_polyBehind = nullptr;
    QLayout* m_eraserMinRow = nullptr;
    QLayout* m_pencilMinRow = nullptr;
    QWidget* m_pressureSmoothRow = nullptr;
    QWidget* m_pencilOpacityRow = nullptr;
    QCheckBox* m_singleStrokeBox = nullptr;
    QComboBox* m_edgeType = nullptr;
    QCheckBox* m_overlayBox = nullptr;
    QSpinBox* m_stampScale = nullptr;
    QWidget* m_stampRow = nullptr;      // which stamp, and taking it off the list
    QCheckBox* m_stampRandom = nullptr;
    QComboBox* m_stampList = nullptr;
    QCheckBox* m_autoFlatten = nullptr;
    QHBoxLayout* m_shapeRow = nullptr;
    QHBoxLayout* m_textureRow = nullptr;     // the pencil texture, and how much of it shows
    QLayout* m_thicknessRow = nullptr;
    QWidget* m_fontRow = nullptr;
    QLayout* m_textRow = nullptr;
    QLayout* m_paraRow = nullptr;
    QWidget* m_selection = nullptr;
    QLabel* m_presetsTitle = nullptr;
    QListWidget* m_presets = nullptr;
    QListWidget* m_morphLayers = nullptr;
    QWidget* m_morphButtons = nullptr;
    bool m_fillingMorphs = false;
};
