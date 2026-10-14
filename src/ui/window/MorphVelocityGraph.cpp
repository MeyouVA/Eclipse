// Eclipse - the velocity curve of a morphing.
#include "ui/window/MorphVelocityGraph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {
// How far along the graph shows: a little past either drawing, as far as a point may go.
constexpr double kLow = -0.5;
constexpr double kHigh = 1.5;
}  // namespace

MorphVelocityGraph::MorphVelocityGraph(Morph const& morph, QWidget* parent) : QWidget(parent), m_morph(morph)
{
    setObjectName(QStringLiteral("MorphVelocityGraph"));
    setMinimumSize(240, 150);
    setToolTip(tr("Across: the time of the morphing.  Up: how far it has gone from the first drawing to the second."));
}

void MorphVelocityGraph::setPoints(QVector<QPointF> const& points)
{
    m_morph.velocity.clear();
    for (QPointF const& p : points)
        if (p.x() > 0.0 && p.x() < 1.0 && (m_morph.velocity.isEmpty() || p.x() > m_morph.velocity.last().x() + 1e-6))
            m_morph.velocity.push_back(QPointF(p.x(), std::clamp(p.y(), kLow, kHigh)));
    update();
    if (changed)
        changed();
}

void MorphVelocityGraph::setEases(double easeIn, double easeOut)
{
    m_morph.easeIn = easeIn;
    m_morph.easeOut = easeOut;
    update();
}

QRectF MorphVelocityGraph::area() const
{
    return QRectF(rect()).adjusted(10, 8, -10, -8);
}

QPointF MorphVelocityGraph::toWidget(QPointF curve) const
{
    QRectF const a = area();
    return QPointF(a.left() + curve.x() * a.width(), a.bottom() - (curve.y() - kLow) / (kHigh - kLow) * a.height());
}

QPointF MorphVelocityGraph::toCurve(QPointF widget) const
{
    QRectF const a = area();
    return QPointF((widget.x() - a.left()) / a.width(), kLow + (a.bottom() - widget.y()) / a.height() * (kHigh - kLow));
}

int MorphVelocityGraph::pointAt(QPointF widget) const
{
    for (int i = 0; i < m_morph.velocity.size(); ++i)
        if (QLineF(toWidget(m_morph.velocity[i]), widget).length() <= 6.0)
            return i;
    return -1;
}

void MorphVelocityGraph::put(int index, QPointF curve)
{
    // A point stays between its neighbours in time, inside the morphing.
    double const before = index > 0 ? m_morph.velocity[index - 1].x() + 0.01 : 0.01;
    double const after = index + 1 < m_morph.velocity.size() ? m_morph.velocity[index + 1].x() - 0.01 : 0.99;
    m_morph.velocity[index] = QPointF(std::clamp(curve.x(), before, std::max(before, after)), std::clamp(curve.y(), kLow, kHigh));
    update();
    if (changed)
        changed();
}

void MorphVelocityGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    QRectF const a = area();
    // The two drawings: how far along 0 and 1.
    p.setPen(QPen(palette().mid().color(), 1, Qt::DashLine));
    for (double level : {0.0, 1.0})
        p.drawLine(toWidget(QPointF(0.0, level)), toWidget(QPointF(1.0, level)));
    p.setPen(QPen(palette().mid().color(), 1));
    p.drawRect(a);
    // The curve, from the first drawing to the second.
    QPainterPath path(toWidget(QPointF(0.0, 0.0)));
    for (int i = 1; i <= 100; ++i) {
        double const t = i / 100.0;
        path.lineTo(toWidget(QPointF(t, m_morph.progress(t))));
    }
    p.setPen(QPen(QColor(90, 170, 240), 2));
    p.drawPath(path);
    p.setPen(QPen(palette().text().color(), 1));
    p.setBrush(QColor(240, 200, 80));
    for (QPointF const& v : m_morph.velocity)
        p.drawEllipse(toWidget(v), 4.0, 4.0);
}

void MorphVelocityGraph::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = pointAt(event->position());
    if (m_dragging >= 0)
        return;
    // A new point, where it was clicked (in its place in time).
    QPointF const at = toCurve(event->position());
    if (at.x() <= 0.0 || at.x() >= 1.0)
        return;
    int index = 0;
    while (index < m_morph.velocity.size() && m_morph.velocity[index].x() < at.x())
        ++index;
    if ((index > 0 && at.x() - m_morph.velocity[index - 1].x() < 0.01)
        || (index < m_morph.velocity.size() && m_morph.velocity[index].x() - at.x() < 0.01))
        return;
    m_morph.velocity.insert(index, QPointF(at.x(), std::clamp(at.y(), kLow, kHigh)));
    m_dragging = index;
    update();
    if (changed)
        changed();
}

void MorphVelocityGraph::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging >= 0 && m_dragging < m_morph.velocity.size())
        put(m_dragging, toCurve(event->position()));
}

void MorphVelocityGraph::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = -1;
}

void MorphVelocityGraph::mouseDoubleClickEvent(QMouseEvent* event)
{
    int const index = pointAt(event->position());
    if (index < 0)
        return;
    m_dragging = -1;
    m_morph.velocity.removeAt(index);
    update();
    if (changed)
        changed();
}
