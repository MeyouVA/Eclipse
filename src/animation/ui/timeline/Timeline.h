// Eclipse - the timeline: one row per layer (front layer on top), one column per frame.
// At the left of a row are the switches of the layer (enabled, solo, locked, colour), its
// name and what it shows on the current frame; at the right its cells.  A filled cell is
// an exposed drawing; a dot marks where a new drawing starts.
#pragma once

class QPainter;
struct TimelinePaintSpace;

#include "EclipseExport.h"

#include <QAbstractScrollArea>
#include <QSet>
#include <QVector>
#include <QHash>
#include <QPixmap>

#include <memory>

class Document;
class Scene;
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
    static constexpr int kControllerRows = ParameterCount + 3000;
    static bool isEffectRow(Parameter which) { return which >= ParameterCount && which < kBoneRows; }
    static bool isBoneRow(Parameter which) { return which >= kBoneRows && which < kWarpRows; }
    static bool isWarpRow(Parameter which) { return which >= kWarpRows && which < kControllerRows; }
    static bool isControllerRow(Parameter which) { return which >= kControllerRows; }
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
    // Lock Timeline Ruler: the end of the scene in the ruler cannot be dragged (the length is not changed by a slip).
    bool rulerLocked() const { return m_rulerLocked; }
    // Overwrite Exposures and Keyframes During Drag and Drop: off, a block dropped pushes what is there on.
    bool overwriteDrop() const { return m_overwriteDrop; }
    void setOverwriteDrop(bool on) { m_overwriteDrop = on; }
    // Show End of Scene Marker: the bracket at the end of the scene, which can be dragged.
    void setShowSceneEnd(bool on);
    // Reduce Indentation: the layers under others set in less far.
    void setReduceIndentation(bool on);
    int indentOf(int layer) const;
    void setRulerLocked(bool on) { m_rulerLocked = on; }
    // The layer a parameter row follows (an attribute control of the layer), or -1 when it goes by its own keyframes.
    int drivenBy(int layer, Parameter which) const;
    QString parameterText(int layer, Parameter which) const;
    void setParameter(int layer, Parameter which, double value);
    // Opens the box of a value for typing in (Enter takes it, Escape leaves it).
    void editParameter(int layer, Parameter which);
    // The buttons over the names: add a layer, delete, add a peg.
    enum HeaderButton { AddLayer, DeleteLayer, AddPeg };
    QRect headerButtonRect(HeaderButton which) const;
    QRect sceneEndRect() const;
    // The band of a scene marker on the ruler.
    QRect markerRect(int marker) const;
    QVector<int> selectedLayers() const;
    int selectionFirstFrame() const;
    int selectionLastFrame() const;
    bool hasRange() const { return m_extentRow >= 0; }
    // Selects from the current cell to this one.
    void extendSelection(int layer, int frame);
    // A layer picked on its own with Ctrl, besides the block: in or out.
    void toggleLayer(int layer);
    struct PasteOptions {
        bool drawings = true, keyframes = true;
        int cycles = 1;
        bool reverse = false;
        int drawingMode = -1;       // CellBlock::DrawingMode
        int parts = 1023;           // Keyframe::Part
        bool matchNames = false, pivot = false;
        bool offsetKeys = false;
        bool currentSetOnly = false;
        bool updateDrawingPivot = false;
        // Alt-dragging cells out: the gap they leave holds the drawing before it.
        bool fillGap = true;
        // Dropped or pasted into an exposure: the last drawing put down holds on over the rest of it (up to the next key exposure or empty frame).
        bool fillToKey = false;
        // Force Keyframes at Function's Beginning and End: the layers pasted onto keep their motion outside the paste (a keyframe of it each side).
        bool edgeKeys = false;
    };
    void pasteSpecial(bool drawings, bool keyframes, int cycles, bool reverse);
    // Preference: a plain Paste or a drop of cells adds keyframes where it begins and ends on the animated layers it lands on.
    static bool& pasteAddsEdgeKeys();
    // Keyframes at first and last on these layers (those animated, where they have none), as each is there now.
    void addEdgeKeys(QVector<int> const& layers, int first, int last);
    void pasteSpecial(PasteOptions const& options);
    // A paste preset: the plain Paste pastes as it says (none: as it always does).
    void setPastePreset(PasteOptions const* preset) { m_hasPreset = preset != nullptr; if (preset) m_preset = *preset; }
    // What an Alt-drag of cells fills in (when no paste preset is in use).
    void setDragFill(bool gap, bool toKey) { m_dragGap = gap; m_dragToKey = toKey; }
    // These layers picked besides the current one (those that are not it).
    void setExtraLayers(QVector<int> const& layers);
    // Whether sound layers show how loud the sound is along their row.
    bool showWaveforms() const { return m_waveforms; }
    std::shared_ptr<SoundData> symbolSound(int layer) const;
    void setShowWaveforms(bool on);
    void setOnion(bool on, int previous, int next);
    QRect onionHandleRect(bool before) const;
    // Moves (or copies) the block selected by so many rows and frames; the block stays selected.
    void moveSelection(int rows, int frames, bool copy, bool alt = false);
    void clearRange();
    static bool canPasteCells();
    // A layer dragged in the list and let go above (below) a row: it goes to that row's level, there (see TimelineInput).
    void dropLayer(int layer, int target, bool below);
    // The row a layer is listed in (its own row, not one of its parameters'); -1 when it is not listed.
    int rowOfLayer(int layer) const;
    // What was cut or copied last, in this view or in the Xsheet.
    static CellBlock& clipboard();
    // Pastes it from its last frame to its first; or so many times over.
    void pasteReversed();
    void pasteCycle(int times, int kind = 0);
    // The cells selected, again and again after themselves: `times` in all.
    void createCycle(int times, int kind = 0);
    enum ViewMode { ShowAll, ShowSelected, ShowTagged, ShowControlled, ShowSameGroup };
    // Whether a layer is among those ShowControlled shows.
    bool controlled(int layer) const;
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);
    // Show: the kinds of layer (Layer::Kind) that have rows.  The current layer always has its row.
    bool kindShown(int kind) const { return !m_hiddenKinds.contains(kind); }
    void setKindShown(int kind, bool shown);
    // Whether a layer's track shows thumbnails of its drawings (a drawing layer with them on).
    bool thumbnailsShown(int layer) const;
    // Toggle Pattern: thumbnails on a chequer, to see what is see-through, or on the library thumbnail colour.
    bool thumbnailPattern() const { return m_thumbPattern; }
    void setThumbnailPattern(bool on);
    // Scrolls the list so that the current layer's row is in the middle.
    void centreOnSelection();
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
    // Paste, with artwork copied last: it goes into the current drawing.
    void pasteArtwork();
    // A handle of the onion skin was dragged on the ruler.
    void onionRangeChanged(int previous, int next);
    void currentCellChanged(int layer, int frame);
    void renameLayerRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);
    // The ruler right-clicked: its menu (the current frame is the one clicked).
    void rulerMenuRequested(QPoint const& globalPos);
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
    // The painting, in parts: what it is done with, then the cells, the ruler and the layer headers.
    TimelinePaintSpace paintSpace() const;
    void paintCells(QPainter& p, TimelinePaintSpace const& space);
    void paintRuler(QPainter& p, TimelinePaintSpace const& space);
    void paintHeaders(QPainter& p, TimelinePaintSpace const& space);
    // The pictures of the drawings along a track that shows thumbnails.
    void paintThumbnails(QPainter& p, TimelinePaintSpace const& space, int layer, int y, int rowHeight);
    void onDocumentChanged();
    void updateScrollBars();
    void rebuildRows();
    // Row 0 is the front layer, i.e. the last one of the scene.  -1 for a layer folded away.
    int layerOfRow(int row) const;
    // The row under a point of the viewport, or -1.
    int rowAt(int y) const;
    // Where a row starts down the list (not scrolled), and how tall it is; rowTop(rows) is the height of them all.
    int rowTop(int row) const { return row >= 0 && row < m_rowTops.size() ? m_rowTops[row] : m_rowTops.isEmpty() ? 0 : m_rowTops.last(); }
    int rowHeight(int row) const { return row >= 0 && row + 1 < m_rowTops.size() ? m_rowTops[row + 1] - m_rowTops[row] : kRowHeight; }
    int frameAt(int x) const;
    void pick(QPoint pos, bool press);
    void pasteBlock(CellBlock const& block, int frame);
    bool pressSwitch(QPoint pos);
    // Alt-drag in a drawing layer's cells: the exposure under the pointer made longer or shorter to where it is let go.
    bool beginExposureDrag(QPoint pos);
    void moveExposureDrag(QPoint pos);
    void endExposureDrag();
    int m_exposureLayer = -1;       // the layer whose exposure is dragged
    int m_exposureStart = 0, m_exposureEnd = 0;   // the exposure, as it was (its last frame + 1)
    QVector<int> m_exposureCells;   // the layer's cells before the drag
    std::shared_ptr<Scene> m_exposureBefore;
    bool m_exposureBegun = false;
    bool m_overwriteDrop = true;
    bool m_dragGap = true, m_dragToKey = false;
    // Before cells land on these layers ending before `end`: how far on each the exposure they cut into goes (-1: none is cut).
    QVector<int> exposuresCut(QVector<int> const& layers, int end) const;
    // ... and after: the last drawing put down holds on to there.
    void holdOverCut(QVector<int> const& layers, QVector<int> const& cut, int end);
    // Cells dragged out of [first, last]: what they left empty holds the drawing before.
    void fillGap(QVector<int> const& layers, int first, int last);
    bool m_showSceneEnd = true;
    bool m_reduceIndent = false;

    static constexpr int kHeaderWidth = 460;   // switches, name, parameters
    static constexpr int kSwitchWidth = 16;
    static constexpr int kSwitches = 5 * kSwitchWidth + 6;   // where the names start
    static constexpr int kParamWidth = 140;
    static constexpr int kRulerHeight = 20;
    static constexpr int kRowHeight = 20;
    static constexpr int kThumbRowHeight = 48;  // a row showing thumbnails
    static constexpr int kPastEnd = 40;        // frames of ruler shown after the scene ends

    enum Drop { DropNone, DropOnto, DropAbove, DropBelow };
    Drop dropAt(QPoint pos, int& targetLayer) const;

    Document* m_document;
    // A row each: the layer, and for the row of a parameter which one (-1: the layer itself).
    QVector<int> m_rows;
    ViewMode m_viewMode = ShowAll;
    QVector<int> m_hiddenKinds;
    QVector<int> m_shownIds;     // ShowSelected: the ids of the layers that were selected
    QVector<int> m_params;
    bool m_thumbPattern = false;
    mutable QHash<quint64, QPixmap> m_thumbnails;   // the pictures of the thumbnails, made again when the scene changes
    QVector<int> m_rowTops;     // where each row starts, and after the last where they end
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
    bool m_rulerLocked = false;
    bool m_hasPreset = false;
    PasteOptions m_preset;    // Lock Timeline Ruler: the end of the scene stays where it is
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
