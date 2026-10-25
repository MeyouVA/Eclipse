// Eclipse - the Node View: the scene as a graph.  Every layer is a node; a cable runs from
// a layer to each layer attached to it, and from every drawing into the composite that
// stacks them into the picture.
//
// Drag a node to arrange the graph, drag from the port under a node onto another node to
// attach that one to it, click a cable and press Delete to detach.
#pragma once

#include "EclipseExport.h"

#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include <QJsonObject>
#include <algorithm>

class Document;
class QLineEdit;
class QMenu;
struct GraphNode;

class ECLIPSE_UI_API NodeView : public QWidget {
    Q_OBJECT

public:
    explicit NodeView(Document* document, QWidget* parent = nullptr);

    // Where the node of a layer is in the widget, and the port under it cables leave from.
    QRectF nodeRect(int layer) const;
    QPointF outPort(int layer) const;
    // The middle of the cable into `child` from its parent (for picking it).
    QPointF cableMiddle(int child) const;
    int find(QString const& text);
    QRectF navigatorRect() const;
    int enteredGroup() const;
    void enterGroup(int layer);
    void leaveGroup();
    // Whether the node of a layer is on show at the level the view is at.
    bool nodeShown(int layer) const;
    // The path along the top of the view while inside a group; a click on it goes out one level.
    QRectF pathRect() const;
    QString pathText() const;
    QRectF groupInputRect() const;
    QRectF groupOutputRect() const;
    // The layers the input node is cabled to: those that hang from the group itself.
    QVector<int> groupInputs() const;
    QPointF navigatorPoint(QPointF graph) const;
    // Where a place of the view is in the graph.
    QPointF graphPoint(QPointF widgetPos) const { return toGraph(widgetPos); }
    double zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    void restoreView(double zoom, QPointF pan) { m_zoom = std::clamp(zoom, 0.1, 4.0); m_pan = pan; m_centred = true; update(); }
    QJsonObject viewState() const;
    void restoreViewState(QJsonObject const& state);
    QVector<int> pickedLayers() const;
    void setNavigatorCorner(int corner);
    int navigatorCorner() const { return m_navigatorCorner; }
    // Larger or smaller about the middle of the view.
    void zoomBy(double factor);
    // Preferences: the small map in the corner shown or not; cables drawn straight from port to port, or as curves.
    void setNavigatorShown(bool on);
    bool navigatorShown() const { return m_navigator; }
    void setStraightCables(bool on);
    bool straightCables() const { return m_straight; }
    // Preferences: a double-click on a layer's node opens its properties (editRequested) instead of renaming it.
    void setDoubleClickOpensEditor(bool on) { m_doubleClickEdits = on; }
    bool doubleClickOpensEditor() const { return m_doubleClickEdits; }

    // ---- the node graph, wired by hand (see NodeGraph) ----
    // Whether the scene's graph is wired by hand: the view then shows it and edits it (otherwise it shows the layers as
    // they stack).
    bool graphMode() const;
    // Where a node of the graph is (by its id) and its ports, in the widget; and the node under a place (0: none).
    QRectF graphNodeRect(int id) const;
    QPointF graphInPort(int id, int port) const;
    QPointF graphOutPort(int id, int port) const;
    int graphNodeAt(QPointF widgetPos) const;
    // The group of the graph the view is inside (its id; 0 at the top), gone into and out of.
    int graphGroup() const { return m_graphGroup; }
    void enterGraphGroup(int id);
    // The node and the cable picked (an id; a place among the cables), or 0 and -1.
    int pickedGraphNode() const { return m_graphNode; }
    int pickedGraphCable() const { return m_graphCable; }
    void pickGraphNode(int id);
    void pickGraphCable(int index);
    int addGraphNode(GraphNode node, QPointF graphPos);
    bool connectGraph(int from, int fromPort, int to, int toPort);
    void changeGraphPorts(int id, bool outputs, int by);
    QPointF graphTransformIn(int id) const;
    QPointF graphTransformOut(int id) const;
    // The nodes picked together (Ctrl+click adds one or takes it away).
    QVector<int> pickedGraphNodes() const;
    void addPickedGraphNode(int id);
    int groupGraphNodes(QVector<int> const& ids);

public slots:
    void onActionNavigator(QString const& position);
    void onActionCableStyle(QString const& style);
    void selectAll();
    void deselectAll();
    void onActionInvertSelection();
    void onActionCentreOnSelection();
    void setCurrentCell(int layer, int frame);
    void deleteSelection();
    void onActionResetView();
    // The group that is the current layer is gone into; and out again, one level.
    void onActionEnterGroup();
    void onActionExitGroup() { leaveGroup(); }
    // View > Zoom In and the rest, with this view in use.
    void onActionZoomIn() { zoomBy(1.25); }
    void onActionZoomOut() { zoomBy(0.8); }
    void onActionResetZoom() { zoomBy(1.0 / m_zoom); }
    void onActionResetPan();
    // The graph made from the layers as they stack, to wire by hand from; and let go again (the layers stack as they are).
    void onActionWireGraph();
    void onActionStackLayers();

signals:
    void layerPicked(int layer);
    void renameRequested(int layer);
    // A double-click on a layer's node, when it is to open the layer's properties (setDoubleClickOpensEditor).
    void editRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);
    void nodeDropped(QString const& what, QPointF graphPos);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
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
    QSize sizeHint() const override { return QSize(360, 300); }

