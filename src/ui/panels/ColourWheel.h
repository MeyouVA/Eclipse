// Eclipse - the colour wheel of the Colour Editor: a disc (hue round it, saturation out from the middle) or,
// as the multiwheel, a hue ring round a square of saturation and value.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QWidget>

class ECLIPSE_UI_API ColourWheel : public QWidget {
    Q_OBJECT

public:
    explicit ColourWheel(QWidget* parent = nullptr);

    QColor colour() const { return m_colour; }
    // Shown without telling anyone.
    void setColour(QColor const& colour);
    bool multiwheel() const { return m_multi; }
    void setMultiwheel(bool on);
    // Where on the widget a colour would be picked (for tests): its hue and saturation on the disc, or its saturation and value
    // in the square of the multiwheel.
    QPointF pointOf(QColor const& colour) const;
    QSize sizeHint() const override { return QSize(180, 180); }

signals:
    void colourChanged(QColor const& colour);
    void editingFinished();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum Part { None, Disc, Ring, Square };
    QPointF middle() const;
    double radius() const;
    QRectF square() const;
    Part partAt(QPointF p) const;
    void pick(QPointF p);
    QColor m_colour = Qt::black;
    bool m_multi = false;
    Part m_dragging = None;
    // The hue of the colour shown, kept while saturation or value is 0 (where the colour itself has none).
    double m_hue = 0.0;
};
