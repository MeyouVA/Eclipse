// Eclipse - the timeline: one row per layer (front layer on top), one column per frame.
// At the left of a row are the switches of the layer (enabled, solo, locked, colour), its
// name and what it shows on the current frame; at the right its cells.  A filled cell is
// an exposed drawing; a dot marks where a new drawing starts.
#pragma once

#include "EclipseExport.h"

#include <QAbstractScrollArea>
#include <QSet>
#include <QVector>
#include <QHash>

#include <memory>

class Document;
struct Pose;
class QLineEdit;
class QSlider;
struct CellBlock;
struct SoundData;

class ECLIPSE_UI_API Timeline : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit Timeline(Document* document, QWidget* parent = nullptr);

    int currentLayer() const { return m_layer; }
    int currentFrame() const { return m_frame; }

    // The switches at the left of a row.
    enum Switch { Enable, Solo, Lock, Onion, Colour };

    // Where things are, in the coordinates of viewport(): for the views that point at them.
    QRect cellRect(int layer, int frame) const;
    QRect switchRect(int layer, Switch which) const;
    // The triangle that folds the layers under `layer` away.
    QRect foldRect(int layer) const;
    QRect nameRect(int layer) const;
    // Where the mark of a synced layer is drawn in its name (two links of a chain); nothing for a layer that is not synced.
    QRect syncMarkRect(int layer) const;
    // The colour of the mark: layers synced with each other have the same one.
    static QColor syncColour(int group) { return QColor::fromHsv((group * 67 + 150) % 360, 150, 235); }

    // ---- the parameters of a layer: a row each under it, opened with the + after its name ----
    // (Fov last: only cameras have it.)  After them, a row for the amount of each effect of the layer: ParameterCount + its place.
    enum Parameter : int { PosX, PosY, ScaleX, ScaleY, Angle, Opacity, PosZ, Skew, Fov, ParameterCount };
    // And then a row for the angle of each bone of its chain: kBoneRows + the bone.
    static constexpr int kBoneRows = ParameterCount + 1000;
    // And two for each point of its deformer, how far the pose has moved it across and down: kWarpRows + 2 * the point (+ 1).
    static constexpr int kWarpRows = ParameterCount + 2000;
    static bool isEffectRow(Parameter which) { return which >= ParameterCount && which < kBoneRows; }
    static bool isBoneRow(Parameter which) { return which >= kBoneRows && which < kWarpRows; }
    static bool isWarpRow(Parameter which) { return which >= kWarpRows; }
    // The name a row goes by: a parameter's, or the effect's of the layer; whether its value is a share shown in percent.
    QString rowName(int layer, Parameter which) const;
    bool rowInPercent(int layer, Parameter which) const;
    // Whether a kind of layer has that parameter (the angle of view is a camera's alone).
    static bool parameterFor(int layerKind, Parameter which);
    static QString parameterName(Parameter which);
    // That value of a pose, read and set.
    static double parameterOf(Pose const& pose, Parameter which);
    static void setParameterOf(Pose& pose, Parameter which, double value);
    // Whether the layer has any (sounds, groups and colour-cards do not move).
    bool hasParameters(int layer) const;
    bool parametersShown(int layer) const;
    void setParametersShown(int layer, bool shown);
    // The + (or -) of a layer; the row of one of its parameters; the box with its value.
    QRect parameterToggleRect(int layer) const;
    QRect parameterRowRect(int layer, Parameter which) const;
    QRect parameterValueRect(int layer, Parameter which) const;
    // The value on the current frame, as it is shown; and set there, which makes a keyframe.
    double parameter(int layer, Parameter which) const;
    // The layer a parameter row follows (an attribute control of the layer), or -1 when it goes by its own keyframes.
    int drivenBy(int layer, Parameter which) const;
    QString parameterText(int layer, Parameter which) const;
    void setParameter(int layer, Parameter which, double value);
    // Opens the box of a value for typing in (Enter takes it, Escape leaves it).
    void editParameter(int layer, Parameter which);
    // The buttons over the names: add a layer, delete, add a peg.
    enum HeaderButton { AddLayer, DeleteLayer, AddPeg };
    QRect headerButtonRect(HeaderButton which) const;
    // The mark on the ruler at the end of the scene; dragging it makes the scene longer
    // or shorter.
    QRect sceneEndRect() const;
    // The band of a scene marker on the ruler.
    QRect markerRect(int marker) const;
    // What is selected: the current cell, or a block of cells dragged or Shift-clicked out
    // from it.  The layers of the block top to bottom, and its first and last frame.
    QVector<int> selectedLayers() const;
    int selectionFirstFrame() const;
    int selectionLastFrame() const;
    bool hasRange() const { return m_extentRow >= 0; }
    // Selects from the current cell to this one.
    void extendSelection(int layer, int frame);
    // A layer picked on its own with Ctrl, besides the block: in or out.
    void toggleLayer(int layer);
    // What was copied, pasted in part: its drawings (exposure), its keyframes, or both; so
    // many times over; from its last frame to its first.
    void pasteSpecial(bool drawings, bool keyframes, int cycles, bool reverse);
    // These layers picked besides the current one (those that are not it).
    void setExtraLayers(QVector<int> const& layers);
    // Whether sound layers show how loud the sound is along their row.
    bool showWaveforms() const { return m_waveforms; }
    // The sound a layer that shows a symbol plays (the sounds inside the symbol, where it shows them), from the scene's
    // first frame; null when the symbol has none.  Worked out once for each way the layer and the symbol are.
    std::shared_ptr<SoundData> symbolSound(int layer) const;
    void setShowWaveforms(bool on);
    // The onion skin: whether it is on, and how far before and after the current frame it
    // reaches.  Shown on the ruler, with a handle at each end to drag.
    void setOnion(bool on, int previous, int next);
    QRect onionHandleRect(bool before) const;
    // Moves (or copies) the block selected by so many rows and frames; the block stays selected.
    void moveSelection(int rows, int frames, bool copy);
    void clearRange();
    static bool canPasteCells();
    // What was cut or copied last, in this view or in the Xsheet.
    static CellBlock& clipboard();
    // Pastes it from its last frame to its first; or so many times over.
    void pasteReversed();
    void pasteCycle(int times);
    // The cells selected, again and again after themselves: `times` in all.
    void createCycle(int times);
    // Which layers have a row: all of them; only those that were selected when this was asked
    // for; or only the tagged ones.  (The current layer always has its row.)
    enum ViewMode { ShowAll, ShowSelected, ShowTagged };
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);
    // Show: the kinds of layer (Layer::Kind) that have rows.  The current layer always has its row.
    bool kindShown(int kind) const { return !m_hiddenKinds.contains(kind); }
    void setKindShown(int kind, bool shown);
    // The layers that have a row, top to bottom.
    QVector<int> rows() const;

    // How wide a frame is, in pixels.
    int cellWidth() const { return m_cellWidth; }
    void setCellWidth(int width);
    // The frames playback runs between (both shown on the ruler).
    void setPlayRange(int start, int stop);

