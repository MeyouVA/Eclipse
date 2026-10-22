// Eclipse - the colour curve of the Colour Curves effect, its points dragged.
#include "ui/panels/EffectsPanel.h"

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QDir>
#include <QJSEngine>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QTemporaryDir>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include "model/document/Commands.h"

#include <algorithm>
#include <cmath>

ColourCurveEdit::ColourCurveEdit(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ColourCurveEdit"));
    setMinimumSize(120, 120);
    setPoints({});
}

void ColourCurveEdit::setPoints(QVector<QPointF> const& points)
{
    m_points = points;
    if (m_points.size() < 2)
        m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
    update();
}

QRectF ColourCurveEdit::box() const
{
    return QRectF(rect()).adjusted(6, 6, -6, -6);
}

QPointF ColourCurveEdit::toWidget(QPointF const& level) const
{
    QRectF const b = box();
    return QPointF(b.left() + level.x() * b.width(), b.bottom() - level.y() * b.height());
}

QPointF ColourCurveEdit::toLevel(QPointF const& at) const
{
    QRectF const b = box();
    return QPointF(std::clamp((at.x() - b.left()) / std::max(1.0, b.width()), 0.0, 1.0),
                   std::clamp((b.bottom() - at.y()) / std::max(1.0, b.height()), 0.0, 1.0));
}

int ColourCurveEdit::pointAt(QPointF const& at) const
{
    for (int k = 0; k < m_points.size(); ++k)
        if (QLineF(toWidget(m_points[k]), at).length() <= 6.0)
            return k;
    return -1;
}

void ColourCurveEdit::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF const b = box();
    p.fillRect(b, QColor(36, 36, 36));
    p.setPen(QColor(64, 64, 64));
    for (int k = 1; k < 4; ++k) {
        p.drawLine(QPointF(b.left() + b.width() * k / 4.0, b.top()), QPointF(b.left() + b.width() * k / 4.0, b.bottom()));
        p.drawLine(QPointF(b.left(), b.top() + b.height() * k / 4.0), QPointF(b.right(), b.top() + b.height() * k / 4.0));
    }
    p.setPen(QPen(QColor(90, 90, 90), 1.0, Qt::DashLine));
    p.drawLine(b.bottomLeft(), b.topRight());
    QVector<int> const levels = Effects::curveLevels(m_points);
    QPainterPath curve;
    for (int i = 0; i < 256; ++i) {
        QPointF const at = toWidget(QPointF(i / 255.0, levels[i] / 255.0));
        if (i == 0)
            curve.moveTo(at);
        else
            curve.lineTo(at);
    }
    p.setPen(QPen(QColor(230, 230, 230), 1.5));
    p.drawPath(curve);
    p.setBrush(QColor(255, 200, 80));
    p.setPen(Qt::black);
    for (QPointF const& point : m_points)
        p.drawEllipse(toWidget(point), 4.0, 4.0);
}

void ColourCurveEdit::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragged = pointAt(event->position());
    if (m_dragged < 0) {
        // A new point, where the click is (between the ends).
        QPointF const level = toLevel(event->position());
        int at = 0;
        while (at < m_points.size() && m_points[at].x() < level.x())
            ++at;
        if (at == 0 || at == m_points.size())
            return;
        m_points.insert(at, level);
        m_dragged = at;
    }
    update();
}

void ColourCurveEdit::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragged < 0)
        return;
    QPointF level = toLevel(event->position());
    // A point stays between the ones either side of it; the ends stay at the ends.
    if (m_dragged == 0)
        level.setX(0.0);
    else if (m_dragged == m_points.size() - 1)
        level.setX(1.0);
    else
        level.setX(std::clamp(level.x(), m_points[m_dragged - 1].x() + 0.01, m_points[m_dragged + 1].x() - 0.01));
    m_points[m_dragged] = level;
    update();
}

void ColourCurveEdit::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_dragged < 0)
        return;
    m_dragged = -1;
    emit edited(m_points);
}

void ColourCurveEdit::mouseDoubleClickEvent(QMouseEvent* event)
{
    int const k = pointAt(event->position());
    if (k <= 0 || k >= m_points.size() - 1)
        return;
    m_points.removeAt(k);
    m_dragged = -1;
    update();
    emit edited(m_points);
}
