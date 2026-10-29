// Eclipse - the Top and Side views: the scene seen from above or from the side, so that how
// far each layer stands from the camera can be seen and set.  A layer is a line at its
// depth, as wide (Top) or as tall (Side) as its drawing; the camera is a cone opening onto
// the scene.  Drag a layer, or the camera, to move it nearer or further.
#pragma once

#include "EclipseExport.h"

#include <QWidget>
#include <QHash>
#include <QImage>
#include <algorithm>

class Document;
class Canvas;

class ECLIPSE_UI_API DepthView : public QWidget {
    Q_OBJECT

public:
    enum Mode { Top, Side };
    DepthView(Document* document, Mode mode, QWidget* parent = nullptr);

    // Where the mark of a layer is in the view (its middle), and what depth a place stands for.
    QPointF layerPoint(int layer) const;
    double depthAt(QPointF widgetPos) const;
    double zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    void restoreView(double zoom, QPointF pan) { m_zoom = std::clamp(zoom, 0.2, 40.0); m_pan = pan; update(); }
    void resetView();
    void setPlanningPreferences(double defaultZoom, int thumbnailSize);
    int thumbnailSize() const { return m_thumbnailSize; }
    void setAnimationCanvas(Canvas* canvas);
    int currentFrame() const { return m_frame; }
    QSize sizeHint() const override { return QSize(320, 260); }

public slots:
    void setCurrentCell(int layer, int frame);
    void onActionZoomIn() { m_zoom = std::min(m_zoom * 1.25, 40.0); update(); }
    void onActionZoomOut() { m_zoom = std::max(m_zoom / 1.25, 0.2); update(); }
    void onActionResetZoom() { m_zoom = m_defaultZoom; update(); }
    void onActionResetPan() { m_pan = QPointF(); update(); }
    void onActionResetView() { resetView(); }
    void onActionFrameSelected();
    void setCameraCone(bool on) { m_cameraCone = on; update(); }
    bool cameraCone() const { return m_cameraCone; }

signals:
    void layerPicked(int layer);
    void message(QString const& text);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

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
    QRectF thumbnailRect(QPointF a, QPointF b) const;
    QImage thumbnail(int layer);
    void paintMotionPath(QPainter& painter, double back, double front) const;
    bool beginMotionControlDrag(QPointF at);
    void motionControlDragTarget(QPointF& position, double& depth) const;

    Document* m_document;
    Canvas* m_canvas = nullptr;
    Mode m_mode;
    double m_zoom = 1.0;
    double m_defaultZoom = 1.0;
    int m_thumbnailSize = 64;
    QHash<int, QImage> m_thumbnails;
    QPointF m_pan;
    bool m_panning = false;
    bool m_cameraCone = true;
    QPointF m_last;
    int m_layer = 0;
    int m_frame = 0;
    int m_dragLayer = -1;
    double m_dragStart = 0.0;   // the depth under the pointer when the drag began
    double m_dragBy = 0.0;
    int m_dragControlKey = -1, m_dragControlIndex = -1;
    double m_dragControlDepth = 0.0, m_dragControlSlope = 0.0;
    QPointF m_dragControlStart, m_dragControlPosition, m_dragControlDelta;
    QPointF m_dragControlAxis, m_dragControlDepthAxis, m_dragNoZCoefficients;
    bool m_dragControlNoZ = false;
    int m_dragControlCoordinate = -1, m_dragControlSecond = 2;
    // The range is held while dragging, so that the view does not slide under the pointer.
    bool m_held = false;
    double m_heldBack = 0.0;
    double m_heldFront = 0.0;
};
