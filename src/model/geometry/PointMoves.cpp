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
    Stroke rest, now;
    QVector<QPointF> at;
    QVector<double> places;
    QRectF box;
    double reach = 4.0;
};

void addAnchor(QVector<Anchor>& anchors, Stroke const& rest, Stroke const& now)
{
    int const span = rest.pieces();
    if (span == 0 || now.pieces() != span)
        return;
    Anchor a;
    a.rest = rest;
    a.now = now;
    int const steps = span * 32;
    for (int k = 0; k <= steps; ++k) {
        double const place = double(k) / 32.0;
        QPointF const from = rest.pointAt(place);
        a.at.push_back(from);
        a.places.push_back(place);
    }
    a.box = QPolygonF(a.at).boundingRect();
    for (StrokePoint const& p : rest.path())
        a.reach = std::max(a.reach, double(rest.width) * p.pressure / 2.0 + 4.0);
    anchors.push_back(a);
}

// Bind an edge to its nearest original boundary, then evaluate the same place on the moved curve.
QPointF follow(QPointF v, QVector<Anchor> const& anchors)
{
    double best = 1e300;
    QPointF displacement;
    for (Anchor const& a : anchors) {
        if (!a.box.adjusted(-a.reach, -a.reach, a.reach, a.reach).contains(v))
            continue;
        int closest = -1;
        double distance = 1e300;
        for (int k = 0; k + 1 < a.at.size(); ++k) {
            double const d = distanceToSegment(v, a.at[k], a.at[k + 1]);
            if (d < distance) { distance = d; closest = k; }
        }
        if (closest < 0 || distance > a.reach + 1.0)
            continue;
        double low = a.places[closest], high = a.places[closest + 1];
        auto distanceAt = [&](double place) { QPointF const d = a.rest.pointAt(place) - v; return QPointF::dotProduct(d, d); };
        for (int k = 0; k < 24; ++k) {
            double const left = (2 * low + high) / 3.0, right = (low + 2 * high) / 3.0;
            if (distanceAt(left) < distanceAt(right)) high = right;
            else low = left;
        }
        double place = (low + high) / 2.0;
        for (double end : {a.places[closest], a.places[closest + 1]})
            if (distanceAt(end) < distanceAt(place)) place = end;
        double const d2 = distanceAt(place);
        if (d2 <= a.reach * a.reach && d2 < best) {
            best = d2;
            displacement = a.now.pointAt(place) - a.rest.pointAt(place);
        }
    }
    return displacement;
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
        for (int f = 0; f < fills.size(); ++f) {
            bool reshaped = false;
            for (int c = 0; c < fills[f].contours.size(); ++c) {
                reshaped = reshaped || contourMoved[f][c];
            }
            if (reshaped)
                fills[f].rebuild();
        }
    }
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Anchor> anchors;
        for (int i = 0; i < drawing.strokesOf(sheet).size(); ++i)
            addAnchor(anchors, drawing.strokesOf(sheet)[i], out.strokesOf(sheet)[i]);
        for (int f = 0; f < drawing.fillsOf(sheet).size(); ++f)
            for (int c = 0; c < drawing.fillsOf(sheet)[f].contours.size(); ++c)
                addAnchor(anchors, drawing.fillsOf(sheet)[f].contours[c], out.fillsOf(sheet)[f].contours[c]);
        if (sheet == Drawing::ColourSheet) {
            for (int i = 0; i < drawing.strokes.size(); ++i)
                addAnchor(anchors, drawing.strokes[i], out.strokes[i]);
            for (int f = 0; f < drawing.fills.size(); ++f)
                for (int c = 0; c < drawing.fills[f].contours.size(); ++c)
                    addAnchor(anchors, drawing.fills[f].contours[c], out.fills[f].contours[c]);
        }
        // Paint without its own contour follows only boundaries on its sheet, or Line Art above Colour Art.
        for (Fill& fill : out.fillsOf(sheet)) {
            if (!fill.contours.isEmpty() || !fill.text.isEmpty())
                continue;
            if (fill.link != 0) {
                for (int line = 0; line < drawing.strokesOf(sheet).size(); ++line) {
                    Stroke const& rest = drawing.strokesOf(sheet)[line];
                    if (rest.link != fill.link)
                        continue;
                    bool changed = false;
                    for (PointMove const& m : moves)
                        changed = changed || (m.drawing == index && m.sheet == sheet && m.fill < 0 && m.line == line);
                    if (!changed)
                        break;
                    QPolygonF was;
                    for (StrokePoint const& p : rest.path()) was << p.pos;
                    QRectF const a = fill.bounds(), b = was.boundingRect();
                    if (std::abs(a.left() - b.left()) > 3 || std::abs(a.right() - b.right()) > 3
                        || std::abs(a.top() - b.top()) > 3 || std::abs(a.bottom() - b.bottom()) > 3)
                        break;
                    QPolygonF boundary;
                    for (StrokePoint const& p : out.strokesOf(sheet)[line].path()) boundary << p.pos;
                    fill.outlines = {boundary};
                    break;
                }
                continue;
            }
            for (QPolygonF& outline : fill.outlines)
                for (QPointF& v : outline)
                    v += follow(v, anchors);
        }
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
