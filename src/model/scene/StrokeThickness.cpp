// Eclipse - a line's thickness profile: keys by place, sides, pressure and lean, simplifying.
#include "model/scene/Scene.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"
#include "model/scene/StrokeShared.h"

using namespace stroke_shared;

int Stroke::pieces() const
{
    int const n = int(points.size());
    return n < 2 ? 0 : (closed && n >= 2 ? n : n - 1);
}

namespace {

double smoothBetween(QVector<ThicknessPoint> const& keys, int j, double at, float ThicknessPoint::*side)
{
    auto slope = [&](int k) {
        if (k <= 0 || k >= keys.size() - 1)
            return 0.0;
        double const h0 = keys[k].at - keys[k - 1].at, h1 = keys[k + 1].at - keys[k].at;
        if (h0 <= 1e-12 || h1 <= 1e-12)
            return 0.0;
        double const d0 = (keys[k].*side - keys[k - 1].*side) / h0, d1 = (keys[k + 1].*side - keys[k].*side) / h1;
        if (d0 * d1 <= 0.0)
            return 0.0;
        return 2.0 / (1.0 / d0 + 1.0 / d1);
    };
    double const x0 = keys[j].at, x1 = keys[j + 1].at, h = x1 - x0;
    double const y0 = keys[j].*side, y1 = keys[j + 1].*side;
    if (h <= 1e-12)
        return y1;
    double const t = std::clamp((at - x0) / h, 0.0, 1.0), t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * slope(j) + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * h * slope(j + 1);
}

} // namespace

ThicknessPoint Stroke::sidesAt(double at) const
{
    int const n = int(points.size());
    ThicknessPoint result;
    result.at = at;
    if (thickness.isEmpty()) {
        // As the points say, from one to the next.
        if (n == 0)
            return result;
        int const span = std::max(1, pieces());
        double const place = closed && n >= 2 ? std::fmod(std::fmod(at, span) + span, span) : std::clamp(at, 0.0, double(n - 1));
        int const i = std::clamp(int(std::floor(place)), 0, n - 1);
        StrokePoint const& a = points[i];
        StrokePoint const& b = points[(i + 1) % n];
        double const t = n > 1 ? place - i : 0.0;
        double const pressure = a.pressure + (b.pressure - a.pressure) * t;
        double const lean = a.lean + (b.lean - a.lean) * t;
        result.left = float(pressure * (1.0 + lean));
        result.right = float(pressure * (1.0 - lean));
        return result;
    }
    // By how far along the line: the shape, and the places of the thickness on it.
    bool const loop = closed && n >= 2;
    QVector<double> places;
    QVector<StrokePoint> const shape = shapeWithPlaces(places);
    QVector<double> along(shape.size(), 0.0);
    for (int i = 1; i < shape.size(); ++i)
        along[i] = along[i - 1] + QLineF(shape[i - 1].pos, shape[i].pos).length();
    double const span = pieces();
    double place = loop && span > 0.0 ? std::fmod(std::fmod(at, span) + span, span) : std::clamp(at, 0.0, span);
    double length = 0.0;
    for (int i = 1; i < places.size(); ++i)
        if (place <= places[i] || i + 1 == places.size()) {
            double const step = places[i] - places[i - 1];
            double const t = step > 1e-12 ? std::clamp((place - places[i - 1]) / step, 0.0, 1.0) : 0.0;
            length = along[i - 1] + (along[i] - along[i - 1]) * t;
            break;
        }
    ThicknessPoint found = sidesAlong(keysByLength(places, along), length, loop, along.isEmpty() ? 0.0 : along.last());
    found.at = at;
    return found;
}

QVector<ThicknessPoint> Stroke::keysByLength(QVector<double> const& places, QVector<double> const& along) const
{
    QVector<ThicknessPoint> keys = thickness;
    for (ThicknessPoint& key : keys) {
        double length = along.isEmpty() ? 0.0 : along.last();
        for (int i = 1; i < places.size(); ++i)
            if (key.at <= places[i]) {
                double const step = places[i] - places[i - 1];
                double const t = step > 1e-12 ? std::clamp((key.at - places[i - 1]) / step, 0.0, 1.0) : 0.0;
                length = along[i - 1] + (along[i] - along[i - 1]) * t;
                break;
            }
        if (key.at <= 0.0)
            length = 0.0;
        key.at = length;
    }
    return keys;
}

