#include "model/geometry/FillRegion.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

#include <algorithm>
#include <cmath>
#include <vector>

namespace FillRegion {

// The raster is at most this many pixels on its longer side; larger frames are scaled down.
static constexpr int kMaxRaster = 4096;

// The lines of a drawing as the walls of its areas, as Harmony has them: the middle of each line (paint goes under the line
// up to it, so two thick lines lying against each other still leave the area between their middles open); and, where a line
// ends against another - its tip reaching the other's edge, or within the gap of it - a wall from its end to the other's
// middle, so lines drawn to meet close what they go round.
struct Wall {
    QVector<StrokePoint> line;   // the middle, as drawn
    bool closed = false;
    double width = 0.0;
    bool roundStart = true, roundEnd = true;
};

static double halfAt(Wall const& w, StrokePoint const& p)
{
    return std::max(0.0, w.width * p.pressure / 2.0);
}

static void rasterWalls(QPainter& painter, QVector<Stroke> const& strokes, double gap, double pixel)
{
    QVector<Wall> walls;
    for (Stroke const& stroke : strokes) {
        if (stroke.points.isEmpty())
            continue;
        Wall w;
        w.line = stroke.path();
        w.closed = stroke.closed;
        w.width = std::max(0.0, double(stroke.width));
        w.roundStart = stroke.capStart == Stroke::RoundCap;
        w.roundEnd = stroke.capEnd == Stroke::RoundCap;
        walls.push_back(w);
    }
    // The middles: two pixels of the raster wide, so that a press on one is on it.
    QPen middle(Qt::black, 2.0 * pixel);
    middle.setCapStyle(Qt::RoundCap);
    middle.setJoinStyle(Qt::RoundJoin);
    painter.setPen(middle);
    painter.setBrush(Qt::NoBrush);
    for (Wall const& w : std::as_const(walls)) {
        QPolygonF centre;
        for (StrokePoint const& p : w.line)
            centre << p.pos;
        if (w.closed && centre.size() > 2)
            centre << centre.first();
        if (centre.size() == 1)
            painter.drawPoint(centre.first());
        else
            painter.drawPolyline(centre);
    }
    // The ends that meet another line (or this one, further along): walled on to its middle.
    QPen join(Qt::black, std::max(2.0 * pixel, gap));
    join.setCapStyle(Qt::RoundCap);
    painter.setPen(join);
    for (int j = 0; j < walls.size(); ++j) {
        Wall const& w = walls[j];
        if (w.closed || w.line.size() < 2)
            continue;
        for (int side = 0; side < 2; ++side) {
            StrokePoint const& end = side == 0 ? w.line.first() : w.line.last();
            // A round tip reaches on by half the thickness there; a flat one ends at its point.
            double const reach = ((side == 0 ? w.roundStart : w.roundEnd) ? halfAt(w, end) : 0.0) + gap;
            // (Along its own line, the stretch next to the end is left out: the end is on it, not against it.)
            double ownSkip = halfAt(w, end) * 2.0 + gap + 4.0 * pixel;
            QPointF best;
            double bestGap = 1e300;
            for (int k = 0; k < walls.size(); ++k) {
                Wall const& other = walls[k];
                int const count = int(other.line.size());
                QVector<double> along;
                if (k == j) {
                    along.resize(count);
                    for (int i = 0; i < count; ++i)
                        along[i] = i == 0 ? 0.0 : along[i - 1] + QLineF(other.line[i - 1].pos, other.line[i].pos).length();
                }
                int const pieces = count - 1 + (other.closed && count > 2 ? 1 : 0);
                for (int i = 0; i < pieces; ++i) {
                    StrokePoint const& a = other.line[i];
                    StrokePoint const& b = other.line[(i + 1) % count];
                    if (k == j) {
                        double const total = along.last();
                        double const fromEnd = side == 0 ? std::min(along[i], along[(i + 1) % count]) : total - std::max(along[i], along[(i + 1) % count]);
                        if (fromEnd < ownSkip)
                            continue;
                    }
                    QPointF const ab = b.pos - a.pos;
                    double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
                    double const t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(end.pos - a.pos, ab) / len2, 0.0, 1.0) : 0.0;
                    QPointF const q = a.pos + ab * t;
                    double const half = halfAt(other, a) + (halfAt(other, b) - halfAt(other, a)) * t;
                    double edge = QLineF(end.pos, q).length() - half;
                    // Past a flat end of the other line: it stops square there, so the way round its corner is longer.
                    bool const pastStart = !other.closed && i == 0 && t <= 0.0 && !other.roundStart;
                    bool const pastEnd = !other.closed && i == pieces - 1 && t >= 1.0 && !other.roundEnd;
                    if ((pastStart || pastEnd) && len2 > 0.0) {
                        QPointF const way = (pastStart ? -ab : ab) / std::sqrt(len2);
                        QPointF const off = end.pos - q;
                        double const beyond = QPointF::dotProduct(off, way);
                        double const across = std::abs(off.x() * way.y() - off.y() * way.x());
                        edge = std::hypot(std::max(0.0, beyond), std::max(0.0, across - half));
                    }
                    if (edge < bestGap) {
                        bestGap = edge;
                        best = q;
                    }
                }
            }
            if (bestGap <= reach + 1e-9)
                painter.drawLine(end.pos, best);
        }
    }
}

