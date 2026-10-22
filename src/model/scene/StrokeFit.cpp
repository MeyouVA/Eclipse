// Eclipse - the scene: fitting - a run of pen samples becomes a curve through a few points.
#include "model/scene/Scene.h"
#include "model/geometry/Morph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/scene/StrokePrivate.h"

void Stroke::fit(double tolerance, double cornerDegrees)
{
    // Samples on top of one another say nothing and upset the arithmetic.
    QVector<StrokePoint> d;
    for (StrokePoint const& sample : std::as_const(points))
        if (d.isEmpty() || length(sample.pos - d.last().pos) > 1e-6)
            d.push_back(sample);
    int const n = int(d.size());
    if (n < 3) {
        curve = false;
        return;
    }
    tolerance = std::max(tolerance, 1e-4);

    QVector<bool> keep(n, false);
    keep[0] = keep[n - 1] = true;
    QVector<QPair<int, int>> stack{{0, n - 1}};
    while (!stack.isEmpty()) {
        auto const [first, last] = stack.takeLast();
        double far = tolerance;
        int index = -1;
        for (int i = first + 1; i < last; ++i) {
            double const distance = distanceToSegment(d[i].pos, d[first].pos, d[last].pos);
            if (distance > far) {
                far = distance;
                index = i;
            }
        }
        if (index >= 0) {
            keep[index] = true;
            stack.push_back({first, index});
            stack.push_back({index, last});
        }
    }
    QVector<int> kept;
    for (int i = 0; i < n; ++i)
        if (keep[i])
            kept.push_back(i);
    double const cornerCosine = std::cos(std::clamp(cornerDegrees, 0.0, 180.0) * 3.14159265358979323846 / 180.0);
    QVector<int> anchors{0};
    QVector<bool> isCorner{false};
    for (int k = 1; k + 1 < kept.size(); ++k) {
        QPointF const in = d[kept[k]].pos - d[kept[k - 1]].pos, out = d[kept[k + 1]].pos - d[kept[k]].pos;
        double const lengths = length(in) * length(out);
        if (lengths > 0.0 && dot(in, out) / lengths < cornerCosine) {
            anchors.push_back(kept[k]);
            isCorner.push_back(true);
        }
    }
    anchors.push_back(n - 1);
    isCorner.push_back(false);
    {
        // Pressure against distance along the line, simplified the same way: a point where it bends.
        QVector<int> extra;
        QVector<QPair<int, int>> spans;
        for (int k = 0; k + 1 < anchors.size(); ++k)
            spans.push_back({anchors[k], anchors[k + 1]});
        while (!spans.isEmpty()) {
            auto const [first, last] = spans.takeLast();
            double far = 0.08;      // of full pressure
            int index = -1;
            for (int i = first + 1; i < last; ++i) {
                double const along = double(i - first) / (last - first);
                auto offOf = [&](auto member) {
                    return std::abs(double(d[i].*member) - (double(d[first].*member) + (double(d[last].*member) - double(d[first].*member)) * along));
                };
                // (The lean and the opacity are kept the same way.)
                double const off = std::max({offOf(&StrokePoint::pressure), offOf(&StrokePoint::opacity), offOf(&StrokePoint::lean) / 2.0});
                if (off > far) {
                    far = off;
                    index = i;
                }
            }
            if (index >= 0) {
                extra.push_back(index);
                spans.push_back({first, index});
                spans.push_back({index, last});
            }
        }
        for (int index : std::as_const(extra)) {
            int at = 0;
            while (at < anchors.size() && anchors[at] < index)
                ++at;
            anchors.insert(at, index);
            isCorner.insert(at, false);
        }
    }

    double const reach = std::max(tolerance * 3.0, 1e-3);
    QVector<Piece> pieces;
    for (int k = 0; k + 1 < anchors.size(); ++k) {
        int const first = anchors[k], last = anchors[k + 1];
        int towards = last, back = first;
        for (int i : std::as_const(kept)) {
            if (i > first && i < towards)
                towards = i;
            if (i < last && i > back)
                back = i;
        }
        QPointF t1 = unit(d[towards].pos - d[first].pos);
        QPointF t2 = unit(d[back].pos - d[last].pos);
        if (t1.isNull())
            t1 = directionFrom(d, first, last, reach);
        if (t2.isNull())
            t2 = directionFrom(d, last, first, reach);
        if (k > 0 && !isCorner[k])
            t1 = directionThrough(d, first, anchors[k - 1], last, reach);
        if (k + 2 < anchors.size() && !isCorner[k + 1])
            t2 = -directionThrough(d, last, first, anchors[k + 2], reach);
        fitRun(d, first, last, t1, t2, tolerance, reach, pieces, 0);
    }

    // The pieces, end to end, as points with handles.
    QVector<StrokePoint> fitted;
    for (int i = 0; i < pieces.size(); ++i) {
        Piece const& piece = pieces[i];
        if (i == 0) {
            StrokePoint start = d[piece.first];
            start.corner = false;
            start.handles = true;
            start.in = QPointF();
            fitted.push_back(start);
        }
        fitted.last().out = piece.c1 - d[piece.first].pos;
        StrokePoint next = d[piece.last];
        next.corner = false;
        next.handles = true;
        next.in = piece.c2 - d[piece.last].pos;
        next.out = QPointF();
        fitted.push_back(next);
    }
    for (int k = 1; k + 1 < anchors.size(); ++k)
        if (isCorner[k])
            for (StrokePoint& point : fitted)
                if (point.pos == d[anchors[k]].pos)
                    point.corner = true;
    points = fitted;
    curve = true;
}
