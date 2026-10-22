// Eclipse - the colour wheel of the Colour Editor.
#include "ui/panels/ColourWheel.h"

#include <QConicalGradient>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double hueOf(QColor const& c, double kept)
{
    float h = 0, s = 0, v = 0;
    c.getHsvF(&h, &s, &v);
    return h < 0 || s <= 0.0f || v <= 0.0f ? kept : h;
}

}  // namespace

ColourWheel::ColourWheel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ColourWheel"));
    setMinimumSize(120, 120);
}

void ColourWheel::setColour(QColor const& colour)
{
    if (!colour.isValid())
        return;
    m_hue = hueOf(colour, m_hue);
    m_colour = colour;
    update();
}

void ColourWheel::setMultiwheel(bool on)
{
    if (on == m_multi)
        return;
    m_multi = on;
    update();
}

QPointF ColourWheel::middle() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

double ColourWheel::radius() const
{
    return std::max(10.0, std::min(width(), height()) / 2.0 - 4.0);
}

QRectF ColourWheel::square() const
{
    // Inside the ring: its corners on the ring's inner edge.
    double const inner = radius() * 0.78;
    double const half = inner / std::sqrt(2.0) - 2.0;
    return QRectF(middle() - QPointF(half, half), QSizeF(2 * half, 2 * half));
}

QPointF ColourWheel::pointOf(QColor const& colour) const
{
    float h = 0, s = 0, v = 0;
    colour.getHsvF(&h, &s, &v);
    if (h < 0)
        h = float(m_hue);
    if (m_multi) {
        QRectF const box = square();
        return QPointF(box.left() + s * box.width(), box.top() + (1.0 - v) * box.height());
    }
    double const angle = h * 2.0 * kPi;
    return middle() + QPointF(std::cos(angle), -std::sin(angle)) * s * radius();
}

ColourWheel::Part ColourWheel::partAt(QPointF p) const
{
    double const d = std::hypot(p.x() - middle().x(), p.y() - middle().y());
    if (!m_multi)
        return d <= radius() + 6 ? Disc : None;
    if (square().adjusted(-4, -4, 4, 4).contains(p))
        return Square;
    return d >= radius() * 0.78 - 4 && d <= radius() + 6 ? Ring : None;
}

void ColourWheel::pick(QPointF p)
{
    float h = 0, s = 0, v = 0, a = m_colour.alphaF();
    m_colour.getHsvF(&h, &s, &v, &a);
    h = float(m_hue);
    QPointF const off = p - middle();
    if (m_dragging == Disc || m_dragging == Ring) {
        double angle = std::atan2(-off.y(), off.x());
        if (angle < 0)
            angle += 2.0 * kPi;
        h = float(angle / (2.0 * kPi));
        m_hue = h;
        if (m_dragging == Disc)
            s = float(std::clamp(std::hypot(off.x(), off.y()) / radius(), 0.0, 1.0));
    } else if (m_dragging == Square) {
        QRectF const box = square();
        s = float(std::clamp((p.x() - box.left()) / box.width(), 0.0, 1.0));
        v = float(std::clamp(1.0 - (p.y() - box.top()) / box.height(), 0.0, 1.0));
    }
    m_colour = QColor::fromHsvF(std::clamp(h, 0.0f, 0.9999f), s, v, a);
    update();
    emit colourChanged(m_colour);
}

void ColourWheel::mousePressEvent(QMouseEvent* event)
{
    m_dragging = event->button() == Qt::LeftButton ? partAt(event->position()) : None;
    if (m_dragging != None)
        pick(event->position());
}

void ColourWheel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging != None)
        pick(event->position());
}

void ColourWheel::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging == None)
        return;
    m_dragging = None;
    emit editingFinished();
}

void ColourWheel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPointF const mid = middle();
    double const r = radius();
    float h = 0, s = 0, v = 0;
    m_colour.getHsvF(&h, &s, &v);
    QConicalGradient hues(mid, 0);
    for (int i = 0; i <= 6; ++i)
        hues.setColorAt(i / 6.0, QColor::fromHsvF(std::min(0.9999f, i / 6.0f), 1.0f, 1.0f));
    if (!m_multi) {
        // Hue round, saturation out from a white middle, darkened by the value.
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawEllipse(mid, r, r);
        QRadialGradient white(mid, r);
        white.setColorAt(0, Qt::white);
        white.setColorAt(1, QColor(255, 255, 255, 0));
        painter.setBrush(white);
        painter.drawEllipse(mid, r, r);
        painter.setBrush(QColor(0, 0, 0, int(std::round((1.0 - v) * 255))));
        painter.drawEllipse(mid, r, r);
    } else {
        // The ring of hues; inside it, saturation across and value up for the hue chosen.
        QPainterPath ring;
        ring.addEllipse(mid, r, r);
        ring.addEllipse(mid, r * 0.78, r * 0.78);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawPath(ring);
        QRectF const box = square();
        QLinearGradient across(box.topLeft(), box.topRight());
        across.setColorAt(0, Qt::white);
        across.setColorAt(1, QColor::fromHsvF(std::min(0.9999f, float(m_hue)), 1.0f, 1.0f));
        painter.fillRect(box, across);
        QLinearGradient down(box.topLeft(), box.bottomLeft());
        down.setColorAt(0, QColor(0, 0, 0, 0));
        down.setColorAt(1, Qt::black);
        painter.fillRect(box, down);
        // Where the hue is on the ring.
        double const angle = m_hue * 2.0 * kPi;
        QPointF const onRing = mid + QPointF(std::cos(angle), -std::sin(angle)) * (r * 0.89);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::black, 3));
        painter.drawEllipse(onRing, 5, 5);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(onRing, 5, 5);
    }
    // The colour's own place: a ring dark on light, light on dark.
    QPointF const at = pointOf(m_colour);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(v > 0.5f && s < 0.5f ? Qt::black : Qt::white, 1.5));
    painter.drawEllipse(at, 5, 5);
}