// Grows the set pixels of `mask` by one pixel in the four directions, `times` times.
static void dilate(std::vector<uchar>& mask, int w, int h, int times)
{
    std::vector<uchar> next;
    for (int t = 0; t < times; ++t) {
        next = mask;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (mask[size_t(y) * w + x])
                    continue;
                if ((x > 0 && mask[size_t(y) * w + x - 1]) || (x + 1 < w && mask[size_t(y) * w + x + 1])
                    || (y > 0 && mask[size_t(y - 1) * w + x]) || (y + 1 < h && mask[size_t(y + 1) * w + x]))
                    next[size_t(y) * w + x] = 1;
            }
        }
        mask.swap(next);
    }
}

QRectF paintArea(Drawing const& drawing, double margin)
{
    QRectF all;
    for (Stroke const& stroke : drawing.strokes)
        all = all.isNull() ? stroke.bounds() : all.united(stroke.bounds());
    for (Fill const& fill : drawing.fills)
        all = all.isNull() ? fill.bounds() : all.united(fill.bounds());
    return all.isNull() ? all : all.adjusted(-margin, -margin, margin, margin);
}

// The middles of lines, to put the places of a traced edge onto: the pieces of every line, in a grid of cells.
class Snapper {
public:
    Snapper(QVector<Stroke> const& strokes, double reach) : m_reach(reach), m_cell(std::max(reach * 2.0, 1.0))
    {
        for (Stroke const& stroke : strokes) {
            QVector<StrokePoint> const run = stroke.path();
            int const count = int(run.size());
            int const pieces = count - 1 + (stroke.closed && count > 2 ? 1 : 0);
            for (int i = 0; i < pieces; ++i) {
                QLineF const piece(run[i].pos, run[(i + 1) % count].pos);
                int const index = int(m_pieces.size());
                m_pieces.push_back(piece);
                QRectF const box = QRectF(piece.p1(), piece.p2()).normalized().adjusted(-reach, -reach, reach, reach);
                for (int cx = cellOf(box.left()); cx <= cellOf(box.right()); ++cx)
                    for (int cy = cellOf(box.top()); cy <= cellOf(box.bottom()); ++cy)
                        m_grid[key(cx, cy)].push_back(index);
            }
        }
    }
    // `p` put on the middle of the nearest line, when one is within reach; whether it was.
    bool onto(QPointF& p) const
    {
        auto const found = m_grid.constFind(key(cellOf(p.x()), cellOf(p.y())));
        if (found == m_grid.constEnd())
            return false;
        double best = m_reach;
        QPointF to;
        bool any = false;
        for (int index : *found) {
            QLineF const& piece = m_pieces[index];
            QPointF const ab = piece.p2() - piece.p1();
            double const len2 = ab.x() * ab.x() + ab.y() * ab.y();
            double const t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - piece.p1(), ab) / len2, 0.0, 1.0) : 0.0;
            QPointF const q = piece.p1() + ab * t;
            double const d = QLineF(p, q).length();
            if (d <= best) {
                best = d;
                to = q;
                any = true;
            }
        }
        if (any)
            p = to;
        return any;
    }

private:
    int cellOf(double v) const { return int(std::floor(v / m_cell)); }
    static qint64 key(int x, int y) { return (qint64(x) << 32) ^ quint32(y); }
    double m_reach, m_cell;
    QVector<QLineF> m_pieces;
    QHash<qint64, QVector<int>> m_grid;
};

