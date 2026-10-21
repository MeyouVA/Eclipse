// Eclipse - the Camera view: the Cutter's gestures (a quick straight stroke across lines takes a piece of them away, or
// cuts them in two), and the thickness of lines copied from one to others and nudged.
#include "ui/canvas/Canvas.h"

#include <algorithm>
#include <cmath>

#include <QLineF>

#include "model/document/Commands.h"

namespace {

// Where the piece a-b crosses the piece c-d: how far along a-b (0 to 1), or -1.
double crossingOf(QPointF a, QPointF b, QPointF c, QPointF d)
{
    QPointF const r = b - a, s = d - c;
    double const across = r.x() * s.y() - r.y() * s.x();
    if (std::abs(across) < 1e-12)
        return -1.0;
    QPointF const q = c - a;
    double const t = (q.x() * s.y() - q.y() * s.x()) / across;
    double const u = (q.x() * r.y() - q.y() * r.x()) / across;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0 ? t : -1.0;
}

// A line as it is drawn, as pieces: their ends, and how far along the line each piece starts.
struct Run {
    QVector<QPointF> at;
    QVector<double> along;
    double total = 0.0;
};

Run runOf(Stroke const& line)
{
    Run run;
    for (StrokePoint const& p : line.path())
        run.at.push_back(p.pos);
    if (line.closed && run.at.size() > 2)
        run.at.push_back(run.at.first());
    run.along.resize(run.at.size());
    for (int i = 0; i < run.at.size(); ++i)
        run.along[i] = i == 0 ? 0.0 : run.along[i - 1] + QLineF(run.at[i - 1], run.at[i]).length();
    run.total = run.along.isEmpty() ? 0.0 : run.along.last();
    return run;
}

// How far along `run` it crosses the piece c-d, for each place it does; with `onOther` how far along c-d.
QVector<double> crossings(Run const& run, QPointF c, QPointF d, QVector<double>* onOther = nullptr)
{
    QVector<double> found;
    for (int i = 0; i + 1 < run.at.size(); ++i) {
        double const t = crossingOf(run.at[i], run.at[i + 1], c, d);
        if (t < 0.0)
            continue;
        found.push_back(run.along[i] + (run.along[i + 1] - run.along[i]) * t);
        if (onOther) {
            QPointF const p = run.at[i] + (run.at[i + 1] - run.at[i]) * t;
            QLineF const other(c, d);
            onOther->push_back(other.length() > 1e-12 ? QLineF(c, p).length() / other.length() : 0.0);
        }
    }
    return found;
}

QPointF pointAlong(Run const& run, double distance)
{
    for (int i = 0; i + 1 < run.at.size(); ++i)
        if (distance <= run.along[i + 1] || i + 2 == run.at.size()) {
            double const span = run.along[i + 1] - run.along[i];
            double const t = span > 0.0 ? std::clamp((distance - run.along[i]) / span, 0.0, 1.0) : 0.0;
            return run.at[i] + (run.at[i + 1] - run.at[i]) * t;
        }
    return run.at.isEmpty() ? QPointF() : run.at.first();
}

// The line cut at those distances along it: the pieces, each with the stretch of the line it was ([from, to]; a closed
// line's last piece runs on round past its start).  Its own ends stay as they were; the ends made by the cuts get `tip`.
QVector<QPair<Stroke, QPair<double, double>>> cutAt(Stroke line, QVector<double> cuts, int tip)
{
    Run const run = runOf(line);
    double const near = std::max(run.total * 1e-6, 1e-6);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](double a, double b) { return std::abs(a - b) < near * 10.0; }), cuts.end());
    if (!line.closed)
        cuts.removeIf([&](double c) { return c <= near * 10.0 || c >= run.total - near * 10.0; });
    QVector<QPair<Stroke, QPair<double, double>>> pieces;
    if (cuts.isEmpty() || line.points.size() < 2) {
        pieces.push_back({line, {0.0, run.total}});
        return pieces;
    }
    // A point of the line at each cut (one there already is used), the line keeping its shape.
    QVector<QPointF> places;
    for (double c : std::as_const(cuts))
        places.push_back(pointAlong(run, c));
    for (QPointF const& p : std::as_const(places)) {
        bool there = false;
        for (StrokePoint const& q : std::as_const(line.points))
            there = there || QLineF(q.pos, p).length() < 1e-4;
        if (!there)
            line.insertPoint(p);
    }
    QVector<int> at;
    for (QPointF const& p : std::as_const(places)) {
        int best = -1;
        double nearest = 1e300;
        for (int k = 0; k < line.points.size(); ++k) {
            double const d = QLineF(line.points[k].pos, p).length();
            if (d < nearest) {
                nearest = d;
                best = k;
            }
        }
        at.push_back(best);
    }
    // (The cuts are in order along the line, and so are the points made for them.)
    auto piece = [&](QVector<StrokePoint> const& points, bool startCut, bool endCut) {
        Stroke s = line;
        s.closed = false;
        s.points = points;
        if (startCut)
            s.capStart = Stroke::Cap(tip);
        if (endCut)
            s.capEnd = Stroke::Cap(tip);
        return s;
    };
    if (!line.closed) {
        int from = 0;
        double fromAt = 0.0;
        for (int k = 0; k <= at.size(); ++k) {
            int const to = k < at.size() ? at[k] : int(line.points.size()) - 1;
            double const toAt = k < at.size() ? cuts[k] : run.total;
            if (to > from)
                pieces.push_back({piece(line.points.mid(from, to - from + 1), k > 0, k < at.size()), {fromAt, toAt}});
            from = to;
            fromAt = toAt;
        }
    } else {
        int const n = int(line.points.size());
        for (int k = 0; k < at.size(); ++k) {
            int const from = at[k], to = at[(k + 1) % at.size()];
            QVector<StrokePoint> points;
            for (int i = from;; i = (i + 1) % n) {
                points.push_back(line.points[i]);
                if (i == to && points.size() > 1)
                    break;
                if (points.size() > n + 1)
                    break;
            }
            double const toAt = k + 1 < at.size() ? cuts[k + 1] : cuts[0] + run.total;
            if (points.size() >= 2)
                pieces.push_back({piece(points, true, true), {cuts[k], toAt}});
        }
    }
    return pieces;
}

} // namespace

