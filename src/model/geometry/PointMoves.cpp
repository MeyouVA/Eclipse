// Eclipse - point animation: a drawing's own points and handles moved from keyframe to keyframe.
#include "model/geometry/Deform.h"

#include <algorithm>
#include <cmath>

namespace Deform {

namespace {

constexpr double kTiny = 1e-9;

bool moved(QPointF p)
{
    return std::abs(p.x()) > kTiny || std::abs(p.y()) > kTiny;
}

// The handles a point has, made its own (a line of straight pieces: handles of no length).
Stroke frozen(Stroke line)
{
    line.freezeHandles();
    return line;
}

void apply(Stroke& line, Stroke const& rest, PointMove const& m)
{
    if (m.point < 0 || m.point >= line.points.size())
        return;
    StrokePoint& p = line.points[m.point];
    StrokePoint const& was = rest.points[m.point];
    p.pos = was.pos + m.by;
    p.in = was.in + m.in;
    p.out = was.out + m.out;
}

// A line seen as places along it: where each is at rest and how far it has gone.
struct Anchor {
    QVector<QPointF> at, by;
    QRectF box;
};

void addAnchor(QVector<Anchor>& anchors, Stroke const& rest, Stroke const& now)
{
    int const span = rest.pieces();
    if (span == 0 || now.pieces() != span)
        return;
    Anchor a;
    int const steps = span * 8;
    for (int k = 0; k <= steps; ++k) {
        double const place = double(k) / 8.0;
        QPointF const from = rest.pointAt(place);
        a.at.push_back(from);
        a.by.push_back(now.pointAt(place) - from);
    }
    a.box = QPolygonF(a.at).boundingRect();
    anchors.push_back(a);
}

// How far a point of paint goes: as the lines nearest it go, the nearer the more.
QPointF follow(QPointF v, QVector<Anchor> const& anchors)
{
    auto boxDistance = [v](QRectF const& r) {
        double const dx = std::max({r.left() - v.x(), 0.0, v.x() - r.right()});
        double const dy = std::max({r.top() - v.y(), 0.0, v.y() - r.bottom()});
        return std::hypot(dx, dy);
    };
    double nearest = 1e300;
    for (Anchor const& a : anchors)
        nearest = std::min(nearest, boxDistance(a.box));
    QPointF sum;
    double weights = 0.0;
    for (Anchor const& a : anchors) {
        if (boxDistance(a.box) > 4.0 * nearest + 40.0)
            continue;
        double best = 1e300;
        QPointF by;
        for (int k = 0; k < a.at.size(); ++k) {
            QPointF const d = a.at[k] - v;
            double const d2 = d.x() * d.x() + d.y() * d.y();
            if (d2 < best) {
                best = d2;
                by = a.by[k];
            }
        }
        double const w = 1.0 / (best + 1.0);
        sum += by * w;
        weights += w;
    }
    return weights > 0.0 ? sum / weights : QPointF();
}

}  // namespace

bool movesPoints(QVector<PointMove> const& moves, int index)
{
    for (PointMove const& m : moves)
        if (m.drawing == index)
            return true;
    return false;
}

Drawing movedPoints(Drawing const& drawing, int index, QVector<PointMove> const& moves)
{
    if (index < 0 || !movesPoints(moves, index))
        return drawing;
    Drawing out = drawing;
    QVector<Anchor> anchors;
    bool any = false;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Stroke>& lines = out.strokesOf(sheet);
        QVector<Fill>& fills = out.fillsOf(sheet);
        QVector<bool> lineMoved(lines.size(), false);
        QVector<QVector<bool>> contourMoved(fills.size());
        for (int f = 0; f < fills.size(); ++f)
            contourMoved[f].fill(false, fills[f].contours.size());
        for (PointMove const& m : moves) {
            if (m.drawing != index || m.sheet != sheet)
                continue;
            if (m.fill < 0 && m.line >= 0 && m.line < lines.size())
                lineMoved[m.line] = true;
            else if (m.fill >= 0 && m.fill < fills.size() && m.line >= 0 && m.line < fills[m.fill].contours.size())
                contourMoved[m.fill][m.line] = true;
        }
        // (Each line moved: its handles made its own first, so that they turn as they were turned when the keyframe was set.)
        for (int i = 0; i < lines.size(); ++i)
            if (lineMoved[i])
                lines[i] = frozen(lines[i]);
        for (int f = 0; f < fills.size(); ++f)
            for (int c = 0; c < fills[f].contours.size(); ++c)
                if (contourMoved[f][c])
                    fills[f].contours[c] = frozen(fills[f].contours[c]);
        for (PointMove const& m : moves) {
            if (m.drawing != index || m.sheet != sheet)
                continue;
            if (m.fill < 0 && m.line >= 0 && m.line < lines.size())
                apply(lines[m.line], frozen(drawing.strokesOf(sheet)[m.line]), m);
            else if (m.fill >= 0 && m.fill < fills.size() && m.line >= 0 && m.line < fills[m.fill].contours.size())
                apply(fills[m.fill].contours[m.line], frozen(drawing.fillsOf(sheet)[m.fill].contours[m.line]), m);
        }
        for (int i = 0; i < lines.size(); ++i) {
            addAnchor(anchors, drawing.strokesOf(sheet)[i], lines[i]);
            any = any || lineMoved[i];
        }
        for (int f = 0; f < fills.size(); ++f) {
            bool reshaped = false;
            for (int c = 0; c < fills[f].contours.size(); ++c) {
                addAnchor(anchors, drawing.fillsOf(sheet)[f].contours[c], fills[f].contours[c]);
                reshaped = reshaped || contourMoved[f][c];
            }
            if (reshaped)
                fills[f].rebuild();
            any = any || reshaped;
        }
    }
    if (!any)
        return out;
    // Paint with no contour of its own: each corner of its edge goes as the lines round it go.
    QRectF reach;
    for (Anchor const& a : anchors)
        for (QPointF const& by : a.by)
            if (moved(by)) {
                reach = reach.united(a.box);
                break;
            }
    reach.adjust(-60, -60, 60, 60);
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        for (Fill& fill : out.fillsOf(sheet)) {
            if (!fill.contours.isEmpty() || !fill.text.isEmpty() || !fill.bounds().intersects(reach))
                continue;
            for (QPolygonF& outline : fill.outlines)
                for (QPointF& v : outline)
                    v += follow(v, anchors);
        }
    return out;
}

