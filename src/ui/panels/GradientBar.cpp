// Eclipse - the Colour view: the bar of the gradient editor.
#include "ui/panels/GradientBar.h"

#include <algorithm>
#include <cmath>
#include <QMouseEvent>
#include <QPainter>

#include "model/document/Commands.h"

GradientBar::GradientBar(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 48);
    m_stops = {{0.0, QColor(Qt::black)}, {1.0, QColor(Qt::white)}};
}

void GradientBar::setStops(QVector<GradientStop> const& stops)
{
    m_stops = stops;
    std::stable_sort(m_stops.begin(), m_stops.end(), [](GradientStop const& a, GradientStop const& b) { return a.at < b.at; });
    if (m_stops.size() < 2)
        m_stops = {{0.0, m_stops.isEmpty() ? QColor(Qt::black) : m_stops[0].color}, {1.0, QColor(Qt::white)}};
    m_selected = std::clamp(m_selected, 0, int(m_stops.size()) - 1);
    update();
    emit changed();
}

void GradientBar::setSelected(int index)
{
    m_selected = std::clamp(index, 0, int(m_stops.size()) - 1);
    update();
    emit changed();
}

void GradientBar::setSelectedColour(QColor const& colour)
{
    if (!colour.isValid() || m_selected < 0 || m_selected >= m_stops.size())
        return;
    m_stops[m_selected].color = colour;
    update();
    emit changed();
}

bool GradientBar::removeSelected()
{
    if (m_stops.size() <= 2 || m_selected < 0 || m_selected >= m_stops.size())
        return false;
    m_stops.removeAt(m_selected);
    m_selected = std::min(m_selected, int(m_stops.size()) - 1);
    update();
    emit changed();
    return true;
}

QRectF GradientBar::bar() const
{
    return QRectF(10, 4, std::max(20, width() - 20), std::max(10, height() - 24));
}

QPointF GradientBar::pointAt(double at) const
{
    QRectF const r = bar();
    return QPointF(r.left() + std::clamp(at, 0.0, 1.0) * r.width(), r.center().y());
}

int GradientBar::markerAt(QPointF pos) const
{
    QRectF const r = bar();
    int found = -1;
    double nearest = 8.0;
    for (int i = 0; i < m_stops.size(); ++i) {
        double const d = std::abs(pos.x() - (r.left() + m_stops[i].at * r.width()));
        // Of two on one spot, the one chosen is the one meant.
        if (d < nearest || (d <= nearest && i == m_selected && d < 8.0)) {
            nearest = d;
            found = i;
        }
    }
    return pos.y() >= r.bottom() - 4 ? found : -1;
}

void GradientBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QRectF const r = bar();
    // A checkerboard shows through colours that are not solid.
    for (int y = int(r.top()); y < r.bottom(); y += 6)
        for (int x = int(r.left()); x < r.right(); x += 6)
            p.fillRect(QRectF(x, y, std::min(6.0, r.right() - x), std::min(6.0, r.bottom() - y)), ((x / 6 + y / 6) % 2) ? QColor(200, 200, 200) : QColor(255, 255, 255));
    Swatch shown;
    shown.kind = Swatch::Linear;
    shown.stops = m_stops;
    p.fillRect(r, shown.brush(QPointF(r.left(), 0), QPointF(r.right(), 0)));
    p.setPen(QColor(20, 20, 20));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_stops.size(); ++i) {
        double const x = r.left() + m_stops[i].at * r.width();
        QPolygonF marker;
        marker << QPointF(x, r.bottom() + 1) << QPointF(x + 6, r.bottom() + 8) << QPointF(x + 6, r.bottom() + 17) << QPointF(x - 6, r.bottom() + 17) << QPointF(x - 6, r.bottom() + 8);
        p.setPen(QPen(i == m_selected ? QColor(255, 190, 70) : QColor(20, 20, 20), i == m_selected ? 2.0 : 1.0));
        p.setBrush(QColor(m_stops[i].color.red(), m_stops[i].color.green(), m_stops[i].color.blue()));
        p.drawPolygon(marker);
    }
}

void GradientBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    int const marker = markerAt(event->position());
    if (marker < 0)
        return;
    m_selected = marker;
    m_dragging = true;
    update();
    emit changed();
}

void GradientBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging || m_selected < 0 || m_selected >= m_stops.size())
        return;
    QRectF const r = bar();
    // Between its neighbours: the colours keep their order.
    double const low = m_selected > 0 ? m_stops[m_selected - 1].at : 0.0;
    double const high = m_selected + 1 < m_stops.size() ? m_stops[m_selected + 1].at : 1.0;
    m_stops[m_selected].at = std::clamp((event->position().x() - r.left()) / r.width(), low, high);
    update();
    emit changed();
}

void GradientBar::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

void GradientBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    QRectF const r = bar();
    if (event->button() != Qt::LeftButton || !r.adjusted(-2, 0, 2, 0).contains(event->position()))
        return;
    // A new colour there: the one the gradient has at that place, so that nothing changes until it is changed.
    double const at = std::clamp((event->position().x() - r.left()) / r.width(), 0.0, 1.0);
    GradientStop stop{at, Swatch::colourAt(m_stops, at)};
    int place = 0;
    while (place < m_stops.size() && m_stops[place].at <= at)
        ++place;
    m_stops.insert(place, stop);
    m_selected = place;
    update();
    emit changed();
}