bool Canvas::isGesture(QPolygonF const& path, double zoom)
{
    if (path.size() < 2)
        return false;
    double length = 0.0;
    for (int i = 1; i < path.size(); ++i)
        length += QLineF(path[i - 1], path[i]).length();
    double const chord = QLineF(path.first(), path.last()).length();
    return chord * zoom >= 12.0 && chord >= length * 0.85;
}

bool Canvas::gestureCut(Drawing& drawing, QPointF a, QPointF b, int gesture, int tip)
{
    if (gesture == GestureOff || QLineF(a, b).length() <= 0.0)
        return false;
    QVector<Stroke>& lines = drawing.strokes;
    bool changed = false;
    if (gesture == GestureBreak) {
        // Each line the stroke crosses cut in two there.
        QVector<Stroke> result;
        for (Stroke const& line : std::as_const(lines)) {
            QVector<double> const cuts = crossings(runOf(line), a, b);
            if (cuts.isEmpty()) {
                result.push_back(line);
                continue;
            }
            auto const pieces = cutAt(line, cuts, tip);
            changed = changed || pieces.size() > 1 || line.closed;
            for (auto const& piece : pieces)
                result.push_back(piece.first);
        }
        if (changed)
            lines = result;
        return changed;
    }
    // The lines the stroke crosses, in the order it crosses them.
    struct Hit {
        int line;
        double along;   // on the line
        double t;       // on the stroke
    };
    QVector<Hit> hits;
    for (int i = 0; i < lines.size(); ++i) {
        QVector<double> onStroke;
        QVector<double> const along = crossings(runOf(lines[i]), a, b, &onStroke);
        for (int k = 0; k < along.size(); ++k)
            hits.push_back({i, along[k], onStroke[k]});
    }
    if (hits.isEmpty())
        return false;
    std::sort(hits.begin(), hits.end(), [](Hit const& x, Hit const& y) { return x.t < y.t; });
    if (gesture == GestureSingle)
        hits.resize(1);
    QVector<Stroke> result;
    for (int i = 0; i < lines.size(); ++i) {
        QVector<double> struck;
        for (Hit const& h : std::as_const(hits))
            if (h.line == i)
                struck.push_back(h.along);
        if (struck.isEmpty()) {
            result.push_back(lines[i]);
            continue;
        }
        // Where the other lines cross this one: the piece struck runs from one of those to the next.
        Run const run = runOf(lines[i]);
        QVector<double> bounds;
        for (int o = 0; o < lines.size(); ++o) {
            if (o == i)
                continue;
            Run const other = runOf(lines[o]);
            for (int k = 0; k + 1 < other.at.size(); ++k)
                bounds += crossings(run, other.at[k], other.at[k + 1]);
        }
        std::sort(bounds.begin(), bounds.end());
        auto const pieces = cutAt(lines[i], bounds, tip);
        changed = true;
        for (auto const& piece : pieces) {
            bool gone = false;
            for (double s : std::as_const(struck)) {
                double const from = piece.second.first, to = piece.second.second;
                gone = gone || (s >= from && s <= to) || (s + run.total >= from && s + run.total <= to);
            }
            if (!gone)
                result.push_back(piece.first);
        }
    }
    if (changed)
        lines = result;
    return changed;
}

// ---- thickness ---------------------------------------------------------------------------------------------------------

void Canvas::nudgeSelectionThickness(bool thicker, bool more)
{
    double const step = more ? 1.25 : 1.05;
    scaleSelectionThickness(thicker ? step : 1.0 / step, 0.0, tr("Nudge Selection Thickness"));
}

void Canvas::onActionCopyThickness()
{
    Drawing const* const d = currentDrawing();
    QVector<float> const profile = selectedLineProfile();
    if (!d || profile.isEmpty()) {
        emit message(tr("Select a pencil line to copy its thickness."));
        return;
    }
    // The profile is in shares of the thickest place: how thick that is.
    Stroke const& line = d->strokes[m_selStrokes.first()];
    float most = 0.0f;
    for (StrokePoint const& p : line.path())
        most = std::max(most, p.pressure);
    m_copiedProfile = profile;
    m_copiedWidth = line.width * most;
    emit message(tr("Thickness copied: select lines and use Paste Thickness."));
}

void Canvas::onActionPasteThickness()
{
    if (m_copiedProfile.isEmpty() || m_copiedWidth <= 0.0f) {
        emit message(tr("Copy the thickness of a line first (Copy Thickness)."));
        return;
    }
    Drawing const* const d = currentDrawing();
    if (!d || !selecting() || m_selStrokes.isEmpty()) {
        emit message(tr("Select the lines to give the thickness to."));
        return;
    }
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes)) {
        if (i < 0 || i >= edited.strokes.size() || edited.strokes[i].width <= 0.0f)
            continue;       // an invisible line stays one
        Stroke& line = edited.strokes[i];
        for (StrokePoint& p : line.points)
            p.pressure = 1.0f;
        line.width = m_copiedWidth;
        applyProfile(line, m_copiedProfile);
    }
    commitDrawing(edited, tr("Paste Thickness"));
    update();
}
