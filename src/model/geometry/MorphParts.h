// Eclipse - morphing, its parts: lines and outlines sampled evenly, lined up, paired by hints, and drawn part of the way between.
#pragma once

#include "model/geometry/Morph.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace morph_parts {

inline constexpr int kLinePoints = 40;     // a line in between is this many points

inline constexpr int kEdgePoints = 64;     // an outline in between is this many

struct Sample {
    QPointF pos;
    double pressure = 1.0;
};

// `count` places evenly spaced along a path (round it and back to its start when closed).
QVector<Sample> spaced(QVector<Sample> path, int count, bool closed);

QPointF middleOf(QVector<Sample> const& samples);

double apart(QVector<Sample> const& a, QVector<Sample> const& b, int shift, bool reversed);

inline QVector<Sample> aligned(QVector<Sample> const& a, QVector<Sample> const& b, bool closed, QVector<QPair<int, int>> const& pins = {})
{
    int const n = int(a.size());
    if (n == 0 || b.size() != n)
        return b;
    auto missed = [&](int shift, bool reversed) {
        double sum = 0.0;
        for (QPair<int, int> const& pin : pins) {
            int const k = reversed ? (shift - pin.first % n + 2 * n) % n : (pin.first + shift) % n;
            QPointF const d = b[k].pos - b[pin.second].pos;
            sum += d.x() * d.x() + d.y() * d.y();
        }
        return sum;
    };
    int bestShift = 0;
    bool bestReversed = false;
    double best = apart(a, b, 0, false);
    double bestMissed = missed(0, false);
    auto consider = [&](int shift, bool reversed) {
        double const d = apart(a, b, shift, reversed);
        double const m = missed(shift, reversed);
        if (m < bestMissed - 1e-9 || (m <= bestMissed + 1e-9 && d < best - 1e-9)) {
            best = d;
            bestMissed = m;
            bestShift = shift;
            bestReversed = reversed;
        }
    };
    if (closed) {
        for (int shift = 0; shift < n; ++shift) {
            consider(shift, false);
            consider(shift, true);
        }
    } else {
        consider(n - 1, true);      // the other way along
    }
    QVector<Sample> out;
    for (int i = 0; i < n; ++i)
        out.push_back(b[bestReversed ? (bestShift - i % n + 2 * n) % n : (i + bestShift) % n]);
    return out;
}

double mix(double a, double b, double t);

// Which of the second lot each of the first goes with (-1: none), and which of the second are left over.
void pairUp(QVector<QPointF> const& first, QVector<QPointF> const& second, QVector<int>& partner, QVector<int>& left);

void colourBetween(int swatchA, QColor const& a, int swatchB, QColor const& b, double t, int& swatch, QColor& colour);

QVector<Sample> samplesOf(Stroke const& stroke);

QVector<Sample> samplesOf(QPolygonF const& outline);

// The point of a run that lies nearest a place.
int nearestSample(QVector<Sample> const& run, QPointF p);

QVector<QPair<int, int>> pinsFor(QVector<Sample> const& from, QVector<Sample> const& to, QVector<QLineF> const& hints);

// How far a place is from a run of points joined up.
double awayFromRun(QVector<QPointF> const& run, bool closed, QPointF p);

double awayFrom(Stroke const& stroke, QPointF p);

double awayFrom(Fill const& fill, QPointF p);

// A hint is for the shape it lies on, or within this of.
inline constexpr double kHintReach = 25.0;

template <typename Shape>
void pairByHints(QVector<Shape> const& from, QVector<Shape> const& to, QVector<QLineF> const& hints, QVector<int>& partner, QVector<int>& left,
                 QVector<QVector<QLineF>>& hintsOf)
{
    hintsOf = QVector<QVector<QLineF>>(from.size());
    for (QLineF const& hint : hints) {
        int mine = -1, theirs = -1;
        double nearMine = kHintReach, nearTheirs = kHintReach;
        for (int i = 0; i < from.size(); ++i) {
            double const away = awayFrom(from[i], hint.p1());
            if (away <= nearMine) {
                nearMine = away;
                mine = i;
            }
        }
        for (int k = 0; k < to.size(); ++k) {
            double const away = awayFrom(to[k], hint.p2());
            if (away <= nearTheirs) {
                nearTheirs = away;
                theirs = k;
            }
        }
        if (mine < 0 || theirs < 0)
            continue;
        if (partner[mine] != theirs) {
            int const had = partner[mine];
            int const holder = int(partner.indexOf(theirs));
            partner[mine] = theirs;
            if (holder >= 0)
                partner[holder] = had;
        }
        hintsOf[mine].push_back(hint);
    }
    left.clear();
    for (int k = 0; k < to.size(); ++k)
        if (!partner.contains(k))
            left.push_back(k);
}

bool splitTo(Stroke& s, int count);

bool curveBetween(Stroke const& fromCurve, Stroke const& toCurve, double t, Stroke& out);

Stroke strokeBetween(Stroke const& a, Stroke const& b, double t, QVector<QLineF> const& hints = {});

// A line on its way to nothing (or from nothing): drawn towards its middle, and thinner.
Stroke strokeShrunk(Stroke const& stroke, double size);

QPolygonF outlineBetween(QPolygonF const& a, QPolygonF const& b, double t, QVector<QLineF> const& hints = {});

QPolygonF outlineShrunk(QPolygonF const& outline, double size);

Fill fillBetween(Fill const& a, Fill const& b, double t, QVector<QLineF> const& hints = {});

Fill fillShrunk(Fill const& fill, double size);

}  // namespace morph_parts