private:
    static constexpr double kNodeWidth = 124.0;
    static constexpr double kNodeHeight = 30.0;

    QVector<QPointF> positions() const;
    QPointF compositePos(QVector<QPointF> const& positions) const;
    QPointF toWidget(QPointF graph) const { return graph * m_zoom + m_pan; }
    QPointF toGraph(QPointF widget) const { return (widget - m_pan) / m_zoom; }
    int nodeAt(QPointF widgetPos) const;
    QRectF graphBounds() const;
    int cableAt(QPointF widgetPos) const;
    QVector<QPointF> cablePoints(QPointF from, QPointF to) const;
    bool m_navigator = true;
    bool m_straight = false;
    bool m_doubleClickEdits = false;
    int m_navigatorCorner = 2;
    bool m_spacePan = false;
    QVector<int> m_layerPicked;
    QVector<int> m_boxBefore;
    Qt::KeyboardModifiers m_boxModifiers = Qt::NoModifier;
    void startBoxSelection(QMouseEvent* event);
    void updateBoxSelection();
    void paintBoxSelection(QPainter& painter);
    void movePickedNodes(QPointF delta);

    Document* m_document;
    QLineEdit* m_search = nullptr;     // along the bottom: type a name, Enter goes to the next node of it
    int m_layer = 0;
    int m_frame = 0;
    int m_groupId = 0;      // the id of the group the view is inside; 0 at the top
    double m_zoom = 1.0;
    QPointF m_pan = QPointF(40, 150);
    bool m_centred = false;   // the graph is brought into view the first time it is shown

    enum Drag { DragNone, DragNode, DragCable, DragPan, DragWire, DragGraphNode, DragPeg, DragBox, DragDisconnect } m_drag = DragNone;

    // The graph, wired by hand: drawn and worked with in the widget.
    int m_graphGroup = 0;
    int m_graphNode = 0;
    int m_graphCable = -1;
    int m_wireFrom = 0, m_wireFromPort = 0;     // a cable being pulled out of this output
    bool m_wireReverse = false;
    int m_unplugged = -1;                       // a cable taken off an input to be plugged in elsewhere (its place)
    QVector<int> m_graphPicked;                 // the nodes picked together
    int m_pegFrom = 0, m_pegChild = 0;          // a parenting cable pulled out of this node (taken off this child)
    QRectF graphLevelBounds() const;
    struct GraphPort {
        int node = 0;
        int port = -1;
        bool out = false;
    };
    GraphPort graphPortAt(QPointF widgetPos) const;
    int graphCableAt(QPointF widgetPos) const;
    double graphNodeWidth(GraphNode const& node) const;
    bool pictureless(GraphNode const& node) const;
    QString graphNodeName(GraphNode const& node) const;
    QRectF graphPathRect() const;
    void paintGraphMode(QPainter& p);
    void graphPress(QMouseEvent* event);
    void graphMove(QMouseEvent* event);
    void graphRelease(QMouseEvent* event);
    void graphDoubleClick(QMouseEvent* event);
    void graphContextMenu(QPoint globalPos, QPointF widgetPos);
    void deleteGraphSelection();
    void editGraphNode(int id);
    void resetGraphView();
    void addNavigationMenu(QMenu& menu);
    int m_dragLayer = -1;
    QPointF m_pressPos;       // widget
    QPointF m_dragNow;        // widget
    QPointF m_nodeStart;      // graph position of the node when its drag began
    bool m_moved = false;
    int m_cable = -1;         // the picked cable: the layer it goes into
};
