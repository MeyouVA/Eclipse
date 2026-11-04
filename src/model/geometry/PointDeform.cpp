// Eclipse - point deformers: a lattice (Free Form Deformer), a mesh warp, weighted points, lines and curves, rigid points.
#include "model/geometry/Deform.h"

#include "model/geometry/DeformShared.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Deform {

namespace {

QPointF movedOf(QVector<QPointF> const& moved, int i)
{
    return i >= 0 && i < moved.size() ? moved[i] : QPointF();
}

// Catmull-Rom between b and c (a before, d after) at t.
QPointF catmull(QPointF a, QPointF b, QPointF c, QPointF d, double t)
{
    double const t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2.0 * b) + (-a + c) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2 + (-a + 3.0 * b - 3.0 * c + d) * t3);
}

// The moves of the points as they act: each its own, or (Allow Individual Transformation off) all of them by their mean.
QVector<QPointF> actingMoves(PointDeformer const& d, QVector<QPointF> const& moved)
{
    QVector<QPointF> out(d.points.size());
    for (int i = 0; i < out.size(); ++i)
        out[i] = movedOf(moved, i);
    if (!d.individual && !out.isEmpty()) {
        QPointF const mean = std::accumulate(out.begin(), out.end(), QPointF()) / double(out.size());
        out.fill(mean);
    }
    return out;
}

// The grid: the move at a point of the area, straight between the four points round it (lattice) or smoothly (mesh).
QPointF gridMove(PointDeformer const& d, QVector<QPointF> const& moves, QPointF p)
{
    if (d.area.width() <= 0.0 || d.area.height() <= 0.0 || !d.area.contains(p))
        return QPointF();
    double const u = (p.x() - d.area.left()) / d.area.width() * d.cols, v = (p.y() - d.area.top()) / d.area.height() * d.rows;
    int const i = std::clamp(int(std::floor(u)), 0, d.cols - 1), j = std::clamp(int(std::floor(v)), 0, d.rows - 1);
    double const s = u - i, t = v - j;
    auto at = [&](int x, int y) {
        x = std::clamp(x, 0, d.cols);
        y = std::clamp(y, 0, d.rows);
        return moves.value(y * (d.cols + 1) + x);
    };
    if (d.kind == PointDeformer::Lattice) {
        QPointF const top = at(i, j) * (1.0 - s) + at(i + 1, j) * s;
        QPointF const bottom = at(i, j + 1) * (1.0 - s) + at(i + 1, j + 1) * s;
        return top * (1.0 - t) + bottom * t;
    }
    // Mesh: across each of four rows, then down them.
    QPointF across[4];
    for (int k = 0; k < 4; ++k)
        across[k] = catmull(at(i - 1, j - 1 + k), at(i, j - 1 + k), at(i + 1, j - 1 + k), at(i + 2, j - 1 + k), s);
    return catmull(across[0], across[1], across[2], across[3], t);
}

// How strongly a handle so far away pulls (0..1).
double pullAt(PointDeformer const& d, double distance)
{
    if (distance <= d.minDistance)
        return 1.0;
    if (d.maxDistance > 0.0 && distance >= d.maxDistance)
        return 0.0;
    double const past = (distance - d.minDistance) / std::max(0.01, d.blendDistance);
    double pull = 1.0 / (1.0 + past * past);
    if (d.maxDistance > d.minDistance)
        pull *= std::clamp((d.maxDistance - distance) / (d.maxDistance - d.minDistance), 0.0, 1.0);
    return pull;
}

}  // namespace

PointDeformer gridDeformer(int kind, QRectF area, int cols, int rows)
{
    PointDeformer d;
    d.kind = kind == PointDeformer::Mesh ? PointDeformer::Mesh : PointDeformer::Lattice;
    d.cols = std::clamp(cols, 1, 64);
    d.rows = std::clamp(rows, 1, 64);
    d.area = area.normalized();
    for (int y = 0; y <= d.rows; ++y)
        for (int x = 0; x <= d.cols; ++x)
            d.points.push_back(QPointF(d.area.left() + d.area.width() * x / d.cols, d.area.top() + d.area.height() * y / d.rows));
    return d;
}

bool isHandled(PointDeformer const& d, QVector<QPointF> const& moved)
{
    if (!d.any())
        return false;
    for (int i = 0; i < moved.size() && i < d.points.size(); ++i)
        if (!moved[i].isNull())
            return true;
    return false;
}

QVector<QPointF> posedHandles(PointDeformer const& d, QVector<QPointF> const& moved)
{
    QVector<QPointF> out = d.points;
    QVector<QPointF> const moves = actingMoves(d, moved);
    for (int i = 0; i < out.size(); ++i)
        out[i] += moves[i];
    return out;
}

QPolygonF handleLine(PointDeformer const& d, QVector<QPointF> const& points, int group, int perSegment)
{
    QVector<QPointF> run;
    for (int i = 0; i < points.size(); ++i)
        if (d.groupOf(i) == group)
            run.push_back(points[i]);
    QPolygonF line;
    if (run.size() < 2 || d.curveType == PointDeformer::Straight)
        return QPolygonF(run);
    if (d.curveType == PointDeformer::CubicBezier && run.size() >= 4) {
        // Points 0, 3, 6 ... on the curve; those between them its handles.
        for (int k = 0; k + 3 < run.size(); k += 3)
            for (int s = 0; s <= perSegment; ++s) {
                if (k > 0 && s == 0)
                    continue;
                double const t = double(s) / perSegment, u = 1.0 - t;
                line << run[k] * (u * u * u) + run[k + 1] * (3 * u * u * t) + run[k + 2] * (3 * u * t * t) + run[k + 3] * (t * t * t);
            }
        return line;
    }
    for (int k = 0; k + 1 < run.size(); ++k)
        for (int s = 0; s <= perSegment; ++s) {
            if (k > 0 && s == 0)
                continue;
            line << catmull(run[std::max(0, k - 1)], run[k], run[k + 1], run[std::min(int(run.size()) - 1, k + 2)], double(s) / perSegment);
        }
    return line;
}

