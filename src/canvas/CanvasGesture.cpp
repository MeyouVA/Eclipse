// Eclipse - the Camera view: the Cutter's gestures (a quick straight stroke across lines takes a piece of them away, or
// cuts them in two), and the thickness of lines copied from one to others and nudged.
#include "canvas/Canvas.h"

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
    auto piece = [&](int from, int to, bool startCut, bool endCut) {
        Stroke s = line.section(from, to);
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
                pieces.push_back({piece(from, to, k > 0, k < at.size()), {fromAt, toAt}});
            from = to;
            fromAt = toAt;
        }
    } else {
        int const n = int(line.points.size());
        for (int k = 0; k < at.size(); ++k) {
            int const from = at[k];
            int to = at[(k + 1) % at.size()];
            if (to <= from)
                to += n;
            double const toAt = k + 1 < at.size() ? cuts[k + 1] : cuts[0] + run.total;
            pieces.push_back({piece(from, to, true, true), {cuts[k], toAt}});
        }
    }
    return pieces;
}

// How far `p` is from the line through `points`, and where on it that is nearest.
double nearestOn(QVector<QPointF> const& points, QPointF p, QPointF* at)
{
    if (points.size() == 1) {
        if (at)
            *at = points.first();
        return QLineF(p, points.first()).length();
    }
    double best = 1e300;
    for (int i = 0; i + 1 < points.size(); ++i) {
        QPointF const a = points[i], ab = points[i + 1] - a;
        double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
        double const t = len2 > 0.0 ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 0.0, 1.0) : 0.0;
        QPointF const q = a + ab * t;
        double const d = QLineF(p, q).length();
        if (d < best) {
            best = d;
            if (at)
                *at = q;
        }
    }
    return best;
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
        line.width = m_copiedWidth;
        QVector<QPair<double, ThicknessPoint>> byLength;
        for (int k = 0; k < m_copiedProfile.size(); ++k) {
            float const share = m_copiedProfile[k];
            byLength.push_back({m_copiedProfile.size() > 1 ? double(k) / (m_copiedProfile.size() - 1) : 0.0, {0.0, share, share}});
        }
        line.setThicknessByLength(byLength);
        line.simplifyThickness(0.005);
    }
    commitDrawing(edited, tr("Paste Thickness"));
    update();
}

// ---- the Pencil's Line Pushing and Auto Adjust Thickness ---------------------------------------------------------------

bool Canvas::pushLine(Drawing& drawing, Stroke const& highlight, int mode, double reach)
{
    if (mode == PencilDraw || highlight.points.size() < 2)
        return false;
    QVector<QPointF> h;
    for (StrokePoint const& p : highlight.points)
        h.push_back(p.pos);
    auto thickest = [](Stroke const& line) {
        float most = 0.0f;
        for (StrokePoint const& p : line.path())
            most = std::max(most, p.pressure);
        return line.width * most;
    };
    // The line it runs along: the one most of its points are near (and at least half of them).
    int target = -1, best = 0;
    for (int i = 0; i < drawing.strokes.size(); ++i) {
        Stroke const& line = drawing.strokes[i];
        if (line.width <= 0.0f || line.points.size() < 2)
            continue;
        double const r = reach + thickest(line) / 2.0;
        int near = 0;
        for (QPointF const& q : std::as_const(h))
            near += line.hit(q, r) ? 1 : 0;
        if (near > best) {
            best = near;
            target = i;
        }
    }
    if (target < 0 || best < std::max<qsizetype>(2, h.size() / 2))
        return false;
    Stroke& line = drawing.strokes[target];
    double const r = reach + thickest(line) / 2.0;
    // Pushing: points on the line all along the stretch the stroke covers, so that the line follows the stroke there.
    double const spacing = std::max(r * 0.5, 1e-3);
    QPointF last;
    bool any = mode != PencilPush;
    for (QPointF const& q : std::as_const(h)) {
        if (mode != PencilPush)
            break;
        if (any && QLineF(last, q).length() < spacing)
            continue;
        last = q;
        any = true;
        if (!line.hit(q, r))
            continue;
        Stroke tried = line;
        int const added = tried.insertPoint(q);
        if (added < 0)
            continue;
        bool crowded = false;
        for (int k = 0; k < line.points.size(); ++k)
            crowded = crowded || QLineF(line.points[k].pos, tried.points[added].pos).length() < spacing * 0.5;
        if (!crowded)
            line = tried;
    }
    // Line Pushing: each point of the line by the stroke moved onto it.
    bool changed = false;
    if (mode == PencilPush) {
        for (StrokePoint& point : line.points) {
            QPointF onto;
            double const d = nearestOn(h, point.pos, &onto);
            if (d <= r && d > 1e-9) {
                point.pos = onto;
                changed = true;
            }
        }
        return changed;
    }
    if (line.width <= 0.0f)
        return false;
    float const halfStroke = highlight.width / 2.0f;
    line.ownThickness();
    QVector<double> places;
    QVector<StrokePoint> const run = line.pathWithPlaces(places);
    // Places along the line no further apart than half the spacing.
    QVector<QPair<double, QPointF>> along;
    for (int i = 0; i + 1 < run.size(); ++i) {
        int const steps = std::max(1, int(std::ceil(QLineF(run[i].pos, run[i + 1].pos).length() / (spacing * 0.5))));
        for (int k = 0; k < steps; ++k) {
            double const t = double(k) / steps;
            along.push_back({places[i] + (places[i + 1] - places[i]) * t, run[i].pos + (run[i + 1].pos - run[i].pos) * t});
        }
    }
    if (!run.isEmpty())
        along.push_back({places.last(), run.last().pos});
    for (auto const& [place, at] : std::as_const(along)) {
        QPointF foot;
        double const d = nearestOn(h, at, &foot);
        if (d > r)
            continue;
        // (Only where the stroke runs beside the line: past its ends, it reaches no further than they do.)
        if ((QLineF(foot, h.first()).length() < 1e-6 || QLineF(foot, h.last()).length() < 1e-6) && d > halfStroke + 1e-6)
            continue;
        double const share = (d + halfStroke) * 2.0 / line.width;
        ThicknessPoint& key = line.thickness[line.thicknessPointAt(place, 1e-4)];
        if (key.left < share - 1e-6 || key.right < share - 1e-6) {
            key.left = float(std::min(std::max<double>(key.left, share), 50.0));
            key.right = float(std::min(std::max<double>(key.right, share), 50.0));
            changed = true;
        }
    }
    if (changed)
        line.simplifyThickness(0.01);
    return changed;
}

