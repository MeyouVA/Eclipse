// Eclipse - the velocity curve of a morphing: how far along the change is as its time goes by, drawn and pulled about.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include <functional>

#include "model/scene/Scene.h"

class ECLIPSE_UI_API MorphVelocityGraph : public QWidget {
    Q_OBJECT

public:
    explicit MorphVelocityGraph(Morph const& morph, QWidget* parent = nullptr);

    // The points of the curve (time, how far along), in order of time; none: the eases say how it goes.
    QVector<QPointF> points() const { return m_morph.velocity; }
    void setPoints(QVector<QPointF> const& points);
    // The eases the curve shows while it has no points.
    void setEases(double easeIn, double easeOut);
    // Where on the widget a place of the curve (time, how far along) is drawn, and back.
    QPointF toWidget(QPointF curve) const;
    QPointF toCurve(QPointF widget) const;
    // Called when the points change.
    std::function<void()> changed;

    QSize sizeHint() const override { return QSize(320, 200); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF area() const;
    int pointAt(QPointF widget) const;
    void put(int index, QPointF curve);
    Morph m_morph;
    int m_dragging = -1;
};