QPointF handlePoint(PointDeformer const& d, QVector<QPointF> const& moved, QPointF p)
{
    if (!d.any())
        return p;
    QVector<QPointF> const moves = actingMoves(d, moved);
    if (d.kind == PointDeformer::Lattice || d.kind == PointDeformer::Mesh)
        return p + gridMove(d, moves, p);
    if (d.kind == PointDeformer::Rigid) {
        // As rigid as the points allow (moving least squares, rigid): each point weighted by how near it is.
        int const n = int(d.points.size());
        QVector<double> w(n);
        double total = 0.0;
        for (int i = 0; i < n; ++i) {
            QPointF const off = d.points[i] - p;
            double const dd = off.x() * off.x() + off.y() * off.y();
            if (dd < 1e-12)
                return p + moves[i];
            w[i] = 1.0 / dd;
            total += w[i];
        }
        QPointF pStar, qStar;
        for (int i = 0; i < n; ++i) {
            pStar += d.points[i] * w[i];
            qStar += (d.points[i] + moves[i]) * w[i];
        }
        pStar /= total;
        qStar /= total;
        if (n == 1)
            return p + (qStar - pStar);
        double a = 0.0, b = 0.0;
        for (int i = 0; i < n; ++i) {
            QPointF const ph = d.points[i] - pStar, qh = d.points[i] + moves[i] - qStar;
            a += w[i] * (ph.x() * qh.x() + ph.y() * qh.y());
            b += w[i] * (ph.x() * qh.y() - ph.y() * qh.x());
        }
        double const mu = std::hypot(a, b);
        if (mu < 1e-12)
            return p + (qStar - pStar);
        double const c = a / mu, s = b / mu;
        QPointF const v = p - pStar;
        return qStar + QPointF(c * v.x() - s * v.y(), s * v.x() + c * v.y());
    }
    // Weighted: each point, line or curve pulls by how near it is; the nearest so many only, if asked.
    struct Pull {
        double weight;
        double near;
        QPointF move;
    };
    QVector<Pull> pulls;
    QVector<int> seen;
    for (int i = 0; i < d.points.size(); ++i) {
        int const g = d.groupOf(i);
        if (seen.contains(g))
            continue;
        seen.push_back(g);
        QVector<int> members;
        for (int k = 0; k < d.points.size(); ++k)
            if (d.groupOf(k) == g)
                members.push_back(k);
        double distance = 0.0;
        QPointF move;
        if (members.size() == 1) {
            distance = std::hypot(p.x() - d.points[i].x(), p.y() - d.points[i].y());
            move = moves[i];
        } else {
            // The nearest place along the line or curve, and the move there (the same curve through the moved points).
            QVector<QPointF> rest = d.points, posed = d.points;
            for (int k = 0; k < posed.size(); ++k)
                posed[k] += moves[k];
            QPolygonF const at = handleLine(d, rest, g), to = handleLine(d, posed, g);
            distance = 1e300;
            for (int k = 0; k + 1 < at.size() && k + 1 < to.size(); ++k) {
                QPointF const a = at[k], seg = at[k + 1] - at[k];
                double const len2 = seg.x() * seg.x() + seg.y() * seg.y();
                double const t = len2 > 0.0 ? std::clamp(((p - a).x() * seg.x() + (p - a).y() * seg.y()) / len2, 0.0, 1.0) : 0.0;
                QPointF const c = a + seg * t;
                double const dist = std::hypot(p.x() - c.x(), p.y() - c.y());
                if (dist < distance) {
                    distance = dist;
                    move = (to[k] + (to[k + 1] - to[k]) * t) - c;
                }
            }
        }
        pulls.push_back({pullAt(d, distance), distance, move});
    }
    if (d.pointsToConsider > 0 && pulls.size() > d.pointsToConsider) {
        std::sort(pulls.begin(), pulls.end(), [](Pull const& a, Pull const& b) { return a.near < b.near; });
        pulls.resize(d.pointsToConsider);
    }
    double total = 0.0, strongest = 0.0;
    QPointF sum;
    for (Pull const& pull : std::as_const(pulls)) {
        double const w = std::pow(pull.weight, std::max(0.1, d.preBlend));
        total += w;
        sum += pull.move * w;
        strongest = std::max(strongest, pull.weight);
    }
    if (total <= 1e-12)
        return p;
    QPointF const shared = sum / total;
    double const reach = d.postBlend + (1.0 - d.postBlend) * strongest;
    return p + shared * reach;
}

Drawing handled(Drawing const& drawing, PointDeformer const& d, QVector<QPointF> const& moved)
{
    if (!isHandled(d, moved))
        return drawing;
    return mapped(drawing, [&](QPointF p) { return handlePoint(d, moved, p); });
}

}  // namespace Deform