public slots:
    // Clamps to the scene and emits currentCellChanged when something changed.
    void setCurrentCell(int layer, int frame);
    void setCurrentFrame(int frame) { setCurrentCell(m_layer, frame); }
    // What the Edit menu does when this view has the focus: on the cells selected.
    void deleteSelection();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void deselectAll() { clearRange(); }
    // View > Zoom In and the rest, with this view in use: how wide the frames are.
    void onActionZoomIn() { setCellWidth(m_cellWidth + 2); }
    void onActionZoomOut() { setCellWidth(m_cellWidth - 2); }
    void onActionResetZoom() { setCellWidth(8); }

signals:
    // A handle of the onion skin was dragged on the ruler.
    void onionRangeChanged(int previous, int next);
    void currentCellChanged(int layer, int frame);
    void renameLayerRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);
    void addLayerMenuRequested(QPoint const& globalPos);
    void deleteLayerRequested();
    void addPegRequested();
    void trackColourRequested(int layer);
    void selectionChanged();
    // A template dragged out of the Library was let go over this frame.
    void templateDropped(QString const& path, int frame, bool onCells);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    QSize sizeHint() const override { return QSize(800, 180); }

private:
    void onDocumentChanged();
    void updateScrollBars();
    void rebuildRows();
    // Row 0 is the front layer, i.e. the last one of the scene.  -1 for a layer folded away.
    int rowOfLayer(int layer) const;
    int layerOfRow(int row) const;
    // The row under a point of the viewport, or -1.
    int rowAt(int y) const;
    int frameAt(int x) const;
    void pick(QPoint pos, bool press);
    void pasteBlock(CellBlock const& block, int frame);
    bool pressSwitch(QPoint pos);

    static constexpr int kHeaderWidth = 460;   // switches, name, parameters
    static constexpr int kSwitchWidth = 16;
    static constexpr int kSwitches = 5 * kSwitchWidth + 6;   // where the names start
    static constexpr int kParamWidth = 140;
    static constexpr int kRulerHeight = 20;
    static constexpr int kRowHeight = 20;
    static constexpr int kPastEnd = 40;        // frames of ruler shown after the scene ends

    // Dragging a layer by its name: dropped on the middle of another it is attached to
    // it, dropped on the edge between two it moves there.
    enum Drop { DropNone, DropOnto, DropAbove, DropBelow };
    Drop dropAt(QPoint pos, int& targetLayer) const;

    Document* m_document;
    // A row each: the layer, and for the row of a parameter which one (-1: the layer itself).
    QVector<int> m_rows;
    ViewMode m_viewMode = ShowAll;
    QVector<int> m_hiddenKinds;
    QVector<int> m_shownIds;     // ShowSelected: the ids of the layers that were selected
    QVector<int> m_params;
    QSet<int> m_open;          // the ids of the layers whose parameters are shown
    QLineEdit* m_entry = nullptr;   // a value being typed
    int m_entryLayer = -1;
    Parameter m_entryParam = PosX;
    void closeEntry(bool take);
    bool isParamRow(int row) const { return row >= 0 && row < m_params.size() && m_params[row] >= 0; }
    QSlider* m_zoom = nullptr;
    int m_cellWidth = 8;
    int m_layer = 0;
    int m_frame = 0;
    int m_playStart = 0;
    int m_playStop = -1;       // -1: the end of the scene
    int m_dragLayer = -1;      // the layer whose name was pressed
    QPoint m_dragOrigin;
    bool m_dragging = false;   // the pointer has moved far enough to mean it
    Drop m_drop = DropNone;
    int m_dropLayer = -1;
    int m_dragMarker = -1;     // the scene marker being dragged along the ruler
    // A parameter's value dragged left or right in its box: whose, which, from where, and what it was.
    int m_dragParamLayer = -1;
    Parameter m_dragParam = PosX;
    int m_dragParamX = 0;
    double m_dragParamFrom = 0.0;
    bool m_dragParamBegun = false;
    int m_markerGrab = 0;      // how many frames into it it was taken hold of
    int m_markerFrame = 0;     // where it starts as it is dragged
    bool m_dragEnd = false;    // the end of the scene is being dragged
    // The far corner of the block selected, from the current cell; -1 when there is none.
    int m_extentRow = -1;
    int m_extentFrame = -1;
    bool m_selecting = false;  // a block is being dragged out
    bool m_moving = false;     // the block selected is being dragged somewhere else
    int m_moveRow = 0, m_moveFrame = 0;     // the cell it was taken by
    int m_moveRows = 0, m_moveFrames = 0;   // how far it has been dragged
    bool m_waveforms = true;
    mutable QHash<int, QPair<quint64, std::shared_ptr<SoundData>>> m_symbolSounds;  // by layer id: what it was worked out for, and the sound
    bool m_onionOn = false;
    int m_onionPrev = 1, m_onionNext = 1;
    int m_dragOnion = 0;       // -1: the handle before is being dragged; +1: the one after
    QVector<int> m_extraLayers;             // picked with Ctrl, besides the block
    // How far the block may go without leaving the rows or the scene.
    void clampMove(int& rows, int& frames) const;
    bool m_scrubbing = false;  // the ruler is being dragged through
    int m_endFrames = 0;       // and the scene would be this long
};