ThicknessPoint Stroke::sidesAlong(QVector<ThicknessPoint> keys, double length, bool loop, double total)
{
    ThicknessPoint result;
    result.at = length;
    if (keys.isEmpty())
        return result;
    if (loop && total > 0.0) {
        // Round the loop: the last place leads on to the first.
        length = std::fmod(std::fmod(length, total) + total, total);
        ThicknessPoint before = keys.last(), after = keys.first();
        before.at -= total;
        after.at += total;
        keys.prepend(before);
        keys.append(after);
    }
    if (keys.size() == 1 || length <= keys.first().at) {
        result.left = keys.first().left;
        result.right = keys.first().right;
        return result;
    }
    if (length >= keys.last().at) {
        result.left = keys.last().left;
        result.right = keys.last().right;
        return result;
    }
    int j = 0;
    while (j + 2 < keys.size() && keys[j + 1].at <= length)
        ++j;
    result.left = float(std::max(0.0, smoothBetween(keys, j, length, &ThicknessPoint::left)));
    result.right = float(std::max(0.0, smoothBetween(keys, j, length, &ThicknessPoint::right)));
    return result;
}

void Stroke::thicknessAt(double at, float& pressure, float& lean) const
{
    ThicknessPoint const sides = sidesAt(at);
    float const sum = sides.left + sides.right;
    pressure = sum / 2.0f;
    lean = sum > 1e-9f ? (sides.left - sides.right) / sum : 0.0f;
}

float Stroke::pressureAt(double at) const
{
    float pressure = 1.0f, lean = 0.0f;
    thicknessAt(at, pressure, lean);
    return pressure;
}

