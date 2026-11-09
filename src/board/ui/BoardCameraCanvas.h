#pragma once

#include "board/model/BoardCamera.h"
#include "canvas/Canvas.h"

Pose boardCameraLayerPose(BoardCameraPose const& pose);

class BoardCameraCanvas : public Canvas {
    Q_OBJECT
public:
    explicit BoardCameraCanvas(Document* document, QWidget* parent);
    void setCameraTool(bool on);
    void setCameraGuide(bool on);
    void setCameraFrame(QSize size, BoardCameraPose const& pose, bool hasKeys, int frame);
    bool cameraTool() const { return m_cameraTool; }

signals:
    void cameraEdited(BoardCameraPose const& pose);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    enum class Drag { None, Pan, Zoom, Rotate };
    QPolygonF framePolygon() const;
    QPointF rotationHandle(QPolygonF const& frame) const;
    void cancelDrag();
    BoardCameraPose m_pose;
    BoardCameraPose m_startPose;
    QSize m_frameSize;
    QPointF m_startMouse;
    int m_frame = 0;
    Drag m_drag = Drag::None;
    bool m_cameraTool = false;
    bool m_cameraGuide = true;
    bool m_hasKeys = false;
};
