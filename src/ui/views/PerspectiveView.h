// Eclipse - the Perspective view: the scene as a stack of sheets in space, which can be
// turned about to see how the layers stand before the camera.  Drag to turn it, drag with
// the middle or right button to slide it, roll the wheel to come closer; a double click
// puts it back.  A click on a layer picks it.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;

class ECLIPSE_UI_API PerspectiveView : public QWidget {
    Q_OBJECT

public:
    explicit PerspectiveView(Document* document, QWidget* parent = nullptr);

    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double zoom() const { return m_zoom; }
    void setOrbit(double yaw, double pitch);
    void resetView();
    // Where a point of space is in the view; `seen` is told whether it is before the eye.
    QPointF project(double x, double y, double z, bool* seen = nullptr) const;
    // Where the pivot of a layer is in the view.
    QPointF layerPoint(int layer) const;
    QSize sizeHint() const override { return QSize(420, 320); }

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
    // The four corners of what a layer shows on this frame, in the view; empty when it shows
    // nothing or some of it is behind the eye.  `box` is told the rectangle they are the corners of.
    QPolygonF layerQuad(int layer, QRectF* box = nullptr) const;
    // How far from the eye a point is (bigger: further off), to draw the furthest first.
    double distanceOf(double x, double y, double z) const;
    int layerAt(QPointF widgetPos) const;

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    double m_yaw = -32.0;
    double m_pitch = 18.0;
    double m_zoom = 1.0;
    QPointF m_pan;
    enum Drag { None, Orbit, Pan } m_drag = None;
    QPointF m_last;
    QPointF m_pressed;
    bool m_moved = false;
};
