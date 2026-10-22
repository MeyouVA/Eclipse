// Eclipse - the bar a gradient's colours are set out along, in the Colour view's gradient editor.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include "model/scene/Scene.h"

class ECLIPSE_UI_API GradientBar : public QWidget {
    Q_OBJECT

public:
    explicit GradientBar(QWidget* parent = nullptr);
    QVector<GradientStop> const& stops() const { return m_stops; }
    void setStops(QVector<GradientStop> const& stops);
    int selected() const { return m_selected; }
    void setSelected(int index);
    void setSelectedColour(QColor const& colour);
    // Takes the chosen colour out (a gradient keeps two at least).
    bool removeSelected();
    // Where a place along the gradient (0..1) is in the widget, on the bar.
    QPointF pointAt(double at) const;
    QSize sizeHint() const override { return QSize(320, 54); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    QRectF bar() const;
    int markerAt(QPointF pos) const;
    QVector<GradientStop> m_stops;
    int m_selected = 0;
    bool m_dragging = false;
};
