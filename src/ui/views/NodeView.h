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

class Document;
class QLineEdit;

class ECLIPSE_UI_API NodeView : public QWidget {
    Q_OBJECT

public:
    explicit NodeView(Document* document, QWidget* parent = nullptr);

    // Where the node of a layer is in the widget, and the port under it cables leave from.
    QRectF nodeRect(int layer) const;
    QPointF outPort(int layer) const;
    // The middle of the cable into `child` from its parent (for picking it).
    QPointF cableMiddle(int child) const;
    // The next layer after the current one whose name has `text` in it (whatever its capitals):
    // it becomes the current layer and its node is brought to the middle of the view.  -1: none.
    int find(QString const& text);
    // The small map of the whole graph in the corner of the view: where it is, and where a place of
    // the graph is on it.  A click on the map brings that part of the graph to the middle of the view.
    QRectF navigatorRect() const;
    // Groups: at the top a group is one node and what is in it is not shown; gone into, the view shows what
    // is in it and nothing else.  The group the view is inside (its place among the layers), or -1 at the top.
    int enteredGroup() const;
    void enterGroup(int layer);
    void leaveGroup();
    // Whether the node of a layer is on show at the level the view is at.
    bool nodeShown(int layer) const;
    // The path along the top of the view while inside a group; a click on it goes out one level.
    QRectF pathRect() const;
    QString pathText() const;
    // Inside a group: the node that stands for what comes into it (its transformation, which what hangs from the group
    // takes), above the rest; and the one that stands for what goes out (its picture), under the composite.  Null at the top.
    QRectF groupInputRect() const;
    QRectF groupOutputRect() const;
    // The layers the input node is cabled to: those that hang from the group itself.
    QVector<int> groupInputs() const;
    QPointF navigatorPoint(QPointF graph) const;
    // Where a place of the view is in the graph.
    QPointF graphPoint(QPointF widgetPos) const { return toGraph(widgetPos); }
    double zoom() const { return m_zoom; }
    // Larger or smaller about the middle of the view.
    void zoomBy(double factor);

public slots:
    void setCurrentCell(int layer, int frame);
    // Edit > Delete with this view in focus: the picked cable (detaches), else the
    // current layer.
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

signals:
    void layerPicked(int layer);
    void renameRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);
    // Something of the Node Library was dropped here: what (as NodeLibraryView::add takes it), and where its
    // node is to stand in the graph.
    void nodeDropped(QString const& what, QPointF graphPos);

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
    QSize sizeHint() const override { return QSize(360, 300); }

private:
    static constexpr double kNodeWidth = 124.0;
    static constexpr double kNodeHeight = 30.0;

    // Top-left corner of every node, in graph coordinates: where the user put it, or a
    // tidy default for those never moved.
    QVector<QPointF> positions() const;
    QPointF compositePos(QVector<QPointF> const& positions) const;
    QPointF toWidget(QPointF graph) const { return graph * m_zoom + m_pan; }
    QPointF toGraph(QPointF widget) const { return (widget - m_pan) / m_zoom; }
    int nodeAt(QPointF widgetPos) const;
    QRectF graphBounds() const;
    int cableAt(QPointF widgetPos) const;
    static QVector<QPointF> cablePoints(QPointF from, QPointF to);

    Document* m_document;
    QLineEdit* m_search = nullptr;     // along the bottom: type a name, Enter goes to the next node of it
    int m_layer = 0;
    int m_groupId = 0;      // the id of the group the view is inside; 0 at the top
    double m_zoom = 1.0;
    QPointF m_pan = QPointF(40, 150);
    bool m_centred = false;   // the graph is brought into view the first time it is shown

    enum Drag { DragNone, DragNode, DragCable, DragPan } m_drag = DragNone;
    int m_dragLayer = -1;
    QPointF m_pressPos;       // widget
    QPointF m_dragNow;        // widget
    QPointF m_nodeStart;      // graph position of the node when its drag began
    bool m_moved = false;
    int m_cable = -1;         // the picked cable: the layer it goes into
};
