// Eclipse - the Function view: the animated values of layers as curves over the frames, their points and handles.
#pragma once

#include "EclipseExport.h"

#include <QPointer>
#include <QWidget>

#include "actions/manager/ActionManager.h"
#include "model/scene/Keyframe.h"
#include "ui/timeline/Timeline.h"

class Document;
struct Layer;
class QDoubleSpinBox;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// A point of a curve: a value (Timeline::Parameter) of a layer on a frame where it has a keyframe.
struct FunctionPoint {
    int layer = 0;
    int value = 0;
    int frame = 0;
    bool operator==(FunctionPoint const&) const = default;
};

// The graph itself.
class ECLIPSE_UI_API FunctionGraph : public QWidget {
    Q_OBJECT

public:
    explicit FunctionGraph(Document* document, QWidget* parent = nullptr);

    void setCurrentCell(int layer, int frame);
    int currentLayer() const { return m_layer; }
    int currentFrame() const { return m_frame; }
    void setShown(Timeline::Parameter which, bool shown);
    bool isShown(Timeline::Parameter which) const { return m_shown[which]; }
    // Preferences: the lines across at round values and down every so many frames (the numbers stay).
    void setGridShown(bool on);
    bool gridShown() const { return m_grid; }
    void setCurrentFrameShown(bool on);
    // Preferences: how many curves are drawn at most.
    void setLoadLimit(int curves);
    int loadLimit() const { return m_loadLimit; }
    bool currentFrameShown() const { return m_currentShown; }
    // The other layers whose curves are drawn with the current one's (fainter); and the ones of them hidden.
    void setOtherLayers(QVector<int> const& layers);
    QVector<int> otherLayers() const { return m_others; }
    void setLayerHidden(int layer, bool hidden);
    // Function columns drawn as well (places in Scene::functions); in the graph each is the layer -1 - its place.
    void setColumns(QVector<int> const& columns);
    QVector<int> columns() const { return m_columnsShown; }
    QVector<int> layersDrawn() const;
    // Where a value's points are: the layer itself, or the bezier or ease column it is linked to (-1 - its place, pos x).
    QPair<int, int> sourceOf(int layer, int value) const;
    static QColor colourOf(Timeline::Parameter which);
    // What a value is shown in: opacity in percent, the rest as they are.
    static double unitOf(int value) { return value == Timeline::Opacity ? 100.0 : 1.0; }
    // Where the keys of the Function Editor commands are kept (Preferences > Keyboard Shortcuts).
    void setActions(ActionManager* actions) { m_actions = actions; }

    // The value a curve has on a frame, as it is drawn (opacity as a percentage).
    double valueAt(Timeline::Parameter which, int frame) const;
    double valueOf(int layer, int value, double frame) const;
    // Where that is in the widget; and the value a height of the widget stands for.
    QPointF pointAt(Timeline::Parameter which, int frame) const;
    QPointF pointOf(FunctionPoint const& p) const;
    double valueOfY(double y) const;
    double frameOfXExact(double x) const;
    // The lowest and highest value the graph shows.
    double bottomValue() const { return m_low; }
    double topValue() const { return m_high; }
    double firstFrame() const { return m_first; }
    double frameSpan() const;
    bool zoomed() const { return m_zoomed || m_span > 0.0; }
    QJsonObject viewState() const;
    void restoreViewState(QJsonObject const& state);
    void resetView();
    void resetZoom();
    void resetPan();
    // Closer (factor below 1) or further, about the middle: along the frames, along the values.
    void zoomFrames(double factor);
    void zoomValues(double factor);

    // The points of the curves drawn; those selected, and the handle of the first of them that is (0: none, 1: left, 2: right).
    QVector<FunctionPoint> points() const;
    QVector<FunctionPoint> selection() const { return m_selection; }
    void setSelection(QVector<FunctionPoint> const& points);
    int handleSelected() const { return m_handle; }
    void selectHandle(int which);
    // Where a point's handle is drawn; null when its curve goes nowhere on that side.
    QPointF handleAt(FunctionPoint const& p, bool out) const;
    // The curve of a value through a point: its own, or as the ease makes it.
    ValueCurve curveAt(FunctionPoint const& p) const;