// Douglas and Peucker round a closed edge: within `fine` along the stretches that lie on lines, `coarse` elsewhere.
static QPolygonF simplifyEdge(QPolygonF const& ring, QVector<bool> const& onLine, double fine, double coarse)
{
    int const n = int(ring.size());
    if (n < 4)
        return ring;
    std::vector<char> keep(size_t(n), 0);
    int a = 0, b = 0;
    for (int i = 1; i < n; ++i) {
        if (ring[i].x() < ring[a].x())
            a = i;
        if (ring[i].x() > ring[b].x())
            b = i;
    }
    if (a == b)
        return ring;
    keep[size_t(a)] = keep[size_t(b)] = 1;
    std::vector<std::pair<int, int>> todo{{a, b}, {b, a}};
    while (!todo.empty()) {
        auto [first, last] = todo.back();
        todo.pop_back();
        int const count = (last - first + n) % n;
        if (count < 2)
            continue;
        QPointF const p0 = ring[first], p1 = ring[last];
        double const dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
        double const len = std::hypot(dx, dy);
        double worst = -1.0;
        int worstIndex = -1;
        bool allOnLine = onLine[first] && onLine[last];
        for (int k = 1; k < count; ++k) {
            int const i = (first + k) % n;
            allOnLine = allOnLine && onLine[i];
            double const d = len > 0 ? std::abs(dy * (ring[i].x() - p0.x()) - dx * (ring[i].y() - p0.y())) / len
                                     : std::hypot(ring[i].x() - p0.x(), ring[i].y() - p0.y());
            if (d > worst) {
                worst = d;
                worstIndex = i;
            }
        }
        if (worst > (allOnLine ? fine : coarse)) {
            keep[size_t(worstIndex)] = 1;
            todo.push_back({first, worstIndex});
            todo.push_back({worstIndex, last});
        }
    }
    QPolygonF out;
    for (int i = 0; i < n; ++i)
        if (keep[size_t(i)] && (out.isEmpty() || QLineF(out.last(), ring[i]).length() > 1e-6))
            out.push_back(ring[i]);
    return out;
}

Result compute(Drawing const& drawing, QRectF const& area, QPointF point, double gap, QVector<QPolygonF>& outlines)
{
    outlines.clear();
    if (area.width() < 1.0 || area.height() < 1.0 || !area.contains(point))
        return Outside;
    // Brought to where the frame would be, worked out there, and put back.
    QTransform const home = QTransform::fromTranslate(-area.left(), -area.top());
    Drawing moved;
    for (Stroke stroke : drawing.strokes) {
        stroke.transform(home);
        moved.strokes.push_back(stroke);
    }
    for (Fill fill : drawing.fills) {
        fill.transform(home);
        moved.fills.push_back(fill);
    }
    Result const result = compute(moved, int(std::ceil(area.width())), int(std::ceil(area.height())), home.map(point), gap, outlines);
    for (QPolygonF& outline : outlines)
        outline.translate(area.topLeft());
    return result;
}

