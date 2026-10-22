// Eclipse - a line edited: places along it, sections, reversing, joining, removing points, freezing handles.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"
#include "model/scene/StrokeShared.h"

using namespace stroke_shared;

namespace {

// The piece of the curve from point `s` and its two control points.
void pieceOf(Stroke const& line, int s, QPointF& a, QPointF& c1, QPointF& c2, QPointF& b)
{
    int const n = int(line.points.size());
    a = line.points[s].pos;
    b = line.points[(s + 1) % n].pos;
    if (line.curve) {
        controls(line, s, c1, c2);
    } else {
        c1 = a + (b - a) / 3.0;
        c2 = a + (b - a) * (2.0 / 3.0);
    }
}

} // namespace

QPointF Stroke::pointAt(double at) const
{
    int const n = int(points.size());
    if (n == 0)
        return QPointF();
    int const span = pieces();
    if (span == 0)
        return points.first().pos;
    double const place = std::clamp(at, 0.0, double(span));
    int const s = std::min(int(std::floor(place)), span - 1);
    QPointF a, c1, c2, b;
    pieceOf(*this, s, a, c1, c2, b);
    return cubicAt(a, c1, c2, b, place - s);
}

QPointF Stroke::directionAt(double at) const
{
    double const e = 1e-3;
    double const span = pieces();
    QPointF const way = pointAt(std::min(span, at + e)) - pointAt(std::max(0.0, at - e));
    double const length = std::hypot(way.x(), way.y());
    return length > 1e-12 ? way / length : QPointF();
}

double Stroke::placeAt(QPointF p) const
{
    int const span = pieces();
    double best = 0.0, bestDistance = 1e300;
    for (int s = 0; s < span; ++s) {
        QPointF a, c1, c2, b;
        pieceOf(*this, s, a, c1, c2, b);
        for (int k = 0; k <= 64; ++k) {
            double const t = k / 64.0;
            double const d = QLineF(cubicAt(a, c1, c2, b, t), p).length();
            if (d < bestDistance) {
                bestDistance = d;
                best = s + t;
            }
        }
    }
    // Closer, about the best of those.
    int const s = std::clamp(int(std::floor(best)), 0, std::max(0, span - 1));
    if (span > 0) {
        QPointF a, c1, c2, b;
        pieceOf(*this, s, a, c1, c2, b);
        double const from = std::max(0.0, best - s - 1.0 / 64.0), to = std::min(1.0, best - s + 1.0 / 64.0);
        for (int k = 0; k <= 32; ++k) {
            double const t = from + (to - from) * k / 32.0;
            double const d = QLineF(cubicAt(a, c1, c2, b, t), p).length();
            if (d < bestDistance) {
                bestDistance = d;
                best = s + t;
            }
        }
    }
    return best;
}