    // The commands.
    void selectAll();
    void deselectAll();
    void selectNeighbour(bool next);
    void copySelection();
    void cutSelection();
    void pasteAtCurrentFrame();
    void deleteSelection();
    void setSegment(int segment);       // ValueCurve::Segment, on from each point selected
    void setCorner(bool corner);
    void insertPoint();
    bool hasCopied() const { return !m_copied.isEmpty(); }
    // The point selected (or its handle): the frame (from 1) and value the boxes show, and those typed in them.
    bool selectedSpot(double& frame, double& value) const;
    void setSelectedSpot(double frame, double value);

signals:
    void framePicked(int frame);
    void selectionChanged();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QSize sizeHint() const override { return QSize(420, 220); }

private:
    void fit();
    void pruneSelection();
    double xOfFrame(double frame) const;
    double yOfValue(double value) const;
    int frameOfX(double x) const;
    QRectF plot() const;
    // The layer as it is, or as it would be with the drag under way.
    Layer const& layerOf(int layer) const;
    Layer const& sourceLayer(int layer) const;     // (without the drag)
    void makePreview();
    bool driven(int layer, int value) const;
    // The point under the pointer, or the handle (of a point selected): false for none.
    bool pointUnder(QPointF pos, FunctionPoint& p) const;
    bool handleUnder(QPointF pos, FunctionPoint& p, bool& out) const;
    // The command its key runs, or an empty string.
    QString commandOfKey(QKeyEvent* event) const;
    void runCommand(QString const& id);
    // The selection moved: its points by so many frames, and by so much of the value shown.
    void commitMove(int by, double delta);
    // How far the selection may go along the frames: as far as the points either side of it that stay.
    void boundsOfSelection(int& low, int& high) const;
    void commitHandle();
    // The value's own curve at a point, with a handle pulled to `to` (a place in the graph) and the other turned with it.
    ValueCurve pulledCurve(FunctionPoint const& p, bool out, QPointF to, bool breaking) const;

    Document* m_document;
    QPointer<ActionManager> m_actions;
    int m_layer = 0;
    int m_frame = 0;
    QVector<int> m_others;
    QVector<int> m_hidden;
    QVector<int> m_columnsShown;
    // The position - across, down and in depth - to begin with.
    bool m_shown[Timeline::ParameterCount] = {true, true, false, false, false, false, true, false, false};
    double m_first = 0.0;       // the frame at the left edge
    double m_span = 0.0;        // how many frames across; 0: all of the scene
    bool m_zoomed = false;      // the values shown were chosen by hand: they are not fitted again
    double m_low = -1.0, m_high = 1.0;
    bool m_grid = true;
    bool m_currentShown = true;
    int m_loadLimit = 50;
    QVector<FunctionPoint> m_selection;
    int m_handle = 0;
    // What the mouse is doing.
    enum Drag { NoDrag, Sliding, Points, Handle, Box };
    Drag m_drag = NoDrag;
    QPointF m_pressAt, m_lastAt;
    bool m_moved = false;
    FunctionPoint m_grip;           // the point (or the point whose handle) taken hold of
    bool m_gripOut = false;
    int m_dragBy = 0;               // the frames the points would move by
    double m_dragDelta = 0.0;       // ... and the value they would gain (as shown)
    int m_byLow = 0, m_byHigh = 0;  // as far as the points either side of them
    ValueCurve m_dragCurve;         // the handle's curve as it would be
    QVector<Layer> m_preview;       // the layers drawn while dragging (empty: none)
    QVector<int> m_previewLayers;
    QRectF m_box;
    // Points cut or copied: each value, how many frames after the first, its value, its curve if it has its own.
    struct Copied {
        int value = 0;
        int offset = 0;
        double v = 0.0;
        bool hasCurve = false;
        ValueCurve curve;
    };
    QVector<Copied> m_copied;
};

class ECLIPSE_UI_API FunctionView : public QWidget {
    Q_OBJECT

public:
    explicit FunctionView(Document* document, QWidget* parent = nullptr);

