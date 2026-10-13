// Eclipse - the Top and Side views: the scene seen from above or from the side, so that how
// far each layer stands from the camera can be seen and set.  A layer is a line at its
// depth, as wide (Top) or as tall (Side) as its drawing; the camera is a cone opening onto
// the scene.  Drag a layer, or the camera, to move it nearer or further.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;

class ECLIPSE_UI_API DepthView : public QWidget {
    Q_OBJECT

public:
    enum Mode { Top, Side };
    DepthView(Document* document, Mode mode, QWidget* parent = nullptr);

    // Where the mark of a layer is in the view (its middle), and what depth a place stands for.
    QPointF layerPoint(int layer) const;
    double depthAt(QPointF widgetPos) const;
    // Roll the wheel to come closer, drag with the middle or right button to slide the
    // view, double-click an empty place to see everything again.
    double zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    void resetView();
    int currentFrame() const { return m_frame; }
    QSize sizeHint() const override { return QSize(320, 260); }

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void layerPicked(int layer);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // What a layer is in this view: where it stands and how far it reaches across.
    struct Mark {
        int layer = -1;
        double depth = 0.0;
        double depthFrom = 0.0;     // at its two ends: they differ when the layer is turned in space
        double depthTo = 0.0;
        double from = 0.0;      // across the view: x for Top, y for Side - in the coordinates of the picture
        double to = 0.0;
        bool camera = false;
    };
    QVector<Mark> marks() const;
    // The range of depths shown, from the furthest to the nearest.
    void depthRange(QVector<Mark> const& marks, double& back, double& front) const;
    QPointF place(double across, double depth, double back, double front) const;
    double depthPerPixel() const;
    double shownDepth(Mark const& mark) const;

    // The place in the view as it is fitted, before zoom and pan.
    QPointF fitted(QPointF widgetPos) const;
    int markAt(QPointF widgetPos) const;

    Document* m_document;
    Mode m_mode;
    double m_zoom = 1.0;
    QPointF m_pan;
    bool m_panning = false;
    QPointF m_last;
    int m_layer = 0;
    int m_frame = 0;
    int m_dragLayer = -1;
    double m_dragStart = 0.0;   // the depth under the pointer when the drag began
    double m_dragBy = 0.0;
    // The range is held while dragging, so that the view does not slide under the pointer.
    bool m_held = false;
    double m_heldBack = 0.0;
    double m_heldFront = 0.0;
};
