// Eclipse - the Perspective view: the scene as a stack of sheets in space, which can be
// turned about to see how the layers stand before the camera.  Drag to turn it, drag with
// the middle or right button to slide it, roll the wheel to come closer; a double click
// puts it back.  A click on a layer picks it.
#pragma once

#include "EclipseExport.h"

#include <QWidget>
#include <QJsonObject>
#include <QPointer>
#include <algorithm>

class Document;
class Canvas;
struct Drawing;

class ECLIPSE_UI_API PerspectiveView : public QWidget {
    Q_OBJECT

public:
    explicit PerspectiveView(Document* document, QWidget* parent = nullptr);

    double yaw() const { return m_yaw; }
    double pitch() const { return m_pitch; }
    double zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    void restoreView(double zoom, QPointF pan) { m_zoom = std::clamp(zoom, 0.1, 20.0); m_pan = pan; update(); }
    void setOrbit(double yaw, double pitch);
    void resetView();
    void setDrawingCanvas(Canvas* canvas);
    QJsonObject viewState() const;
    void restoreViewState(QJsonObject const& state);
    bool setClippingPlanes(double nearDistance, double farDistance);
    double nearDistance() const { return m_nearDistance; }
    double farDistance() const { return m_farDistance; }
    int currentFrame() const { return m_frame; }
    // Where a point of space is in the view; `seen` is told whether it is before the eye.
    QPointF project(double x, double y, double z, bool* seen = nullptr) const;
    // Where the pivot of a layer is in the view.
    QPointF layerPoint(int layer) const;
    QSize sizeHint() const override { return QSize(420, 320); }

public slots:
    void setCurrentCell(int layer, int frame);
    void onActionZoomIn() { m_zoom = std::min(m_zoom * 1.25, 20.0); update(); }
    void onActionZoomOut() { m_zoom = std::max(m_zoom / 1.25, 0.1); update(); }
    void onActionResetZoom() { m_zoom = 1.0; update(); }
    void onActionResetPan();
    void onActionResetView() { resetView(); }
    void onActionFrameSelected();
    void onActionLookAtSelected();
    void onActionFaceSelectedToDraw();
    void onActionMoveForward();
    void onActionMoveBackward();
    void onActionClippingPlanes();
    void setCameraCone(bool on) { m_cameraCone = on; update(); }
    bool cameraCone() const { return m_cameraCone; }

signals:
    void layerPicked(int layer);
    void message(QString const& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Point { double x, y, z; };
    Point viewPoint(Point point) const;
    QPointF projectView(Point point) const;
    QPolygonF projectPolygon(QVector<Point> polygon) const;
    bool projectSegment(Point a, Point b, QLineF& line) const;
    QPolygonF layerPolygon(int layer, QTransform* projection = nullptr, Drawing* displayed = nullptr) const;
    QTransform layerProjection(int layer) const;
    QPolygonF editingPolygon() const;
    bool canEditPlane() const;
    bool drawingTool() const;
    bool preparePlaneEditor();
    void cancelPlaneDrawing();
    // How far from the eye a point is (bigger: further off), to draw the furthest first.
    double distanceOf(double x, double y, double z) const;
    int layerAt(QPointF widgetPos) const;
    void moveView(double by);
    void paintMotionPath(QPainter& painter) const;

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    double m_yaw = -32.0;
    double m_pitch = 18.0;
    double m_zoom = 1.0;
    QPointF m_pan;
    double m_viewX = 0.0;
    double m_viewY = 0.0;
    double m_viewZ = 0.0;
    double m_nearDistance = 1.0;
    double m_farDistance = 100000.0;
    enum Drag { None, Orbit, Pan } m_drag = None;
    QPointF m_last;
    QPointF m_pressed;
    bool m_moved = false;
    bool m_cameraCone = true;
    QPointer<Canvas> m_drawingCanvas;
    Canvas* m_planeEditor = nullptr;
    int m_facingLayer = -1;
    bool m_drawingGesture = false;
    bool m_spaceDown = false;
};
