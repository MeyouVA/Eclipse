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

public slots:
    void setCurrentCell(int layer, int frame);
    // Edit > Delete with this view in focus: the picked cable (detaches), else the
    // current layer.
    void deleteSelection();
    void onActionResetView();

signals:
    void layerPicked(int layer);
    void renameRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
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
    int cableAt(QPointF widgetPos) const;
    static QVector<QPointF> cablePoints(QPointF from, QPointF to);

    Document* m_document;
    QLineEdit* m_search = nullptr;     // along the bottom: type a name, Enter goes to the next node of it
    int m_layer = 0;
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