    FunctionGraph* graph() const { return m_graph; }
    // Gives the strip over the graph its buttons, and the graph its keys.
    void setActions(ActionManager* actions);
    // Auto Load Selection: the list follows the layers selected (on to begin with).
    bool autoLoads() const { return m_autoLoad; }
    // The layers loaded (ids, the first the one edited), and the function columns loaded by themselves (ids).
    QVector<int> loadedLayers() const { return m_loaded; }
    QVector<int> loadedColumns() const { return m_loadedColumns; }
    void loadColumn(int id);
    // Send to Function View: a value of a layer (its column, when it is linked to one) shown, its points selected.
    void sendToFunctionView(int layer, int value);
    // The item of a value of a layer loaded in the list; null when it is not there.
    QTreeWidgetItem* valueItem(int layer, Timeline::Parameter which) const;

public slots:
    void setCurrentCell(int layer, int frame);
    void setOtherLayers(QVector<int> const& layers);
    // The menu commands (the view's menu and the graph's).
    void onActionCut() { m_graph->cutSelection(); }
    void onActionCopy() { m_graph->copySelection(); }
    void onActionPaste() { m_graph->pasteAtCurrentFrame(); }
    void onActionDelete() { m_graph->deleteSelection(); }
    void onActionSelectLeftHandle() { m_graph->selectHandle(1); }
    void onActionSelectRightHandle() { m_graph->selectHandle(2); }
    void onActionSelectNextKey() { m_graph->selectNeighbour(true); }
    void onActionSelectPreviousKey() { m_graph->selectNeighbour(false); }
    void onActionSelectAll() { m_graph->selectAll(); }
    void onActionDeselectAll() { m_graph->deselectAll(); }
    void onActionShowCurrentFrame(bool on) { m_graph->setCurrentFrameShown(on); }
    void onActionToggleGrid() { m_graph->setGridShown(!m_graph->gridShown()); }
    void onActionResetZoom() { m_graph->resetZoom(); }
    void onActionResetPan() { m_graph->resetPan(); }
    void onActionResetView() { m_graph->resetView(); }
    void onActionZoomIn() { m_graph->zoomFrames(1.0 / 1.25), m_graph->zoomValues(1.0 / 1.25); }
    void onActionZoomOut() { m_graph->zoomFrames(1.25), m_graph->zoomValues(1.25); }
    void onActionZoomInX() { m_graph->zoomFrames(1.0 / 1.25); }
    void onActionZoomOutX() { m_graph->zoomFrames(1.25); }
    void onActionZoomInY() { m_graph->zoomValues(1.0 / 1.25); }
    void onActionZoomOutY() { m_graph->zoomValues(1.25); }
    void onActionStepped() { m_graph->setSegment(ValueCurve::Held); }
    void onActionStraight() { m_graph->setSegment(ValueCurve::Straight); }
    void onActionCurved() { m_graph->setSegment(ValueCurve::Curved); }
    void onActionSmooth() { m_graph->setCorner(false); }
    void onActionCorner() { m_graph->setCorner(true); }
    void onActionInsertPoint() { m_graph->insertPoint(); }
    void onActionAutoLoad(bool on);
    void onActionClearList();
    void onActionLoadSelection();
    void onActionAddSelection();
    void onActionRemoveSelection();
    void onActionShowKind(QString const& kind);

signals:
    void framePicked(int frame);

private:
    void rebuildList();
    void loadIntoGraph();
    void showSpot();
    void syncKinds();
    QVector<int> selectedIds() const;

    Document* m_document;
    FunctionGraph* m_graph;
    QTreeWidget* m_list;
    QDoubleSpinBox* m_frameBox;
    QDoubleSpinBox* m_valueBox;
    QWidget* m_strip;
    QVector<QToolButton*> m_pointButtons;      // shown only while points are selected
    QVector<QToolButton*> m_kindButtons;
    QPointer<ActionManager> m_actions;
    bool m_autoLoad = true;
    QVector<int> m_loaded;                      // ids
    QVector<int> m_loadedColumns;               // ids
    int m_current = 0;                          // the layer and the layers the Timeline has selected (places)
    QVector<int> m_timelineSelection;
    bool m_filling = false;
};
