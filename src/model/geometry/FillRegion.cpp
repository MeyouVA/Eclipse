#include "model/geometry/FillRegion.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

#include <cmath>
#include <vector>

namespace FillRegion {

// The raster is at most this many pixels on its longer side; larger frames are scaled down.
static constexpr int kMaxRaster = 4096;

static void rasterStroke(QPainter& painter, Stroke const& stroke, double extra)
{
    QPen pen(Qt::black);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    QVector<StrokePoint> const line = stroke.path();
    auto widthAt = [&](int i) { return std::max(1.0, double(stroke.width) * line[i].pressure + extra); };
    if (line.size() == 1) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(line[0].pos, widthAt(0) / 2.0, widthAt(0) / 2.0);
        return;
    }
    painter.setBrush(Qt::NoBrush);
    for (int i = 1; i < line.size(); ++i) {
        pen.setWidthF((widthAt(i - 1) + widthAt(i)) / 2.0);
        painter.setPen(pen);
        painter.drawLine(line[i - 1].pos, line[i].pos);
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

    double const scale = std::min(1.0, double(kMaxRaster) / std::max(width, height));
    int const w = std::max(1, int(std::ceil(width * scale)));
    int const h = std::max(1, int(std::ceil(height * scale)));

    // Lines in black on white, without antialiasing: a pixel is either a wall or it is not.
    // Thickening every line by the gap size closes openings up to that width.
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.scale(scale, scale);
        for (Stroke const& stroke : drawing.strokes)
            if (!stroke.points.isEmpty())
                rasterStroke(painter, stroke, gap);
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
    dilate(mask, w, h, int(std::ceil(gap * scale / 2.0)) + 2);

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
    for (QPolygonF const& raw : path.simplified().toSubpathPolygons()) {
        // The outline follows pixel edges; smooth the staircase away.
        QPolygonF const poly = simplify(toScene.map(raw), 0.75 / scale);
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