Stroke Stroke::section(int from, int to) const
{
    int const n = int(points.size());
    Stroke out = *this;
    out.closed = false;
    out.points.clear();
    out.thickness.clear();
    if (n == 0 || to <= from)
        return out;
    for (int i = from; i <= to; ++i)
        out.points.push_back(points[((i % n) + n) % n]);
    if (thickness.isEmpty())
        return out;
    for (StrokePoint& p : out.points) {
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
    double const span = pieces();
    bool const loop = closed && n >= 2;
    auto put = [&](double at, ThicknessPoint sides) {
        sides.at = at;
        out.thickness.push_back(sides);
    };
    put(0.0, sidesAt(from));
    for (ThicknessPoint const& key : thickness)
        for (double u : {key.at, key.at + span}) {
            if (!loop && u != key.at)
                continue;
            if (u > from + 1e-9 && u < to - 1e-9)
                put(u - from, key);
        }
    put(double(to - from), sidesAt(to));
    std::stable_sort(out.thickness.begin(), out.thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
    out.simplifyThickness(1e-4);
    return out;
}

void Stroke::reverse()
{
    int const n = int(points.size());
    double const span = pieces();
    bool const loop = closed && n >= 2;
    std::reverse(points.begin(), points.end());
    for (StrokePoint& p : points) {
        std::swap(p.in, p.out);
        p.lean = -p.lean;       // its left is now its right
    }
    std::swap(capStart, capEnd);
    if (thickness.isEmpty())
        return;
    for (ThicknessPoint& key : thickness) {
        double at = (loop ? n - 1.0 : span) - key.at;
        if (loop && at < 0.0)
            at += span;
        key.at = at;
        std::swap(key.left, key.right);
    }
    std::stable_sort(thickness.begin(), thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
}

void Stroke::append(Stroke const& other, bool meets)
{
    if (other.points.isEmpty())
        return;
    if (points.isEmpty()) {
        *this = other;
        return;
    }
    // The other line's thickness, against this one's width.
    double const scale = width > 0.0f && other.width > 0.0f ? double(other.width) / width : 1.0;
    bool const profiled = !thickness.isEmpty() || !other.thickness.isEmpty() || std::abs(scale - 1.0) > 1e-6;
    Stroke after = other;
    if (profiled) {
        ownThickness();
        after.ownThickness();
        for (ThicknessPoint& key : after.thickness) {
            key.left = float(key.left * scale);
            key.right = float(key.right * scale);
        }
        // Places at the two ends, so that the thickness runs from the one to the other.
        thicknessPointAt(pieces());
        after.thicknessPointAt(0.0);
    }
    double const offset = pieces() + (meets ? 0.0 : 1.0);
    if (curve || after.curve) {
        freezeHandles();
        after.freezeHandles();
    }
    if (meets) {
        // The point where they meet: this one's way in, the other's way out (a corner, as the two lines meet there).
        StrokePoint& meet = points.last();
        meet.out = after.points.first().out;
        meet.handles = meet.handles || after.points.first().handles;
        meet.corner = true;
    }
    for (int i = meets ? 1 : 0; i < after.points.size(); ++i)
        points.push_back(after.points[i]);
    capEnd = after.capEnd;
    if (!profiled)
        return;
    for (ThicknessPoint key : std::as_const(after.thickness)) {
        key.at += offset;
        if (!thickness.isEmpty() && key.at <= thickness.last().at + 1e-9) {
            // Where they meet: this line's thickness up to there, the other's straight after.
            if (std::abs(key.left - thickness.last().left) < 1e-5f && std::abs(key.right - thickness.last().right) < 1e-5f)
                continue;
            key.at = thickness.last().at + 1e-3;
        }
        thickness.push_back(key);
    }
}

ThicknessPoint Stroke::sidesAtLength(double fraction) const
{
    Lengths const l = lengthsOf(*this);
    ThicknessPoint sides = sidesAt(placeAtLength(l, std::clamp(fraction, 0.0, 1.0) * l.total));
    sides.at = fraction;
    return sides;
}

void Stroke::removePoint(int index)
{
    if (index < 0 || index >= points.size())
        return;
    bool const profiled = !thickness.isEmpty();
    QVector<QPair<double, ThicknessPoint>> const byLength = profiled ? thicknessByLength() : QVector<QPair<double, ThicknessPoint>>();
    points.removeAt(index);
    if (profiled)
        setThicknessByLength(byLength);
}

void Stroke::freezeHandles()
{
    if (!curve) {
        // Straight pieces: a curve whose handles have no length.
        for (StrokePoint& p : points) {
            p.handles = true;
            p.corner = true;
            p.in = p.out = QPointF();
        }
        curve = true;
        return;
    }
    QVector<QPair<QPointF, QPointF>> kept;
    for (int i = 0; i < points.size(); ++i) {
        QPointF in, out;
        handlesAt(i, in, out);
        kept.push_back({in, out});
    }
    for (int i = 0; i < points.size(); ++i) {
        points[i].handles = true;
        points[i].in = kept[i].first;
        points[i].out = kept[i].second;
    }
}
