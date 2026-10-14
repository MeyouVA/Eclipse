// Eclipse - Pose Controllers: sets of layers with poses kept under names, and a slider that blends between them.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class Timeline;

// The pad of a 2D point controller: the states as dots on a square, and the point dragged among them.  Shift-drag
// a state to move it.
class ECLIPSE_UI_API ControllerPad : public QWidget {
    Q_OBJECT

public:
    explicit ControllerPad(QWidget* parent = nullptr);
    void setStates(QVector<QPointF> const& places, QStringList const& names);
    void setPoint(QPointF where);
    QPointF point() const { return m_where; }
    // Where a place on the pad (0..1 across and down) is in the widget; and back.
    QPointF toWidget(QPointF place) const;
    QPointF toPlace(QPointF widgetPos) const;
    QSize sizeHint() const override { return QSize(140, 140); }
    QSize minimumSizeHint() const override { return QSize(90, 90); }

signals:
    // The point let go there; a state dragged there.
    void pointSet(QPointF where);
    void stateMoved(int state, QPointF at);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF square() const;
    QVector<QPointF> m_places;
    QStringList m_names;
    QPointF m_where{0.5, 0.5};
    int m_dragState = -1;       // the state being moved, or -1
    bool m_dragging = false;
};

class ECLIPSE_UI_API ControllerView : public QWidget {
    Q_OBJECT

public:
    ControllerView(Document* document, Timeline* timeline, QWidget* parent = nullptr);

    // The controller chosen (its place in the scene's list), or -1.
    int currentController() const;
    // The slider moved to `value` (0: the first state, 1: the second ...): the controller is set on the current frame.
    void setValue(double value);
    // A 2D point controller: its point set to `where` on its pad, on the current frame.
    void setPoint(QPointF where);
    // The Camera view: the controller chosen can have its slider there too, in its lower left corner ("Show in Camera").
    void setCameraHost(QWidget* camera);
    void setShownInCamera(bool on);

public slots:
    void newController();
    void deleteController();
    void addState();
    void replaceState();
    void removeState();
    void refresh();

private:
    Document* m_document;
    Timeline* m_timeline;
    QListWidget* m_controllers;
    QListWidget* m_states;
    QSlider* m_slider;
    QLabel* m_value;
    bool m_refreshing = false;
    QWidget* m_camera = nullptr;
    class QFrame* m_overlay = nullptr;     // in the Camera view: the controller's name and a slider
    QLabel* m_overlayName = nullptr;
    QSlider* m_overlaySlider = nullptr;
    class QCheckBox* m_inCamera = nullptr;
    class QCheckBox* m_point = nullptr;     // a 2D point instead of a slider
    ControllerPad* m_pad = nullptr;
    ControllerPad* m_overlayPad = nullptr;
    void placeOverlay();
    bool eventFilter(QObject* watched, QEvent* event) override;
};
