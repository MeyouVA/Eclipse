#include "board/ui/BoardTimeline.h"
#include "board/model/BoardProject.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <cmath>
#include <limits>

namespace {
constexpr int rulerHeight = 25;
constexpr int panelHeight = 44;
constexpr int audioHeight = 29;
constexpr int gutter = 65;
}

BoardTimeline::BoardTimeline(QWidget* parent) : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("BoardTimeline"));
    setAccessibleName(tr("Timeline"));
    setToolTip(tr("Click or drag to scrub. Drag the right edge of a panel to change its duration."));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(125);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}

void BoardTimeline::setProject(BoardProject const& project)
{
    if (m_resizing.first >= 0) {
        m_resizing = {-1, -1};
        viewport()->unsetCursor();
    }
    m_spans = project.spans();
    m_cameraKeys.clear();
    for (auto const& span : m_spans)
        for (auto const& key : project.shots[span.shot].panels[span.panel].camera)
            m_cameraKeys.append(span.start + key.frame);
    m_sound = project.sound;
    m_names.clear();
    m_dissolves.clear();
    for (auto const& shot : project.shots) {
        m_names.push_back(shot.name);
        m_dissolves.push_back(shot.dissolve);
    }
    m_length = m_spans.isEmpty() ? 0 : m_spans.last().start + m_spans.last().length;
    m_frame = std::clamp(m_frame, 0, std::max(0, m_length - 1));
    updateScrollBar();
    viewport()->update();
}

BoardSpan const* BoardTimeline::spanAt(int frame) const
{
    if (frame < 0 || frame >= m_length)
        return nullptr;
    auto const after = std::upper_bound(m_spans.cbegin(), m_spans.cend(), frame,
        [](int at, BoardSpan const& span) { return at < span.start; });
    return &*std::prev(after);
}

int BoardTimeline::panelStart(int shot, int panel) const
{
    auto const address = qMakePair(shot, panel);
    auto const found = std::lower_bound(m_spans.cbegin(), m_spans.cend(), address,
        [](BoardSpan const& span, QPair<int, int> const& target) { return qMakePair(span.shot, span.panel) < target; });
    return found == m_spans.cend() || qMakePair(found->shot, found->panel) != address ? -1 : found->start;
}

void BoardTimeline::setFrame(int frame, bool follow)
{
    m_frame = std::clamp(frame, 0, std::max(0, m_length - 1));
    if (follow) {
        int const first = horizontalScrollBar()->value();
        if (m_frame < first)
            horizontalScrollBar()->setValue(m_frame);
        else if (qint64(m_frame) >= qint64(first) + visibleFrames())
            horizontalScrollBar()->setValue(m_frame - visibleFrames() + 1);
    }
    viewport()->update();
}

void BoardTimeline::zoom(int direction)
{
    m_scale = std::clamp(m_scale * (direction > 0 ? 1.5 : 1.0 / 1.5), 0.1, 30.0);
    updateScrollBar();
    setFrame(m_frame, true);
}

double BoardTimeline::xAt(qint64 frame) const { return gutter + double(frame - horizontalScrollBar()->value()) * m_scale; }

int BoardTimeline::frameAt(double x) const
{
    double const frame = horizontalScrollBar()->value() + std::floor((x - gutter) / m_scale);
    return int(std::clamp(frame, 0.0, double(std::max(0, m_length - 1))));
}

int BoardTimeline::visibleFrames() const { return std::max(1, int((viewport()->width() - gutter) / m_scale)); }

void BoardTimeline::updateScrollBar()
{
    horizontalScrollBar()->setPageStep(visibleFrames());
    horizontalScrollBar()->setRange(0, std::max(0, m_length - visibleFrames()));
}

void BoardTimeline::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
}

