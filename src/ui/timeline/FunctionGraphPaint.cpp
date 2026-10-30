// Eclipse - the Function view's graph painted: the grid, the curves of the layers drawn, their points and handles.
#include "ui/timeline/FunctionView.h"

#include <QPainter>

#include <cmath>

#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"

void FunctionGraph::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter p(this);
    p.fillRect(rect(), InterfaceColours::get("functionBackground"));
    QRectF const r = plot();
    p.fillRect(r, InterfaceColours::get("functionPlot"));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
    p.setFont(small);

    // Lines across at round values, with the value at the left.
    double const span = m_high - m_low;
    double step = std::pow(10.0, std::floor(std::log10(span / 4.0)));
    if (span / step > 10.0)
        step *= span / step > 25.0 ? 5.0 : 2.0;
    for (double v = std::ceil(m_low / step) * step; v <= m_high; v += step) {
        double const y = yOfValue(v);
        p.setPen(std::abs(v) < step / 2.0 ? InterfaceColours::get("functionAxis") : InterfaceColours::get("functionGrid"));
        if (m_grid || std::abs(v) < step / 2.0)
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(QColor(160, 160, 160));
        p.drawText(QRectF(0, y - 7, r.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(std::abs(v) < step / 2.0 ? 0.0 : v, 'g', 6));
    }
    // Lines down every so many frames, with the number of the frame under them.
    int const frames = scene.frameCount();
    double const pixelsPerFrame = r.width() / frameSpan();
    int every = 1;
    for (int nice : {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000})
        if (nice * pixelsPerFrame >= 36.0 || nice == 1000) {
            every = nice;
            break;
        }
    for (int f = every - 1; f < frames; f += every) {
        double const x = xOfFrame(f);
        if (x < r.left() - 1.0 || x > r.right() + 1.0)
            continue;
        p.setPen(InterfaceColours::get("functionGrid"));
        if (m_grid)
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(QColor(160, 160, 160));
        p.drawText(QRectF(x - 20, r.bottom() + 2, 40, 14), Qt::AlignHCenter | Qt::AlignTop, QString::number(f + 1));
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(r.adjusted(-4, -4, 4, 4));
    // The frames sampled: each frame, and between frames as well when they are far apart.
    int const between = std::clamp(int(pixelsPerFrame / 4.0), 1, 16);
    int const firstFrame = std::max(0, int(std::floor(m_first)) - 1), lastFrame = std::min(frames - 1, int(std::ceil(m_first + frameSpan())) + 1);
    QVector<int> const drawn = layersDrawn();
    int curves = 0;
    // The other layers first, under the edited one: fainter and dashed.
    for (int n = int(drawn.size()) - 1; n >= 0; --n) {
        int const layer = drawn[n];
        bool const main = layer == m_layer || layer < 0;
        Layer const& l = layerOf(layer);
        for (int which = 0; which < Timeline::ParameterCount; ++which) {
            Timeline::Parameter const param = Timeline::Parameter(which);
            // (A column loaded by itself: its one curve.)
            if (layer < 0 ? which != Driver::X : !m_shown[which] || !Timeline::parameterFor(l.kind, param) || (!main && which == Timeline::Fov))
                continue;
            // (Preferences: no more curves than so many.)
            if (++curves > m_loadLimit)
                break;
            QPair<int, int> const source = sourceOf(layer, which);
            QColor colour = layer < 0 ? InterfaceColours::get("functionForeground") : source.first < 0 ? InterfaceColours::get("functionColumn") : colourOf(param);
            bool const follows = driven(layer, which);
            QPolygonF curve;
            for (int f = firstFrame; f <= lastFrame; ++f)
                for (int s = 0; s < (f < lastFrame && !follows ? between : 1); ++s) {
                    double const at = f + double(s) / between;
                    curve << QPointF(xOfFrame(at), yOfValue(valueOf(layer, which, at)));
                }
            if (!main)
                colour.setAlpha(150);
            QPen pen(colour, main ? 1.6 : 1.2);
            // A value that follows another layer (an attribute control): dashed, and no points to take hold of.
            if (!main || follows)
                pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(curve);
            if (follows)
                continue;
            // A square on each point (of the column it is linked to, if it is); red when selected.
            if (source.first < 0 && m_document->scene().functions[-1 - source.first].kind != FunctionColumn::Bezier
                && m_document->scene().functions[-1 - source.first].kind != FunctionColumn::Ease)
                continue;
            Layer const& points = layerOf(source.first);
            for (int k : points.keysOf(source.second)) {
                FunctionPoint const point{source.first, source.second, points.keys[k].frame};
                bool const chosen = m_selection.contains(point);
                p.setPen(QPen(QColor(24, 24, 24), 1));
                p.setBrush(chosen ? InterfaceColours::get("functionSelected") : main ? colour : colour.darker(130));
                QPointF const at = pointOf(point);
                double const size = chosen ? 8.0 : 7.0;
                p.drawRect(QRectF(at.x() - size / 2.0, at.y() - size / 2.0, size, size));
            }
        }
    }
    // The handles of the points selected: a line out to each, a yellow one for the handle chosen.
    for (int i = 0; i < m_selection.size(); ++i) {
        FunctionPoint const& point = m_selection[i];
        QPointF const at = pointOf(point);
        for (bool out : {false, true}) {
            QPointF const handle = handleAt(point, out);
            if (handle.isNull())
                continue;
            bool const chosen = i == 0 && m_handle == (out ? 2 : 1);
            p.setPen(QPen(InterfaceColours::get("functionHandle"), 1.0));
            p.drawLine(at, handle);
            p.setBrush(chosen ? InterfaceColours::get("functionHandle") : QColor(46, 46, 46));
            p.drawEllipse(handle, 3.5, 3.5);
        }
    }
    // The box being drawn round points.
    if (m_drag == Box) {
        QColor fill = InterfaceColours::get("functionSelected");
        fill.setAlpha(40);
        p.setPen(QPen(InterfaceColours::get("functionSelected"), 1.0, Qt::DashLine));
        p.setBrush(fill);
        p.drawRect(m_box);
    }
    p.setClipping(false);
    p.setRenderHint(QPainter::Antialiasing, false);
    // The current frame.
    if (m_currentShown) {
        p.setPen(InterfaceColours::get("functionCurrentFrame"));
        p.drawLine(QPointF(xOfFrame(m_frame), r.top()), QPointF(xOfFrame(m_frame), r.bottom()));
    }
}