void Canvas::multiplyThickness(Stroke& line, double multiplier, bool tips, double tipExtra)
{
    if (multiplier <= 0.0 || line.points.isEmpty())
        return;
    // The thickness along the line: its own places, or its points'.
    bool const own = !line.thickness.isEmpty();
    int const n = own ? int(line.thickness.size()) : int(line.points.size());
    auto value = [&](int i) { return own ? (line.thickness[i].left + line.thickness[i].right) / 2.0 : double(line.points[i].pressure); };
    auto scale = [&](int i, double by) {
        if (own) {
            line.thickness[i].left = float(line.thickness[i].left * by);
            line.thickness[i].right = float(line.thickness[i].right * by);
        } else {
            line.points[i].pressure = float(line.points[i].pressure * by);
        }
    };
    double most = 0.0;
    for (int i = 0; i < n; ++i)
        most = std::max(most, value(i));
    if (most <= 0.0)
        return;
    // The tips: from each end, the places before the line reaches (near enough) its full thickness.
    QVector<bool> tip(n, false);
    for (int i = 0; i < n && value(i) < most * 0.95; ++i)
        tip[i] = true;
    for (int i = n - 1; i >= 0 && value(i) < most * 0.95; --i)
        tip[i] = true;
    line.width = float(line.width * multiplier);
    for (int i = 0; i < n; ++i) {
        if (!tip[i])
            continue;
        double const p = value(i);
        double const share = p / most;      // 0 at a point, 1 where the tip meets the body
        double next = p;
        if (!tips)
            next = next * (1.0 + (multiplier - 1.0) * share) / multiplier;
        if (tipExtra != 1.0)
            next = std::min(most, next * tipExtra);
        if (p > 1e-9)
            scale(i, next / p);
    }
}

void Canvas::colourArtTwin(Stroke const& line)
{
    if (!canEditDrawing())
        return;
    Drawing const* const real = realDrawing();
    if (!real || line.points.size() < 2)
        return;
    Drawing whole = *real;
    Stroke edge = line;
    edge.width = 0.0f;
    edge.group = 0;
    whole.colourStrokes.push_back(edge);
    m_committing = true;
    keepWholeDrawing(whole, tr("Draw Stroke"));
    m_committing = false;
}

QPointF Canvas::endMagnet(QPointF p) const
{
    Drawing const* const d = currentDrawing();
    if (!m_snapping || !d)
        return p;
    double nearest = 8.0 / std::max(m_zoom, 1e-6);
    QPointF best = p;
    for (Stroke const& line : d->strokes) {
        if (line.closed || line.points.isEmpty())
            continue;
        for (QPointF const& end : {line.points.first().pos, line.points.last().pos}) {
            double const distance = QLineF(end, p).length();
            if (distance < nearest) {
                nearest = distance;
                best = end;
            }
        }
    }
    return best;
}