void Stroke::ownThickness()
{
    if (!thickness.isEmpty() || points.isEmpty())
        return;
    for (int i = 0; i < points.size(); ++i) {
        StrokePoint& p = points[i];
        thickness.push_back({double(i), float(p.pressure * (1.0 + p.lean)), float(p.pressure * (1.0 - p.lean))});
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
}

QVector<QPair<double, ThicknessPoint>> Stroke::thicknessByLength() const
{
    Stroke owned = *this;
    owned.ownThickness();
    Lengths const l = lengthsOf(owned);
    QVector<QPair<double, ThicknessPoint>> out;
    for (ThicknessPoint const& key : std::as_const(owned.thickness))
        out.push_back({l.total > 0.0 ? lengthAtPlace(l, key.at) / l.total : 0.0, key});
    return out;
}

void Stroke::setThicknessByLength(QVector<QPair<double, ThicknessPoint>> const& byLength)
{
    for (StrokePoint& p : points) {
        p.pressure = 1.0f;
        p.lean = 0.0f;
    }
    thickness.clear();
    Lengths const l = lengthsOf(*this);
    for (auto const& [fraction, key] : byLength) {
        ThicknessPoint placed = key;
        placed.at = placeAtLength(l, std::clamp(fraction, 0.0, 1.0) * l.total);
        thickness.push_back(placed);
    }
    std::stable_sort(thickness.begin(), thickness.end(), [](ThicknessPoint const& a, ThicknessPoint const& b) { return a.at < b.at; });
    // Two places at one: the later one goes.
    for (int i = int(thickness.size()) - 1; i > 0; --i)
        if (thickness[i].at - thickness[i - 1].at < 1e-9)
            thickness.removeAt(i);
}

void Stroke::takeThicknessFrom(QVector<StrokePoint> const& drawn)
{
    if (drawn.isEmpty())
        return;
    bool even = true;
    for (StrokePoint const& p : drawn)
        even = even && std::abs(p.pressure - drawn.first().pressure) < 1e-3f && std::abs(p.lean - drawn.first().lean) < 1e-3f;
    if (even) {
        // As thick all along: the points say it.
        thickness.clear();
        for (StrokePoint& p : points) {
            p.pressure = drawn.first().pressure;
            p.lean = drawn.first().lean;
        }
        return;
    }
    QVector<double> along(drawn.size(), 0.0);
    for (int i = 1; i < drawn.size(); ++i)
        along[i] = along[i - 1] + QLineF(drawn[i - 1].pos, drawn[i].pos).length();
    double const total = along.last();
    QVector<QPair<double, ThicknessPoint>> byLength;
    for (int i = 0; i < drawn.size(); ++i) {
        StrokePoint const& p = drawn[i];
        byLength.push_back({total > 0.0 ? along[i] / total : 0.0, {0.0, float(p.pressure * (1.0 + p.lean)), float(p.pressure * (1.0 - p.lean))}});
    }
    setThicknessByLength(byLength);
    simplifyThickness(0.01);
}

void Stroke::simplifyThickness(double tolerance)
{
    if (thickness.size() < 2)
        return;
    // Many places: first those a straight run between their neighbours says (Douglas and Peucker's way).
    if (thickness.size() > 48) {
        QVector<bool> keep(thickness.size(), false);
        keep.first() = keep.last() = true;       // (and the ends always)
        QVector<QPair<int, int>> todo{{0, int(thickness.size()) - 1}};
        while (!todo.isEmpty()) {
            auto const [a, b] = todo.takeLast();
            double worst = -1.0;
            int at = -1;
            for (int k = a + 1; k < b; ++k) {
                double const span = thickness[b].at - thickness[a].at;
                double const t = span > 1e-12 ? (thickness[k].at - thickness[a].at) / span : 0.0;
                double const off = std::max(std::abs(thickness[a].left + (thickness[b].left - thickness[a].left) * t - thickness[k].left),
                                            std::abs(thickness[a].right + (thickness[b].right - thickness[a].right) * t - thickness[k].right));
                if (off > worst) {
                    worst = off;
                    at = k;
                }
            }
            if (at >= 0 && worst > tolerance * 0.5) {
                keep[at] = true;
                todo.push_back({a, at});
                todo.push_back({at, b});
            }
        }
        QVector<ThicknessPoint> kept;
        for (int k = 0; k < thickness.size(); ++k)
            if (keep[k])
                kept.push_back(thickness[k]);
        thickness = kept;
    }
    QVector<double> places;
    QVector<StrokePoint> const shape = shapeWithPlaces(places);
    QVector<double> along(shape.size(), 0.0);
    for (int i = 1; i < shape.size(); ++i)
        along[i] = along[i - 1] + QLineF(shape[i - 1].pos, shape[i].pos).length();
    double const total = along.isEmpty() ? 0.0 : along.last();
    QVector<ThicknessPoint> byLength = keysByLength(places, along);
    for (;;) {
        if (thickness.size() < 2)
            break;
        int best = -1;
        double bestOff = tolerance;
        bool const loop = closed && points.size() >= 2;
        for (int k = 0; k < thickness.size(); ++k) {
            if (!loop && (k == 0 || k + 1 == thickness.size()))
                continue;       // the first and the last stay: what is said of one place is not then said of the whole line
            QVector<ThicknessPoint> without = byLength;
            ThicknessPoint const gone = without.takeAt(k);
            ThicknessPoint const there = sidesAlong(without, gone.at, loop, total);
            double const off = std::max(std::abs(there.left - gone.left), std::abs(there.right - gone.right));
            if (off <= bestOff) {
                bestOff = off;
                best = k;
            }
        }
        if (best < 0)
            break;
        thickness.removeAt(best);
        byLength.removeAt(best);
    }
}

int Stroke::thicknessPointAt(double at, double near)
{
    ownThickness();
    for (int k = 0; k < thickness.size(); ++k)
        if (std::abs(thickness[k].at - at) <= near)
            return k;
    ThicknessPoint key = sidesAt(at);
    key.at = at;
    int k = 0;
    while (k < thickness.size() && thickness[k].at < at)
        ++k;
    thickness.insert(k, key);
    return k;
}