Result compute(Drawing const& drawing, int width, int height, QPointF point, double gap, QVector<QPolygonF>& outlines)
{
    outlines.clear();
    if (point.x() < 0 || point.y() < 0 || point.x() >= width || point.y() >= height)
        return Outside;

    // Finer than a pixel of the scene for small areas (up to three raster pixels to one), so that narrow places keep open;
    // no more than a few million pixels in all, and no side longer than the most.
    double const scale = std::min({3.0, double(kMaxRaster) / std::max(width, height), std::sqrt(6.0e6 / (double(width) * height))});
    int const w = std::max(1, int(std::ceil(width * scale)));
    int const h = std::max(1, int(std::ceil(height * scale)));

    // Lines in black on white, without antialiasing: a pixel is either a wall or it is not.
    // Thickening every line by the gap size closes openings up to that width.
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.scale(scale, scale);
        rasterWalls(painter, drawing.strokes, gap, 1.0 / scale);
        // Brush strokes are walls too: they are contour shapes, painted solid, and thickened
        // by the gap as the lines are.
        for (Fill const& fill : drawing.fills) {
            if (fill.contours.isEmpty())
                continue;
            QPainterPath shape;
            shape.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& outline : fill.outlines) {
                shape.addPolygon(outline);
                shape.closeSubpath();
            }
            QPen edge(Qt::black, std::max(0.0, gap));
            edge.setJoinStyle(Qt::RoundJoin);
            painter.setPen(gap > 0.0 ? edge : QPen(Qt::NoPen));
            painter.setBrush(Qt::black);
            painter.drawPath(shape);
        }
    }
    auto wall = [&](int x, int y) { return image.constScanLine(y)[x] < 128; };

    int const sx = std::clamp(int(point.x() * scale), 0, w - 1);
    int const sy = std::clamp(int(point.y() * scale), 0, h - 1);
    if (wall(sx, sy))
        return OnLine;

    // Scanline flood fill.
    std::vector<uchar> mask(size_t(w) * h, 0);
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    bool leaked = false;
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        if (mask[size_t(y) * w + x] || wall(x, y))
            continue;
        int l = x, r = x;
        while (l > 0 && !mask[size_t(y) * w + l - 1] && !wall(l - 1, y))
            --l;
        while (r + 1 < w && !mask[size_t(y) * w + r + 1] && !wall(r + 1, y))
            ++r;
        if (l == 0 || r == w - 1 || y == 0 || y == h - 1)
            leaked = true;
        for (int i = l; i <= r; ++i)
            mask[size_t(y) * w + i] = 1;
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            bool inRun = false;
            for (int i = l; i <= r; ++i) {
                bool const open = !mask[size_t(ny) * w + i] && !wall(i, ny);
                if (open && !inRun)
                    stack.push_back({i, ny});
                inRun = open;
            }
        }
    }
    if (leaked)
        return NotClosed;

    // Reach under the lines: by the gap thickening that was added, plus a little more so
    // that no sliver of background shows between the paint and the middle of the line.
    // Up to the middle of the lines round it (and a little past, so that no sliver shows): the lines are drawn over it.
    dilate(mask, w, h, int(std::ceil(scale)) + 1);

    // Mask -> region (one rectangle per run) -> outline polygons.
    QRegion region;
    {
        QVector<QRect> rects;
        for (int y = 0; y < h; ++y) {
            int x = 0;
            while (x < w) {
                if (!mask[size_t(y) * w + x]) {
                    ++x;
                    continue;
                }
                int const start = x;
                while (x < w && mask[size_t(y) * w + x])
                    ++x;
                rects.push_back(QRect(start, y, x - start, 1));
            }
        }
        region.setRects(rects.constData(), int(rects.size()));
    }
    QPainterPath path;
    path.addRegion(region);
    QTransform const toScene = QTransform::fromScale(1.0 / scale, 1.0 / scale);
    // The edge of the paint on the lines themselves, as a vector: each place of the traced edge near the middle of a line
    // goes onto it (the pixels only said which area; the lines say where its edge is).  Elsewhere (a gap closed, the edge
    // of a brush shape) the pixel staircase is smoothed away.
    Snapper const snap(drawing.strokes, (3.0 + std::ceil(scale)) / scale + 0.5);
    for (QPolygonF const& raw : path.simplified().toSubpathPolygons()) {
        QPolygonF ring = toScene.map(raw);
        if (ring.size() > 1 && ring.first() == ring.last())
            ring.removeLast();
        QVector<bool> onLine(ring.size(), false);
        for (int i = 0; i < ring.size(); ++i)
            onLine[i] = snap.onto(ring[i]);
        QPolygonF const poly = simplifyEdge(ring, onLine, 0.04, 0.75 / scale);
        if (poly.size() >= 3)
            outlines.push_back(poly);
    }
    return outlines.isEmpty() ? OnLine : Ok;
}

// Douglas-Peucker, iterative.
QPolygonF simplify(QPolygonF const& polygon, double tolerance)
{
    int n = int(polygon.size());
    if (n > 1 && polygon.first() == polygon.last())
        --n;  // closed: work on the open ring
    if (n < 4)
        return polygon;
    std::vector<char> keep(size_t(n), 0);
    // Split the ring at its two extreme points so that each half is an open polyline.
    int a = 0, b = 0;
    for (int i = 1; i < n; ++i) {
        if (polygon[i].x() < polygon[a].x())
            a = i;
        if (polygon[i].x() > polygon[b].x())
            b = i;
    }
    if (a == b)
        return polygon;
    keep[size_t(a)] = keep[size_t(b)] = 1;
    std::vector<std::pair<int, int>> todo{{a, b}, {b, a}};
    while (!todo.empty()) {
        auto [first, last] = todo.back();
        todo.pop_back();
        int const count = (last - first + n) % n;
        if (count < 2)
            continue;
        QPointF const p0 = polygon[first], p1 = polygon[last];
        double const dx = p1.x() - p0.x(), dy = p1.y() - p0.y();
        double const len = std::hypot(dx, dy);
        double worst = -1.0;
        int worstIndex = -1;
        for (int k = 1; k < count; ++k) {
            int const i = (first + k) % n;
            double const d = len > 0 ? std::abs(dy * (polygon[i].x() - p0.x()) - dx * (polygon[i].y() - p0.y())) / len
                                     : std::hypot(polygon[i].x() - p0.x(), polygon[i].y() - p0.y());
            if (d > worst) {
                worst = d;
                worstIndex = i;
            }
        }
        if (worst > tolerance) {
            keep[size_t(worstIndex)] = 1;
            todo.push_back({first, worstIndex});
            todo.push_back({worstIndex, last});
        }
    }
    QPolygonF out;
    for (int i = 0; i < n; ++i)
        if (keep[size_t(i)])
            out.push_back(polygon[i]);
    return out;
}

}  // namespace FillRegion