void BoardTimeline::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    QRect const area = viewport()->rect();
    auto const colors = palette();
    QColor const base = colors.base().color();
    QColor const inactive = base.lightnessF() < 0.5 ? base.lighter(180) : base.darker(118);
    painter.fillRect(area, colors.base());
    painter.fillRect(0, 0, area.width(), rulerHeight, colors.alternateBase());
    painter.fillRect(0, rulerHeight + panelHeight, area.width(), audioHeight, colors.alternateBase());
    painter.setPen(colors.text().color());
    if (m_length > 0)
        painter.drawText(6, 16, tr("Frame %1").arg(m_frame + 1));
    painter.drawText(6, rulerHeight + 25, tr("Panels"));
    painter.drawText(6, rulerHeight + panelHeight + 19, tr("Sound"));
    painter.setClipRect(QRect(gutter, 0, std::max(0, area.width() - gutter), area.height()));
    int const first = horizontalScrollBar()->value();
    int const step = std::max(1, int(std::ceil(68.0 / m_scale)));
    for (qint64 at = (first / step) * qint64(step); at < m_length && xAt(at) < area.width(); at += step) {
        double const x = xAt(at);
        painter.drawLine(QLineF(x, rulerHeight - 5, x, rulerHeight));
        painter.drawText(QPointF(x + 3, 15), QString::number(at + 1));
    }
    if (auto const* start = spanAt(first))
        for (auto const* span = start; span != m_spans.constData() + m_spans.size() && xAt(span->start) < area.width(); ++span) {
            double const left = std::max(double(gutter), xAt(span->start));
            int const length = qMakePair(span->shot, span->panel) == m_resizing ? m_resizeLength : span->length;
            double const right = std::min(double(area.width()), xAt(qint64(span->start) + length));
            QRectF const rect(left, rulerHeight + 3, std::max(1.0, right - left - 1.0), panelHeight - 6);
            bool const selected = m_frame >= span->start && m_frame - span->start < span->length;
            painter.fillRect(rect, selected ? colors.highlight() : QBrush(inactive));
            if (span->panel == 0 && span->shot > 0 && m_dissolves[span->shot] > 0) {
                double const end = xAt(span->start + std::min(span->length, m_dissolves[span->shot]));
                painter.fillRect(QRectF(rect.left(), rect.top(), std::max(0.0, std::min(end, rect.right()) - rect.left()), 4),
                                 QColor(221, 168, 80));
            }
            painter.setPen((selected ? colors.highlightedText() : colors.text()).color());
            painter.drawText(rect.adjusted(5, 0, -3, 0), Qt::AlignVCenter,
                painter.fontMetrics().elidedText(tr("%1 · %2   %3f").arg(m_names[span->shot]).arg(span->panel + 1)
                    .arg(length), Qt::ElideRight, std::max(0, int(rect.width()) - 8)));
        }
    if (!m_sound.asset.isEmpty()) {
        double const left = std::max(double(gutter), xAt(m_sound.start));
        double const right = std::min(double(area.width()), xAt(std::min(m_length, m_sound.start + m_sound.length)));
        if (right > left) {
            QRectF const clip(left, rulerHeight + panelHeight + 3, right - left - 1, audioHeight - 6);
            painter.fillRect(clip, m_sound.muted ? inactive : QColor(37, 117, 99));
            painter.setPen(m_sound.muted ? colors.text().color() : QColor(Qt::white));
            painter.drawText(clip.adjusted(5, 0, -3, 0), Qt::AlignVCenter,
                painter.fontMetrics().elidedText(m_sound.name, Qt::ElideRight, std::max(0, int(clip.width()) - 8)));
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(95, 192, 245));
    auto const firstKey = std::lower_bound(m_cameraKeys.cbegin(), m_cameraKeys.cend(), first);
    for (auto key = firstKey; key != m_cameraKeys.cend() && xAt(*key) < area.width(); ++key) {
        double const x = xAt(*key);
        double const y = rulerHeight + panelHeight - 8;
        painter.drawPolygon(QPolygonF(QList<QPointF>{{x, y - 4}, {x + 4, y}, {x, y + 4}, {x - 4, y}}));
    }
    if (m_length > 0) {
        painter.setPen(QColor(246, 115, 80));
        painter.drawLine(QLineF(xAt(m_frame), 0, xAt(m_frame), area.height()));
    }
    if (m_resizing.first >= 0) {
        double const edge = xAt(qint64(m_resizeStart) + m_resizeLength);
        painter.setPen(colors.highlight().color());
        painter.drawLine(QLineF(edge, rulerHeight, edge, rulerHeight + panelHeight));
    }
}

void BoardTimeline::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_length == 0 || event->position().x() < gutter)
        return;
    int const frame = frameAt(event->position().x());
    if (auto const* span = spanAt(frame); span && event->position().y() >= rulerHeight
        && event->position().y() < rulerHeight + panelHeight && m_scale * span->length >= 12
        && std::abs(xAt(span->start + span->length) - event->position().x()) <= 6) {
        m_resizing = {span->shot, span->panel};
        m_resizeStart = span->start;
        m_resizeLength = span->length;
        viewport()->setCursor(Qt::SizeHorCursor);
    }
    emit seek(frame);
}

void BoardTimeline::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if (m_resizing.first >= 0) {
        double const edge = horizontalScrollBar()->value() + (event->position().x() - gutter) / m_scale;
        m_resizeLength = int(std::clamp(std::round(edge - m_resizeStart), 1.0, 1000000.0));
        viewport()->update();
    } else {
        emit seek(frameAt(event->position().x()));
    }
}

void BoardTimeline::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_resizing.first >= 0) {
        auto const [shot, panel] = m_resizing;
        m_resizing = {-1, -1};
        viewport()->unsetCursor();
        emit durationChanged(shot, panel, m_resizeLength);
        viewport()->update();
    }
}

void BoardTimeline::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        emit seek(m_frame + (event->key() == Qt::Key_Left ? -1 : 1));
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}