bool pointMoves(Drawing const& rest, Drawing const& now, int index, QVector<PointMove>& out)
{
    out.clear();
    auto compare = [&](Stroke const& a, Stroke const& b, int sheet, int fill, int line) {
        if (a.points.size() != b.points.size() || a.closed != b.closed)
            return false;
        Stroke const was = frozen(a), is = frozen(b);
        for (int i = 0; i < was.points.size(); ++i) {
            PointMove m;
            m.drawing = index;
            m.sheet = sheet;
            m.fill = fill;
            m.line = line;
            m.point = i;
            m.by = is.points[i].pos - was.points[i].pos;
            m.in = is.points[i].in - was.points[i].in;
            m.out = is.points[i].out - was.points[i].out;
            if (moved(m.by) || moved(m.in) || moved(m.out))
                out.push_back(m);
        }
        return true;
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Stroke> const& lines = rest.strokesOf(sheet);
        QVector<Fill> const& fills = rest.fillsOf(sheet);
        if (lines.size() != now.strokesOf(sheet).size() || fills.size() != now.fillsOf(sheet).size())
            return false;
        for (int f = 0; f < fills.size(); ++f) {
            if (fills[f].contours.size() != now.fillsOf(sheet)[f].contours.size())
                return false;
            for (int c = 0; c < fills[f].contours.size(); ++c)
                if (!compare(fills[f].contours[c], now.fillsOf(sheet)[f].contours[c], sheet, f, c))
                    return false;
        }
        for (int i = 0; i < lines.size(); ++i)
            if (!compare(lines[i], now.strokesOf(sheet)[i], sheet, -1, i))
                return false;
    }
    std::sort(out.begin(), out.end(), [](PointMove const& a, PointMove const& b) { return a.comesBefore(b); });
    return true;
}

QVector<PointMove> withMoves(QVector<PointMove> const& all, int index, QVector<PointMove> const& mine)
{
    QVector<PointMove> out;
    for (PointMove const& m : all)
        if (m.drawing != index)
            out.push_back(m);
    out += mine;
    std::sort(out.begin(), out.end(), [](PointMove const& a, PointMove const& b) { return a.comesBefore(b); });
    return out;
}

}  // namespace Deform
