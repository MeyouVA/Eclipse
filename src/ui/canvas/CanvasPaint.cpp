// Eclipse - the Camera view: painting a line, an area, a drawing, a layer with its effects, a whole frame.
#include "ui/canvas/Canvas.h"
#include "model/scene/StrokePrivate.h"

#include <atomic>
#include <QPainter>
#include <QPainterPath>

#include <QHash>
#include <QSet>

#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/canvas/PencilTexture.h"
#include "render/effects/Effects.h"

static void paintTexturedFill(QPainter& painter, QPainterPath const& path, Fill const& fill, QBrush const& color)
{
    QTransform const world = painter.combinedTransform();
    double const ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    double const zoom = std::sqrt(std::abs(world.determinant())) * ratio;
    // How far in from the edge the grain thins out: a few units of the scene, in pixels.
    int const soft = std::clamp(int(std::lround(3.0 * zoom)), 1, 24);
    QRectF const covers = path.boundingRect().adjusted(-2, -2, 2, 2);
    // Only what is in sight is worked out - and a little round it, so that the rim of the view is not taken for an edge.
    QRect const seen = world.mapRect(covers).toAlignedRect().adjusted(-2, -2, 2, 2) & painter.viewport().adjusted(-2 * soft, -2 * soft, 2 * soft, 2 * soft);
    if (seen.isEmpty())
        return;
    int const wide = int(std::ceil(seen.width() * ratio)), high = int(std::ceil(seen.height() * ratio));
    QImage ink(wide, high, QImage::Format_ARGB32_Premultiplied);
    ink.setDevicePixelRatio(ratio);
    ink.fill(Qt::transparent);
    QImage grain = ink;
    for (QImage* const image : {&ink, &grain}) {
        QPainter p(image);
        p.setRenderHints(painter.renderHints());
        p.setRenderHint(QPainter::SmoothPixmapTransform, !fill.textureCrisp);      // (crisp: the grain as it is, not blurred)
        p.translate(-seen.topLeft());
        p.setTransform(world, true);
        if (image == &ink && fill.textureCrisp) {
            p.fillRect(covers, color);      // (crisp: the colour all over; which bits of grain show is settled below)
        } else if (image == &ink) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPath(path);
        } else {
            // (As fine as on a line a dozen units thick.)
            QBrush flat(PencilTexture::paper(fill.texture));
            flat.setTransform(QTransform::fromScale(0.1875, 0.1875));
            p.fillRect(covers, flat);
        }
    }
    // How deep inside the area each pixel is: how much of what is round it is inside (the area, blurred).
    QVector<int> inside(wide * high), sums(wide * high);
    QVector<uchar> shapeAt(wide * high);        // the area itself, pixel by pixel
    {
        QPainterPath const shape = path;
        QImage solid(wide, high, QImage::Format_Alpha8);
        solid.setDevicePixelRatio(ratio);
        solid.fill(0);
        QPainter p(&solid);
        p.setRenderHints(painter.renderHints());
        p.translate(-seen.topLeft());
        p.setTransform(world, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(shape);
        p.end();
        for (int y = 0; y < high; ++y) {
            uchar const* const row = solid.constScanLine(y);
            for (int x = 0; x < wide; ++x) {
                inside[y * wide + x] = row[x];
                shapeAt[y * wide + x] = row[x];
            }
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        int const count = pass == 0 ? wide : high, lines = pass == 0 ? high : wide, step = pass == 0 ? 1 : wide, lineStep = pass == 0 ? wide : 1;
        for (int l = 0; l < lines; ++l) {
            int const* const from = inside.constData() + l * lineStep;
            int* const to = sums.data() + l * lineStep;
            auto at = [&](int i) { return from[std::clamp(i, 0, count - 1) * step]; };
            int sum = 0;
            for (int i = -soft; i <= soft; ++i)
                sum += at(i);
            for (int i = 0; i < count; ++i) {
                to[i * step] = sum / (2 * soft + 1);
                sum += at(i + soft + 1) - at(i - soft);
            }
        }
        inside.swap(sums);
    }
    double const amount = std::clamp(double(fill.textureAmount), 0.0, 1.0);
    if (fill.textureCrisp) {
        QImage const& paper = PencilTexture::paper(fill.texture);
        double const bit = 0.1875;      // a bit of the paper, in units of the scene
        QTransform const back = world.inverted();
        for (int y = 0; y < high; ++y) {
            QRgb* const row = reinterpret_cast<QRgb*>(ink.scanLine(y));
            for (int x = 0; x < wide; ++x) {
                if (row[x] == 0)
                    continue;
                QPointF const place = back.map(QPointF((x + 0.5) / ratio + seen.left(), (y + 0.5) / ratio + seen.top()));
                double const column = std::floor(place.x() / bit), line = std::floor(place.y() / bit);
                QPointF const middle = (world.map(QPointF((column + 0.5) * bit, (line + 0.5) * bit)) - QPointF(seen.topLeft())) * ratio;
                int const mx = std::clamp(int(std::floor(middle.x())), 0, wide - 1), my = std::clamp(int(std::floor(middle.y())), 0, high - 1);
                int const px = ((int(column) % paper.width()) + paper.width()) % paper.width(), py = ((int(line) % paper.height()) + paper.height()) % paper.height();
                double const texture = fill.textureRamp(qAlpha(reinterpret_cast<QRgb const*>(paper.constScanLine(py))[px]) / 255.0);
                double const deep = std::clamp((inside[my * wide + mx] / 255.0 - 0.5) * 2.0, 0.0, 1.0);
                bool const there = shapeAt[my * wide + mx] >= 128 && (deep - 0.55 * (1.0 - texture)) / 0.45 >= 0.2;
                // (What shows of the solid paint under the grain keeps the area's own smooth edge.)
                double const shows = (there ? amount * texture : 0.0) + (1.0 - amount) * shapeAt[y * wide + x] / 255.0;
                QRgb const c = row[x];
                row[x] = qRgba(int(qRed(c) * shows), int(qGreen(c) * shows), int(qBlue(c) * shows), int(qAlpha(c) * shows));
            }
        }
        painter.save();
        painter.resetTransform();
        painter.drawImage(seen.topLeft(), ink);
        painter.restore();
        return;
    }
    for (int y = 0; y < high; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(ink.scanLine(y));
        QRgb const* const grainRow = reinterpret_cast<QRgb const*>(grain.constScanLine(y));
        for (int x = 0; x < wide; ++x) {
            if (row[x] == 0)
                continue;
            // At the very edge half of what is round a pixel is inside; `soft` pixels in, all of it.
            double const deep = std::clamp((inside[y * wide + x] / 255.0 - 0.5) * 2.0, 0.0, 1.0);
            double const texture = fill.textureRamp(qAlpha(grainRow[x]) / 255.0);
            // (The grain itself decides where the edge frays: the thin places give way first.)
            double const edge = std::clamp((deep - 0.55 * (1.0 - texture)) / 0.45, 0.0, 1.0);
            double const shows = amount * texture * edge + (1.0 - amount);
            QRgb const c = row[x];
            row[x] = qRgba(int(qRed(c) * shows), int(qGreen(c) * shows), int(qBlue(c) * shows), int(qAlpha(c) * shows));
        }
    }
    painter.save();
    painter.resetTransform();
    painter.drawImage(seen.topLeft(), ink);
    painter.restore();
}

static void paintTexturedStroke(QPainter& painter, Stroke const& stroke, QBrush const& color)
{
    QVector<StrokePoint> const line = stroke.path();
    if (line.size() < 2) {
        Stroke solid = stroke;
        solid.texture = 0;
        Canvas::paintStroke(painter, solid, color);
        return;
    }
    QTransform const world = painter.combinedTransform();
    double const reach = double(stroke.width) + 2.0;
    QRectF const covers = stroke.bounds().adjusted(-reach, -reach, reach, reach);
    // Only what is in sight is worked out.
    QRect const seen = world.mapRect(covers).toAlignedRect().adjusted(-2, -2, 2, 2) & painter.viewport();
    if (seen.isEmpty())
        return;
    double const ratio = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    int const wide = int(std::ceil(seen.width() * ratio)), high = int(std::ceil(seen.height() * ratio));
    // How much bigger the line is on the screen than in the scene.
    double const zoom = std::sqrt(std::abs(world.determinant())) * ratio;
    double const width = std::max(0.3, double(stroke.width));

    bool const crisp = stroke.textureCrisp;
    QImage const& tile = PencilTexture::tile(stroke.texture, crisp);
    int const tileWide = tile.width(), tileHigh = tile.height();
    double const perUnit = tileHigh / width;        // pixels of the strip to a unit of the scene
    auto grainAt = [&](double along, double across) {
        if (crisp) {
            // The grain as it is, bit by bit: nothing blended between one bit and the next.
            int const column = ((int(std::floor(along * perUnit)) % tileWide) + tileWide) % tileWide;
            int const line = std::clamp(int(std::floor(across * tileHigh)), 0, tileHigh - 1);
            return qAlpha(reinterpret_cast<QRgb const*>(tile.constScanLine(line))[column]) / 255.0;
        }
        double x = along * perUnit - 0.5, y = std::clamp(across * tileHigh - 0.5, 0.0, tileHigh - 1.0);
        double const fx = x - std::floor(x), fy = y - std::floor(y);
        int const x0 = ((int(std::floor(x)) % tileWide) + tileWide) % tileWide, x1 = (x0 + 1) % tileWide;
        int const y0 = int(y), y1 = std::min(y0 + 1, tileHigh - 1);
        QRgb const* const top = reinterpret_cast<QRgb const*>(tile.constScanLine(y0));
        QRgb const* const bottom = reinterpret_cast<QRgb const*>(tile.constScanLine(y1));
        double const a = qAlpha(top[x0]) + (qAlpha(top[x1]) - qAlpha(top[x0])) * fx;
        double const c = qAlpha(bottom[x0]) + (qAlpha(bottom[x1]) - qAlpha(bottom[x0])) * fx;
        return (a + (c - a) * fy) / 255.0;
    };
    auto smooth = [](double from, double to, double x) {
        double const t = std::clamp((x - from) / (to - from), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };

    // For every pixel: how near the line comes, and how opaque the line is there.
    QVector<float> nearest(wide * high, 1e30f);
    QImage mask(wide, high, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    double const amount = std::clamp(double(stroke.textureAmount), 0.0, 1.0);
    int const pieces = int(line.size()) - 1 + (stroke.closed ? 1 : 0);
    double run = 0.0;
    for (int i = 0; i < pieces; ++i) {
        StrokePoint const& from = line[i];
        StrokePoint const& to = line[(i + 1) % line.size()];
        double const length = QLineF(from.pos, to.pos).length();
        if (length < 1e-9)
            continue;
        QPointF const a = (world.map(from.pos) - QPointF(seen.topLeft())) * ratio, b = (world.map(to.pos) - QPointF(seen.topLeft())) * ratio;
        QPointF const ab = b - a;
        double const squared = ab.x() * ab.x() + ab.y() * ab.y();
        if (squared < 1e-12) {
            run += length;
            continue;
        }
        double const onScreen = std::sqrt(squared);
        double const halfA = std::max(0.15, width * from.pressure / 2.0) * zoom, halfB = std::max(0.15, width * to.pressure / 2.0) * zoom;
        double const most = std::max(halfA, halfB) + 1.5;
        int const left = std::max(0, int(std::floor(std::min(a.x(), b.x()) - most))), right = std::min(wide - 1, int(std::ceil(std::max(a.x(), b.x()) + most)));
        int const top = std::max(0, int(std::floor(std::min(a.y(), b.y()) - most))), bottom = std::min(high - 1, int(std::ceil(std::max(a.y(), b.y()) + most)));
        bool const first = !stroke.closed && i == 0, last = !stroke.closed && i == pieces - 1;
        for (int y = top; y <= bottom; ++y) {
            QRgb* const row = reinterpret_cast<QRgb*>(mask.scanLine(y));
            for (int x = left; x <= right; ++x) {
                double const px = x + 0.5 - a.x(), py = y + 0.5 - a.y();
                double const t = (px * ab.x() + py * ab.y()) / squared, on = std::clamp(t, 0.0, 1.0);
                double const dx = px - ab.x() * on, dy = py - ab.y() * on;
                double const distance = std::sqrt(dx * dx + dy * dy);
                double const half = halfA + (halfB - halfA) * on;
                // Past an end of the line: its tip.  A flat one stops there.
                bool const tip = (first && t < 0.0) || (last && t > 1.0);
                double const side = (ab.x() * py - ab.y() * px) / onScreen;
                if (crisp) {
                    double const beyond = t < 0.0 ? -t * onScreen : t > 1.0 ? (t - 1.0) * onScreen : 0.0;      // how far past the end
                    if (std::abs(side) > half || distance >= nearest[y * wide + x] || (tip ? beyond > half : distance > half))
                        continue;
                    if (tip && (t < 0.0 ? stroke.capStart : stroke.capEnd) == Stroke::FlatCap)
                        continue;
                    double const along = run + (tip ? t : on) * length;
                    double grain = stroke.textureRamp(grainAt(along, 0.5 + side / (2.0 * half)));
                    if (tip) {
                        // The middle of the bit of grain, and how far that is from the end point of the line.
                        double const bitAlong = (std::floor(along * perUnit) + 0.5) / perUnit;
                        double const bitBeyond = std::max(0.0, t < 0.0 ? run - bitAlong : bitAlong - (run + length)) * zoom;
                        double const bitSide = ((std::clamp(std::floor((0.5 + side / (2.0 * half)) * tileHigh), 0.0, tileHigh - 1.0) + 0.5) / tileHigh - 0.5) * 2.0 * half;
                        double const away = std::hypot(bitBeyond, bitSide);
                        if (1.0 - away / half - 0.3 * (1.0 - grain) < 0.08)
                            grain = 0.0;        // a round tip frays as the sides do - by whole bits
                    }
                    nearest[y * wide + x] = float(distance);
                    // (What shows of the solid line under the grain keeps the line's own smooth shape.)
                    double const solid = std::clamp(half - distance + 0.5, 0.0, 1.0);
                    int const alpha = int(std::lround((amount * grain + (1.0 - amount) * solid) * 255.0));
                    row[x] = qRgba(alpha, alpha, alpha, alpha);
                    continue;
                }
                if (distance > half + 0.5 || distance >= nearest[y * wide + x])
                    continue;
                if (tip && (t < 0.0 ? stroke.capStart : stroke.capEnd) == Stroke::FlatCap)
                    continue;
                nearest[y * wide + x] = float(distance);
                double const cover = std::clamp(half - distance + 0.5, 0.0, 1.0);
                // Where in the strip: along (on past the end, at a tip), and across.
                double const along = run + (tip ? t : on) * length;
                double grain = stroke.textureRamp(grainAt(along, 0.5 + side / (2.0 * half)));
                if (tip)
                    grain *= smooth(0.0, 0.35, 1.0 - distance / half);      // a round tip thins out as the sides do
                int const alpha = int(std::lround(cover * (amount * grain + (1.0 - amount)) * 255.0));
                row[x] = qRgba(alpha, alpha, alpha, alpha);
            }
        }
        run += length;
    }
    // The colour of the line, where the mask lets it show.
    QImage ink(wide, high, QImage::Format_ARGB32_Premultiplied);
    ink.setDevicePixelRatio(ratio);
    ink.fill(Qt::transparent);
    {
        QPainter p(&ink);
        p.translate(-seen.topLeft());
        p.setTransform(world, true);
        p.fillRect(covers, color);
        p.resetTransform();
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mask.setDevicePixelRatio(ratio);
        p.drawImage(QPoint(0, 0), mask);
    }
    painter.save();
    painter.resetTransform();
    painter.drawImage(seen.topLeft(), ink);
    painter.restore();
}

// A stroke is drawn as round-capped segments whose width follows the pressure.
void Canvas::paintStroke(QPainter& painter, Stroke const& stroke, QBrush const& color)
{
    if (stroke.points.isEmpty() || stroke.width <= 0.0f)
        return;     // nothing drawn; or a line with no thickness, which is not seen
    if (s_lineOpacity < 1.0) {
        double const was = painter.opacity();
        painter.setOpacity(was * s_lineOpacity);
        double const keep = s_lineOpacity;
        s_lineOpacity = 1.0;
        paintStroke(painter, stroke, color);
        s_lineOpacity = keep;
        painter.setOpacity(was);
        return;
    }
    if (stroke.fades()) {
        paintFadingStroke(painter, stroke, color);
        return;
    }
    if (stroke.texture != 0 && stroke.textureAmount > 0.0f && !PencilTexture::tile(stroke.texture).isNull()) {
        paintTexturedStroke(painter, stroke, color);
        return;
    }
    QVector<StrokePoint> const line = stroke.path();
    QPen pen(color, 1.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    auto widthAt = [&](int i) { return std::max(0.3, double(stroke.width) * line[i].pressure); };

    if (line.size() == 1) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        double const r = widthAt(0) / 2.0;
        if (stroke.capStart == Stroke::FlatCap && stroke.capEnd == Stroke::FlatCap)
            painter.drawRect(QRectF(line[0].pos.x() - r, line[0].pos.y() - r, 2 * r, 2 * r));
        else
            painter.drawEllipse(line[0].pos, r, r);
        return;
    }
    painter.setBrush(Qt::NoBrush);
    if (stroke.uniform()) {
        QPainterPath path(line[0].pos);
        int const last = stroke.closed ? int(line.size()) - 1 : int(line.size());  // a closed path ends where it began
        for (int i = 1; i < last; ++i)
            path.lineTo(line[i].pos);
        if (stroke.closed)
            path.closeSubpath();
        pen.setWidthF(widthAt(0));
        pen.setCapStyle(stroke.capStart == Stroke::FlatCap ? Qt::FlatCap : Qt::RoundCap);
        pen.setJoinStyle(stroke.join == Stroke::MitreJoin ? Qt::MiterJoin : stroke.join == Stroke::BevelJoin ? Qt::BevelJoin : Qt::RoundJoin);
        pen.setMiterLimit(Stroke::kMitreLimit / 2.0);
        if (stroke.closed || stroke.capStart == stroke.capEnd) {
            painter.setPen(pen);
            painter.drawPath(path);
            return;
        }
        QPainterPathStroker stroker(pen);
        stroker.setCapStyle(Qt::FlatCap);
        QPainterPath outline = stroker.createStroke(path);
        outline.setFillRule(Qt::WindingFill);
        QPointF const round = stroke.capStart == Stroke::RoundCap ? line.first().pos : line[last - 1].pos;
        outline.addEllipse(round, widthAt(0) / 2.0, widthAt(0) / 2.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(outline);
        return;
    }
    QVector<QPointF> pos;
    QVector<double> half, lean;
    for (StrokePoint const& p : line) {
        if (!pos.isEmpty() && QLineF(pos.last(), p.pos).length() < 1e-6)
            continue;  // a doubled point has no direction
        pos.push_back(p.pos);
        half.push_back(std::max(0.0, double(stroke.width) * p.pressure / 2.0));
        lean.push_back(std::clamp(double(p.lean), -1.0, 1.0));
    }
    // (A line whose thickness lies to one side: its places moved that way first.)
    leanAside(pos, half, lean, stroke.closed);
    int const n = int(pos.size());
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (n < 2) {
        painter.drawEllipse(pos[0], half[0], half[0]);
        return;
    }
    auto unit = [](QPointF v) {
        double const length = std::hypot(v.x(), v.y());
        return length > 0.0 ? v / length : QPointF(1, 0);
    };
    auto normalOf = [](QPointF d) { return QPointF(-d.y(), d.x()); };
    QPainterPath outline, discs;
    outline.setFillRule(Qt::WindingFill);
    discs.setFillRule(Qt::WindingFill);
    QPolygonF left, right;
    for (int i = 0; i < n; ++i) {
        QPointF const in = unit(pos[i] - pos[i > 0 ? i - 1 : i + 1]) * (i > 0 ? 1.0 : -1.0);
        QPointF const out = i + 1 < n ? unit(pos[i + 1] - pos[i]) : in;
        QPointF const n0 = normalOf(i > 0 ? in : out), n1 = normalOf(out);
        double const turn = in.x() * out.x() + in.y() * out.y();  // the cosine of the turn
        QPointF const mid = unit(n0 + n1);
        double const along = mid.x() * n0.x() + mid.y() * n0.y();  // how much the mitre shortens
        bool const corner = i > 0 && i + 1 < n && turn < 0.9;      // more than about 25 degrees
        if (!corner || (stroke.join == Stroke::MitreJoin && along > 1.0 / Stroke::kMitreLimit)) {
            // Along a curve, or a mitre: one point each side, where the two edges meet.
            double const reach = half[i] / std::max(along, 1e-3);
            left.push_back(pos[i] + mid * reach);
            right.push_back(pos[i] - mid * reach);
        } else if (stroke.join == Stroke::MitreJoin) {
            // A mitre that would reach too far: still a point, its very tip trimmed square across at the limit.
            double const reach = half[i] / std::max(along, 1e-6), limit = Stroke::kMitreLimit * half[i];
            double const from = half[i] * along;      // how far out along the middle the edges' ends are
            double const f = (limit - from) / std::max(reach - from, 1e-9);
            for (int side : {1, -1}) {
                QPointF const a = pos[i] + n0 * (side * half[i]), b = pos[i] + n1 * (side * half[i]);
                QPointF const tip = pos[i] + mid * (side * reach);
                QPolygonF& edge = side > 0 ? left : right;
                edge.push_back(a + (tip - a) * f);
                edge.push_back(b + (tip - b) * f);
            }
        } else {
            // A corner cut flat: the end of the one edge, then the start of the other.
            left.push_back(pos[i] + n0 * half[i]);
            left.push_back(pos[i] + n1 * half[i]);
            right.push_back(pos[i] - n0 * half[i]);
            right.push_back(pos[i] - n1 * half[i]);
            if (stroke.join == Stroke::RoundJoin)
                discs.addEllipse(pos[i], half[i], half[i]);  // and rounded over
        }
    }
    QPolygonF shape = left;
    for (int i = int(right.size()) - 1; i >= 0; --i)
        shape.push_back(right[i]);
    outline.addPolygon(shape);
    outline.closeSubpath();
    if (!stroke.closed && stroke.capStart == Stroke::RoundCap)
        discs.addEllipse(pos[0], half[0], half[0]);
    if (!stroke.closed && stroke.capEnd == Stroke::RoundCap)
        discs.addEllipse(pos[n - 1], half[n - 1], half[n - 1]);
    if (stroke.closed && stroke.join == Stroke::RoundJoin)
        discs.addEllipse(pos[0], half[0], half[0]);  // where it closes
    if (color.isOpaque()) {
        painter.drawPath(outline);
        painter.drawPath(discs);
    } else {
        painter.drawPath(outline.united(discs));      // one shape, so that a translucent colour is not doubled
    }
}

void Canvas::paintFadingStroke(QPainter& painter, Stroke const& stroke, QBrush const& color)
{
    QPaintDevice* const device = painter.device();
    if (!device)
        return;
    QTransform const toDevice = painter.transform();
    double const margin = double(stroke.width) * 2.5 + 2.0;
    QRect const area = toDevice.mapRect(stroke.bounds().adjusted(-margin, -margin, margin, margin)).toAlignedRect()
                           .intersected(QRect(0, 0, device->width(), device->height()));
    if (area.isEmpty())
        return;
    qreal const ratio = device->devicePixelRatioF();
    QImage solid(area.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    solid.setDevicePixelRatio(ratio);
    solid.fill(Qt::transparent);
    QImage fade = solid;
    QTransform const toPiece = toDevice * QTransform::fromTranslate(-area.x(), -area.y());
    Stroke whole = stroke;
    for (StrokePoint& point : whole.points)
        point.opacity = 1.0f;
    {
        QPainter onto(&solid);
        onto.setRenderHints(painter.renderHints());
        onto.setTransform(toPiece);
        paintStroke(onto, whole, color);
    }
    {
        QPainter onto(&fade);
        onto.setRenderHint(QPainter::Antialiasing, true);
        onto.setTransform(toPiece);
        onto.setCompositionMode(QPainter::CompositionMode_Source);
        QVector<StrokePoint> const run = stroke.path();
        double most = 0.0;
        for (StrokePoint const& point : run)
            most = std::max(most, double(stroke.width) * point.pressure);
        QPen pen(Qt::black, most * 1.6 + 2.0 / std::max(std::sqrt(std::abs(toDevice.determinant())), 1e-6), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        // Each stretch in short steps (a couple of pixels where it is painted), each as solid as the line is there.
        double const scale = std::sqrt(std::abs(toDevice.determinant()));
        for (int k = 0; k + 1 < run.size(); ++k) {
            double const length = QLineF(run[k].pos, run[k + 1].pos).length() * scale;
            int const steps = std::clamp(int(std::ceil(length / 2.0)), 1, 2000);
            for (int j = 0; j < steps; ++j) {
                double const u0 = double(j) / steps, u1 = double(j + 1) / steps;
                QColor shade(0, 0, 0);
                shade.setAlphaF(std::clamp(run[k].opacity + (run[k + 1].opacity - run[k].opacity) * (u0 + u1) / 2.0, 0.0, 1.0));
                pen.setColor(shade);
                onto.setPen(pen);
                onto.drawLine(run[k].pos + (run[k + 1].pos - run[k].pos) * u0, run[k].pos + (run[k + 1].pos - run[k].pos) * u1);
            }
        }
        if (run.size() == 1) {
            QColor shade(0, 0, 0);
            shade.setAlphaF(std::clamp(double(run[0].opacity), 0.0, 1.0));
            pen.setColor(shade);
            onto.setPen(pen);
            onto.drawPoint(run[0].pos);
        }
    }
    {
        QPainter onto(&solid);
        onto.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        onto.drawImage(QPointF(0, 0), fade);
    }
    painter.save();
    painter.resetTransform();
    painter.drawImage(QPointF(area.topLeft()), solid);
    painter.restore();
}

void Canvas::paintFill(QPainter& painter, Fill const& fill, QBrush const& color)
{
    QPainterPath path;
    // (Auto Fill paint is all its line goes round: where the line crosses itself it is painted, not cut out.)
    path.setFillRule(fill.link != 0 ? Qt::WindingFill : Qt::OddEvenFill);
    for (QPolygonF const& outline : fill.outlines) {
        path.addPolygon(outline);
        path.closeSubpath();
    }
    if (fill.texture != 0 && fill.textureAmount > 0.0f && !PencilTexture::paper(fill.texture).isNull()) {
        paintTexturedFill(painter, path, fill, color);
        return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(path);
}

static void paintDrawingOpaque(QPainter& painter, Scene const& scene, Drawing const& drawing, QColor const* tint)
{
    auto paintPicture = [&painter](Picture const& picture) {
        painter.save();
        painter.setTransform(picture.placement, true);
        if (!picture.clip.isEmpty()) {
            // Only the part inside its outlines shows.
            QPainterPath inside;
            for (QPolygonF const& ring : picture.clip)
                inside.addPolygon(ring);
            painter.setClipPath(inside, Qt::IntersectClip);
        }
        QRectF const box(QPointF(0, 0), QSizeF(picture.size));
        if (picture.image.isNull()) {
            QPen pen(QColor(220, 90, 70));
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box);
            painter.drawLine(box.topLeft(), box.bottomRight());
            painter.drawLine(box.topRight(), box.bottomLeft());
        } else {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(box, picture.image);
        }
        painter.restore();
    };
    bool placed = false;    // pictures placed among the lines and areas of the line art
    for (Picture const& picture : drawing.pictures) {
        if (!picture.painted && picture.order < 0)
            paintPicture(picture);
        placed = placed || (!picture.painted && picture.order >= 0);
    }
    // From the sheet underneath to the one on top: underlay, colour art, lines, overlay.
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Picture const& picture : drawing.pictures)
            if (picture.painted && picture.sheet == sheet)
                paintPicture(picture);
        QVector<Fill> const& fills = drawing.fillsOf(sheet);
        QVector<Stroke> const& strokes = drawing.strokesOf(sheet);
        bool const withPictures = placed && sheet == Drawing::LineSheet;
        // Paint that follows its lines: with its fine edge, on the lines themselves (unless things are moving).
        bool fine = false;
        if (Canvas::s_fineFills)
            for (Fill const& fill : fills)
                fine = fine || (fill.follows && fill.contours.isEmpty());
        QVector<Stroke> const walls = fine ? Canvas::wallsOf(drawing, sheet) : QVector<Stroke>();
        quint64 const key = fine ? Canvas::wallsKey(walls) : 0;
        auto drawFill = [&](Fill const& fill) {
            QBrush const brush = tint ? QBrush(*tint) : scene.brushFor(fill);
            if (!fine || !fill.follows || !fill.contours.isEmpty()) {
                Canvas::paintFill(painter, fill, brush);
                return;
            }
            Fill shown = fill;
            shown.outlines = Canvas::fineOutlinesOf(fill, walls, key);
            Canvas::paintFill(painter, shown, brush);
        };
        if (!withPictures && Drawing::topOrder(fills, strokes) == Drawing::bottomOrder(fills, strokes)) {
            // All in one place in the order (as nearly always): the areas, then the lines over them.
            for (Fill const& fill : fills)
                drawFill(fill);
            for (Stroke const& stroke : strokes)
                Canvas::paintStroke(painter, stroke, tint ? QBrush(*tint) : scene.brushFor(stroke));
            continue;
        }
        // From the bottom of the order up: areas under lines of the same place, each list in its own order.
        struct Item {
            int order, kind, index;
        };
        QVector<Item> items;
        for (int i = 0; i < fills.size(); ++i)
            items.push_back({fills[i].order, 0, i});
        for (int i = 0; i < strokes.size(); ++i)
            items.push_back({strokes[i].order, 1, i});
        if (withPictures)
            for (int i = 0; i < drawing.pictures.size(); ++i)
                if (!drawing.pictures[i].painted && drawing.pictures[i].order >= 0)
                    items.push_back({drawing.pictures[i].order, -1, i});
        std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) {
            return a.order != b.order ? a.order < b.order : a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
        });
        for (Item const& item : std::as_const(items)) {
            if (item.kind < 0)
                paintPicture(drawing.pictures[item.index]);
            else if (item.kind == 0)
                drawFill(fills[item.index]);
            else
                Canvas::paintStroke(painter, strokes[item.index], tint ? QBrush(*tint) : scene.brushFor(strokes[item.index]));
        }
    }
}

void Canvas::paintDrawing(QPainter& painter, Scene const& scene, Drawing const& drawing, double opacity, QColor const* tint)
{
    if (drawing.isEmpty())
        return;
    if (opacity >= 1.0) {
        paintDrawingOpaque(painter, scene, drawing, tint);
        return;
    }
    QPaintDevice* const device = painter.device();
    double const dpr = device->devicePixelRatioF();
    QImage image(QSize(int(device->width() * dpr), int(device->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter off(&image);
        off.setRenderHints(painter.renderHints());
        off.setTransform(painter.transform());
        paintDrawingOpaque(off, scene, drawing, tint);
    }
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(0, 0, image);
    painter.restore();
}

void Canvas::paintSymbol(QPainter& painter, Scene const& scene, int symbolId, int frame)
{
    static int depth = 0;       // symbols inside symbols: so deep and no deeper (and never round in a ring)
    Scene::Symbol const* const symbol = scene.findSymbol(symbolId);
    if (!symbol || depth >= Scene::kSymbolDepth)
        return;
    ++depth;
    Scene const inner = scene.symbolScene(*symbol);
    int const f = std::clamp(frame, 0, inner.frameCount() - 1);
    QTransform const base = painter.transform();
    for (int l : inner.drawOrder(f)) {
        Layer const& layer = inner.layers[l];
        int const index = layer.drawingAt(f);
        if (!inner.enabled(l) || index == Layer::kNoDrawing || !layer.holdsDrawings())
            continue;
        painter.setTransform(inner.worldTransform(l, f) * base);
        Pose const pose = inner.poseOf(l, f);
        paintLayerDrawing(painter, inner, l, f, Deform::ridden(inner, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose)),
                          layer.opacity * pose.opacity, base);
    }
    painter.setTransform(base);
    --depth;
}

// While a layer with a blend mode is painted alone, to be mixed with what is under it as a whole.
static thread_local bool tPaintingToBlend = false;

void Canvas::paintLayerDrawing(QPainter& painter, Scene const& scene, int l, int frame, Drawing const& drawing, double opacity,
                               QTransform const& sceneToDevice, bool everySheet)
{
    Layer const& layer = scene.layers[l];
    if (layer.blend != Layer::BlendNormal && !tPaintingToBlend && opacity > 0.0) {
        // The layer alone in a picture of its own (its lines over its paint as usual), mixed with what is under it once.
        static QPainter::CompositionMode const kModes[Layer::BlendCount] = {
            QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Multiply, QPainter::CompositionMode_Screen,
            QPainter::CompositionMode_Overlay, QPainter::CompositionMode_Darken, QPainter::CompositionMode_Lighten,
            QPainter::CompositionMode_ColorDodge, QPainter::CompositionMode_ColorBurn, QPainter::CompositionMode_HardLight,
            QPainter::CompositionMode_SoftLight, QPainter::CompositionMode_Difference, QPainter::CompositionMode_Exclusion,
            QPainter::CompositionMode_Plus};
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        QImage alone(QSize(int(device->width() * dpr), int(device->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
        alone.setDevicePixelRatio(dpr);
        alone.fill(Qt::transparent);
        {
            QPainter off(&alone);
            off.setRenderHints(painter.renderHints());
            off.setTransform(painter.transform());
            off.setClipRegion(painter.clipRegion(), painter.hasClipping() ? Qt::ReplaceClip : Qt::NoClip);
            tPaintingToBlend = true;
            paintLayerDrawing(off, scene, l, frame, drawing, opacity, sceneToDevice, everySheet);
            tPaintingToBlend = false;
        }
        painter.save();
        painter.resetTransform();
        painter.setCompositionMode(kModes[std::clamp(layer.blend, 0, int(Layer::BlendCount) - 1)]);
        painter.drawImage(0, 0, alone);
        painter.restore();
        return;
    }
    if (!everySheet && layer.hiddenSheets != 0 && !layer.showsSymbol()) {
        // The art layers left out of the picture are not in it.
        Drawing kept = drawing;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            if (!layer.sheetShown(sheet)) {
                kept.fillsOf(sheet).clear();
                kept.strokesOf(sheet).clear();
                kept.pictures.removeIf([sheet](Picture const& p) { return p.painted && p.sheet == sheet; });
            }
        }
        paintLayerDrawing(painter, scene, l, frame, kept, opacity, sceneToDevice, true);
        return;
    }
    bool effects = false;
    for (Effect const& e : layer.effects)
        effects = effects || e.enabled;
    // A layer that shows a symbol: the symbol is painted, on the frame of it the cell says.
    bool const symbol = layer.showsSymbol();
    int const symbolFrame = symbol ? layer.drawingAt(frame) : Layer::kNoDrawing;
    if (symbol && (symbolFrame == Layer::kNoDrawing || opacity <= 0.0))
        return;
    if (symbol && !effects) {
        if (opacity >= 1.0) {
            paintSymbol(painter, scene, layer.symbolId, symbolFrame);
            return;
        }
        // Faint: painted whole in a picture of its own and blended once, as a drawing is.
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        QImage image(QSize(int(device->width() * dpr), int(device->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        {
            QPainter off(&image);
            off.setRenderHints(painter.renderHints());
            off.setTransform(painter.transform());
            paintSymbol(off, scene, layer.symbolId, symbolFrame);
        }
        painter.save();
        painter.resetTransform();
        painter.setOpacity(painter.opacity() * opacity);
        painter.drawImage(0, 0, image);
        painter.restore();
        return;
    }
    if (!effects) {
        paintDrawing(painter, scene, drawing, opacity);
        return;
    }
    if (!symbol && (drawing.isEmpty() || opacity <= 0.0))
        return;
    // The layer alone, in a picture of its own, for the effects to work on.
    QPaintDevice* const device = painter.device();
    double const dpr = device->devicePixelRatioF();
    QSize const size(int(device->width() * dpr), int(device->height() * dpr));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter off(&image);
        off.setRenderHints(painter.renderHints());
        off.setTransform(painter.transform());
        // Particles: the picture is thrown out again and again, each copy where it has got to and as faint as it has grown.
        int thrown = -1;
        for (int i = 0; i < layer.effects.size() && thrown < 0; ++i)
            if (layer.effects[i].enabled && layer.effects[i].type == Effect::Particles)
                thrown = i;
        auto picture = [&] {
            if (symbol)
                paintSymbol(off, scene, layer.symbolId, symbolFrame);
            else
                paintDrawingOpaque(off, scene, drawing, nullptr);
        };
        if (thrown < 0) {
            picture();
        } else {
            Effect const& emitter = layer.effects[thrown];
            double const rate = layer.effectAmount(thrown, layer.poseAt(frame));
            QTransform const now = scene.worldTransform(l, frame);
            QTransform const base = off.transform();
            QImage plain;
            QRectF area;
            QVector<QImage> mixed;
            if (emitter.endColor.isValid()) {
                QRectF const whole(QPointF(0, 0), QSizeF(size) / dpr);
                area = symbol ? whole : base.mapRect(drawing.bounds()).adjusted(-4, -4, 4, 4) & whole;
                if (!area.isEmpty()) {
                    plain = QImage((area.size() * dpr).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
                    plain.setDevicePixelRatio(dpr);
                    plain.fill(Qt::transparent);
                    QPainter once(&plain);
                    once.setRenderHints(painter.renderHints());
                    once.setTransform(base * QTransform::fromTranslate(-area.left(), -area.top()));
                    if (symbol)
                        paintSymbol(once, scene, layer.symbolId, symbolFrame);
                    else
                        paintDrawingOpaque(once, scene, drawing, nullptr);
                    mixed.resize(9);
                }
            }
            auto mixedImage = [&](int step) -> QImage const& {
                if (mixed[step].isNull()) {
                    QImage made = plain.copy();
                    made.setDevicePixelRatio(dpr);
                    QPainter tint(&made);
                    tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    QColor colour = emitter.endColor;
                    colour.setAlphaF(float(step / 8.0));
                    tint.fillRect(QRectF(QPointF(0, 0), area.size()), colour);
                    tint.end();
                    mixed[step] = made;
                }
                return mixed[step];
            };
            QTransform const backToScene = base.isInvertible() ? base.inverted() : QTransform();
            // Let go from the drawing's own shape: places on its artwork, found once (the same every time it is painted).
            QVector<QPointF> places;
            if (emitter.regionShape == 3 && !symbol) {
                QPainterPath art;
                art.setFillRule(Qt::WindingFill);
                for (Fill const& f : drawing.fills)
                    for (QPolygonF const& outline : f.outlines)
                        art.addPolygon(outline);
                for (Stroke const& s : drawing.strokes)
                    for (QPolygonF const& outline : s.outlinePolygons())
                        art.addPolygon(outline);
                QRectF const box = art.boundingRect();
                QPointF const middle = drawing.bounds().center();
                quint32 seed = quint32(emitter.seed) * 2654435761u + 12345u;
                auto next = [&seed] {
                    seed = seed * 1664525u + 1013904223u;
                    return double(seed >> 8) / double(1u << 24);
                };
                for (int tries = 0; tries < 4000 && places.size() < 96 && !box.isEmpty(); ++tries) {
                    QPointF const p(box.left() + next() * box.width(), box.top() + next() * box.height());
                    if (art.contains(p))
                        places.push_back(p - middle);
                }
            }
            std::function<QPainterPath(int)> shapeFor;
            int const wall = emitter.matte != 0 ? scene.layerIndex(emitter.matte) : -1;
            QPainterPath obstacle;
            if (wall >= 0 && wall != l) {
                int const index = scene.layers[wall].drawingAt(frame);
                if (index != Layer::kNoDrawing) {
                    Drawing const shown = Deform::shown(scene.layers[wall].drawings[index], scene.layers[wall], scene.layers[wall].poseAt(frame));
                    obstacle.setFillRule(Qt::WindingFill);
                    for (Fill const& f : shown.fills)
                        for (QPolygonF const& outline : f.outlines)
                            obstacle.addPolygon(outline);
                    for (Stroke const& s : shown.strokes)
                        for (QPolygonF const& outline : s.outlinePolygons())
                            obstacle.addPolygon(outline);
                    obstacle = scene.worldTransform(wall, frame).map(obstacle);
                }
            }
            if (!obstacle.isEmpty()) {
                QPointF const middle = drawing.bounds().center();
                shapeFor = [&, middle](int born) {
                    QTransform const at = scene.worldTransform(l, emitter.followLayer ? frame : born);
                    if (!at.isInvertible())
                        return QPainterPath();
                    return at.inverted().map(obstacle).translated(-middle);
                };
            }
            for (Effect::Particle const& particle : emitter.particles(rate, frame, places, shapeFor)) {
                off.save();
                // Each stays where the layer was when it was let go (unless it is to move with the layer).
                if (!emitter.followLayer && particle.born != frame && now.isInvertible())
                    off.setTransform(scene.worldTransform(l, particle.born) * now.inverted() * base);
                off.translate(particle.offset);
                if (particle.scale != 1.0 || particle.angle != 0.0) {
                    // Grown or shrunk, and turned, about the middle of the drawing.
                    QPointF const middle = drawing.bounds().center();
                    off.translate(middle);
                    off.rotate(particle.angle);
                    off.scale(particle.scale, particle.scale);
                    off.translate(-middle);
                }
                off.setOpacity(particle.opacity);
                int const step = int(std::lround(std::clamp(particle.tint, 0.0, 1.0) * 8.0));
                if (step > 0 && !plain.isNull()) {
                    off.setTransform(QTransform::fromTranslate(area.left(), area.top()) * backToScene * off.transform());
                    off.drawImage(QPointF(0, 0), mixedImage(step));
                } else {
                    picture();
                }
                off.restore();
            }
        }
    }
    // A blur is so many scene units wide: on screen that depends on the zoom.
    double const pixelsPerUnit = std::sqrt(std::abs(sceneToDevice.determinant())) * dpr;
    static thread_local int tMatteDepth = 0;
    auto paintMatte = [&](QPainter& mp, int m, int index) {
        Layer const& matteLayer = scene.layers[m];
        bool generated = false;
        for (Effect const& me : matteLayer.effects)
            generated = generated || (me.enabled && me.type == Effect::MatteGenerator);
        Drawing const shownMatte = Deform::shown(matteLayer.drawings[index], matteLayer, matteLayer.poseAt(frame));
        if (!generated || tMatteDepth >= 2) {
            paintDrawingOpaque(mp, scene, shownMatte, nullptr);
            return;
        }
        ++tMatteDepth;
        paintLayerDrawing(mp, scene, m, frame, shownMatte, 1.0, sceneToDevice);
        --tMatteDepth;
    };
    Pose const pose = layer.poseAt(frame);
    for (int i = 0; i < layer.effects.size(); ++i) {
        Effect const& e = layer.effects[i];
        if (!e.enabled)
            continue;
        double const amount = layer.effectAmount(i, pose);
        if ((e.type == Effect::Blur && !blursEnabled()) || (e.type == Effect::Focus && !focusEnabled())) {
            // (Switched off in the preferences: the picture goes through as it is.)
        } else if (e.type == Effect::Blur) {
            Effects::blur(image, amount * pixelsPerUnit);
        } else if (e.type == Effect::Focus) {
            // Out of focus as far as the layer stands from the depth that is sharp.
            Effects::blur(image, amount * std::abs(scene.depthOf(l, frame) - e.direction) / 100.0 * pixelsPerUnit);
        } else if (e.type == Effect::Tint) {
            Effects::tint(image, e.color, amount);
        } else if (e.type == Effect::Transparency) {
            Effects::transparency(image, amount);
        } else if (e.type == Effect::DirectionalBlur) {
            // (A direction on the picture, as a shadow's: it does not turn with the layer.)
            Effects::directionalBlur(image, amount * pixelsPerUnit, e.direction);
        } else if (e.type == Effect::HueSaturation) {
            Effects::hueSaturation(image, e.hue, e.saturation, e.lightness, amount);
        } else if (e.type == Effect::BrightnessContrast) {
            Effects::brightnessContrast(image, e.brightness, e.contrast, amount);
        } else if (e.type == Effect::Grain) {
            Effects::grain(image, amount, quint32(e.seed) * 7919u + quint32(frame));     // a new grain each frame
        } else if (e.type == Effect::RadialBlur) {
            Effects::radialBlur(image, amount);
        } else if (e.type == Effect::ColourScale) {
            Effects::colourScale(image, e.color, amount);
        } else if (e.type == Effect::Negate) {
            Effects::negate(image, amount);
        } else if (e.type == Effect::Median) {
            Effects::median(image, amount * pixelsPerUnit);
        } else if (e.type == Effect::Ripple) {
            // (Waves across the picture, as a shadow's offset: they do not turn with the layer.)
            Effects::ripple(image, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
        } else if (e.type == Effect::Glow || e.type == Effect::Shadow) {
            // The offset is a direction on the picture: it does not turn with the layer.
            Effects::halo(image, e.color, amount * pixelsPerUnit, e.type == Effect::Shadow ? e.offset * (pixelsPerUnit / dpr) : QPointF());
        } else if (e.type == Effect::MatteGenerator) {
            Effects::matteGenerator(image, e.direction * pixelsPerUnit, amount * pixelsPerUnit, e.color);
        } else if (e.type == Effect::Bloom) {
            Effects::bloom(image, amount * pixelsPerUnit);
        } else if (e.type == Effect::Pixelate) {
            Effects::pixelate(image, amount * pixelsPerUnit);
        } else if (e.type == Effect::Turbulence) {
            Effects::turbulence(image, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
        } else if (e.type == Effect::Gradient) {
            Effects::gradient(image, e.color, e.endColor, e.direction, e.invert, amount);
        } else if (e.type == Effect::BlackWhite) {
            Effects::blackWhite(image, amount);
        } else if (e.type == Effect::MatteResize) {
            Effects::matteResize(image, amount * pixelsPerUnit, e.invert);
        } else if (e.type == Effect::FieldChart) {
            Effects::fieldChart(image, QTransform::fromScale(dpr, dpr).mapRect(sceneToDevice.mapRect(QRectF(0, 0, scene.width, scene.height))), e.color,
                                amount * dpr);
        } else if (e.type == Effect::MotionBlur && !symbol) {
            int const back = std::clamp(int(std::lround(amount)), 0, 60);
            QImage trail(size, QImage::Format_ARGB32_Premultiplied);
            trail.setDevicePixelRatio(dpr);
            trail.fill(Qt::transparent);
            {
                QPainter tp(&trail);
                tp.setRenderHints(painter.renderHints());
                for (int k = back; k >= 1; --k) {
                    int const f = frame - k;
                    int const index = f >= 0 ? layer.drawingAt(f) : Layer::kNoDrawing;
                    if (index == Layer::kNoDrawing)
                        continue;
                    tp.setOpacity(0.6 * (1.0 - double(k) / (back + 1)));
                    tp.setTransform(scene.worldTransform(l, f) * sceneToDevice);
                    Pose const then = scene.poseOf(l, f);
                    paintDrawingOpaque(tp, scene, Deform::ridden(scene, l, f, Deform::shown(layer.drawings[index], layer, then)), nullptr);
                }
                tp.setOpacity(1.0);
                tp.resetTransform();
                tp.drawImage(0, 0, image);
            }
            image = trail;
        } else if (e.type == Effect::ColourCurves) {
            Effects::colourCurves(image, e.curves, amount);
        } else if (e.type == Effect::ApplyPeg) {
            // The peg's motion, in the picture's pixels: back to the scene, moved as the peg is, onto the picture again.
            if (int const peg = scene.layerIndex(e.matte); peg >= 0 && peg != l) {
                QTransform const toDevice = sceneToDevice * QTransform::fromScale(dpr, dpr);
                Effects::transformImage(image, toDevice.inverted() * scene.worldTransform(peg, frame) * toDevice);
            }
        } else if (e.type == Effect::ScaleOutput) {
            Effects::scaleOutput(image, amount);
        } else if (e.type == Effect::Script) {
            Effects::runScript(image, e.text, frame, amount, pixelsPerUnit);
        } else if (e.type == Effect::External) {
            Effects::runExternal(image, e.text, frame);
        } else if (e.type == Effect::ChannelSelector) {
            Effects::channelSelect(image, e.channel);
        } else if (e.type == Effect::Crop) {
            // (The area is in scene units: where it is in the picture.)
            if (!e.area.isEmpty())
                Effects::crop(image, QTransform::fromScale(dpr, dpr).mapRect(sceneToDevice.mapRect(e.area)));
        } else if (e.type == Effect::RemoveTransparency) {
            Effects::removeTransparency(image, amount);
        } else if (e.type == Effect::MatteComposite) {
            Effects::matteComposite(image);
        } else if (e.type == Effect::VariableBlur) {
            int const m = scene.layerIndex(e.matte);
            if (m < 0)
                continue;  // no matte: nothing blurred
            QImage where(size, QImage::Format_ARGB32_Premultiplied);
            where.setDevicePixelRatio(dpr);
            where.fill(Qt::transparent);
            if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
                QPainter mp(&where);
                mp.setRenderHints(painter.renderHints());
                mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
                paintMatte(mp, m, index);
            }
            Effects::variableBlur(image, where, amount * pixelsPerUnit);
        } else if (e.type == Effect::MatteBlur) {
            int const m = scene.layerIndex(e.matte);
            if (m < 0)
                continue;  // no matte: nowhere to blur
            QImage where(size, QImage::Format_ARGB32_Premultiplied);
            where.setDevicePixelRatio(dpr);
            where.fill(e.invert ? Qt::black : Qt::transparent);
            if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
                QPainter mp(&where);
                mp.setRenderHints(painter.renderHints());
                if (e.invert)
                    mp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
                paintMatte(mp, m, index);
            }
            // The change from sharp to blurred soft-edged as well.
            Effects::blur(where, std::max(1.0, amount * pixelsPerUnit / 3.0));
            QImage blurred = image;
            Effects::blur(blurred, amount * pixelsPerUnit);
            {
                QPainter bp(&blurred);
                bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                bp.drawImage(0, 0, where);
            }
            {
                QPainter ip(&image);
                ip.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                ip.drawImage(0, 0, where);
                ip.setCompositionMode(QPainter::CompositionMode_Plus);
                ip.drawImage(0, 0, blurred);
            }
        } else if (e.type == Effect::Tone || e.type == Effect::Highlight) {
            int const m = scene.layerIndex(e.matte);
            if (m < 0)
                continue;  // no matte: nowhere to shade
            QImage where(size, QImage::Format_ARGB32_Premultiplied);
            where.setDevicePixelRatio(dpr);
            where.fill(Qt::transparent);
            if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
                QPainter mp(&where);
                mp.setRenderHints(painter.renderHints());
                mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
                paintMatte(mp, m, index);
            }
            if (e.invert) {
                // Outside the matte: the rest of the picture.
                QImage outside(size, QImage::Format_ARGB32_Premultiplied);
                outside.setDevicePixelRatio(dpr);
                outside.fill(Qt::black);
                QPainter op(&outside);
                op.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                op.drawImage(0, 0, where);
                op.end();
                where = outside;
            }
            Effects::blur(where, amount * pixelsPerUnit);
            // The layer in the shade (or the light), then only where the matte says, over the layer as it is.
            QImage shaded = image;
            {
                QPainter sp(&shaded);
                sp.setCompositionMode(e.type == Effect::Tone ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_Screen);
                sp.fillRect(shaded.rect(), QColor(e.color.red(), e.color.green(), e.color.blue()));
                sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                sp.drawImage(0, 0, image);
                sp.drawImage(0, 0, where);
            }
            QPainter onto(&image);
            onto.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            onto.drawImage(0, 0, shaded);
        } else if (e.type == Effect::Cutter) {
            int const m = scene.layerIndex(e.matte);
            if (m < 0)
                continue;  // no matte: nothing is cut
            QImage matte(size, QImage::Format_ARGB32_Premultiplied);
            matte.setDevicePixelRatio(dpr);
            matte.fill(Qt::transparent);
            int const index = scene.layers[m].drawingAt(frame);
            if (index != Layer::kNoDrawing) {
                QPainter mp(&matte);
                mp.setRenderHints(painter.renderHints());
                mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
                paintMatte(mp, m, index);
            }
            QPainter cut(&image);
            cut.setCompositionMode(e.invert ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_DestinationIn);
            cut.drawImage(0, 0, matte);
        }
    }
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(0, 0, image);
    painter.restore();
}

static QRgb blendPixel(QRgb s, QRgb d, int mode)
{
    double const sa = qAlpha(s) / 255.0, da = qAlpha(d) / 255.0;
    if (sa <= 0.0)
        return d;
    auto mix = [mode](double cs, double cb) {
        auto hard = [](double a, double b) { return a <= 0.5 ? b * 2.0 * a : b + (2.0 * a - 1.0) - b * (2.0 * a - 1.0); };
        switch (mode) {
        case Layer::BlendMultiply: return cs * cb;
        case Layer::BlendScreen: return cs + cb - cs * cb;
        case Layer::BlendOverlay: return hard(cb, cs);
        case Layer::BlendDarken: return std::min(cs, cb);
        case Layer::BlendLighten: return std::max(cs, cb);
        case Layer::BlendColourDodge: return cb <= 0.0 ? 0.0 : cs >= 1.0 ? 1.0 : std::min(1.0, cb / (1.0 - cs));
        case Layer::BlendColourBurn: return cb >= 1.0 ? 1.0 : cs <= 0.0 ? 0.0 : 1.0 - std::min(1.0, (1.0 - cb) / cs);
        case Layer::BlendHardLight: return hard(cs, cb);
        case Layer::BlendSoftLight: {
            double const dd = cb <= 0.25 ? ((16.0 * cb - 12.0) * cb + 4.0) * cb : std::sqrt(cb);
            return cs <= 0.5 ? cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb) : cb + (2.0 * cs - 1.0) * (dd - cb);
        }
        case Layer::BlendDifference: return std::abs(cb - cs);
        case Layer::BlendExclusion: return cb + cs - 2.0 * cb * cs;
        case Layer::BlendAdd: return std::min(1.0, cs + cb);
        default: return cs;
        }
    };
    auto one = [&](int sc, int dc) {
        double const cs = sc / 255.0 / sa, cb = da > 0.0 ? dc / 255.0 / da : 0.0;
        double const v = sc / 255.0 * (1.0 - da) + dc / 255.0 * (1.0 - sa) + sa * da * std::clamp(mix(std::min(cs, 1.0), std::min(cb, 1.0)), 0.0, 1.0);
        return std::clamp(int(std::lround(v * 255.0)), 0, 255);
    };
    int const a = std::clamp(int(std::lround((sa + da - sa * da) * 255.0)), 0, 255);
    return qRgba(std::min(one(qRed(s), qRed(d)), a), std::min(one(qGreen(s), qGreen(d)), a), std::min(one(qBlue(s), qBlue(d)), a), a);
}

namespace {
std::atomic<bool> gBlurs{true};
std::atomic<bool> gFocus{true};
}

void Canvas::setBlursEnabled(bool on)
{
    gBlurs = on;
}

bool Canvas::blursEnabled()
{
    return gBlurs;
}

void Canvas::setFocusEnabled(bool on)
{
    gFocus = on;
}

bool Canvas::focusEnabled()
{
    return gFocus;
}

void Canvas::paintFrame(QPainter& painter, Scene const& scene, int frame)
{
    painter.fillRect(QRectF(0, 0, scene.width, scene.height), scene.background);
    // Wired by hand in the Node View: the graph says what the frame is.
    if (!scene.graph.isEmpty() && paintGraph(painter, scene, frame))
        return;
    QTransform const base = painter.transform();
    QTransform const camera = scene.cameraTransform(frame);
    QVector<int> const order = scene.drawOrder(frame);
    bool turnedAny = false;
    int drawn = 0;
    for (int l : order) {
        Layer const& layer = scene.layers[l];
        if (!scene.enabled(l) || (layer.kind != Layer::ColourCard && layer.drawingAt(frame) == Layer::kNoDrawing))
            continue;
        ++drawn;
        turnedAny = turnedAny || (layer.kind != Layer::ColourCard && scene.turned(l, frame));
    }
    if (turnedAny && drawn >= 2 && painter.device()) {
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        int const wide = int(device->width() * dpr), high = int(device->height() * dpr);
        QImage built(wide, high, QImage::Format_ARGB32_Premultiplied);
        built.fill(Qt::transparent);
        {
            // The ground of the frame in it too, for a layer with a blend mode to be mixed with.
            built.setDevicePixelRatio(dpr);
            QPainter ground(&built);
            ground.setTransform(base);
            ground.fillRect(QRectF(0, 0, scene.width, scene.height), scene.background);
            built.setDevicePixelRatio(1.0);
        }
        QVector<float> nearest(qsizetype(wide) * high, -std::numeric_limits<float>::infinity());
        for (int l : order) {
            Layer const& layer = scene.layers[l];
            int const index = layer.drawingAt(frame);
            bool const card = layer.kind == Layer::ColourCard;
            if (!scene.enabled(l) || (!card && index == Layer::kNoDrawing))
                continue;
            QImage alone(wide, high, QImage::Format_ARGB32_Premultiplied);
            alone.setDevicePixelRatio(dpr);
            alone.fill(Qt::transparent);
            QTransform const onto = card ? base : scene.worldTransform(l, frame) * camera * base;
            {
                QPainter one(&alone);
                one.setRenderHints(painter.renderHints());
                if (card) {
                    QColor colour = layer.cardColor;
                    colour.setAlphaF(colour.alphaF() * float(layer.opacity));
                    one.setTransform(base);
                    one.fillRect(QRectF(0, 0, scene.width, scene.height), colour);
                } else {
                    one.setTransform(onto);
                    Pose const pose = scene.poseOf(l, frame);
                    paintLayerDrawing(one, scene, l, frame,
                                      Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose)),
                                      layer.opacity * pose.opacity, camera * base);
                }
            }
            double a = 0.0, b = 0.0, c = scene.depthOf(l, frame);
            if (!card)
                scene.depthPlane(l, frame, a, b, c);
            QTransform const back = onto.isInvertible() ? onto.inverted() : QTransform();
            int const blend = card ? int(Layer::BlendNormal) : layer.blend;
            for (int y = 0; y < high; ++y) {
                QRgb const* const from = reinterpret_cast<QRgb const*>(alone.constScanLine(y));
                QRgb* const to = reinterpret_cast<QRgb*>(built.scanLine(y));
                float* const depth = nearest.data() + qsizetype(y) * wide;
                for (int x = 0; x < wide; ++x) {
                    QRgb const s = from[x];
                    int const sa = qAlpha(s);
                    if (sa == 0)
                        continue;
                    float z = float(c);
                    if (a != 0.0 || b != 0.0) {
                        QPointF const local = back.map(QPointF((x + 0.5) / dpr, (y + 0.5) / dpr));
                        z = float(a * local.x() + b * local.y() + c);
                    }
                    QRgb const d = to[x];
                    if (z >= depth[x] - 0.5f && blend != Layer::BlendNormal) {
                        // In front of what is there, and mixed with it as the layer's blend mode says.
                        to[x] = blendPixel(s, d, blend);
                        if (sa > 127)
                            depth[x] = z;
                    } else if (z >= depth[x] - 0.5f) {
                        // In front of what is there: over it.
                        int const keep = 255 - sa;
                        to[x] = qRgba(qRed(s) + qRed(d) * keep / 255, qGreen(s) + qGreen(d) * keep / 255, qBlue(s) + qBlue(d) * keep / 255, sa + qAlpha(d) * keep / 255);
                        if (sa > 127)
                            depth[x] = z;
                    } else {
                        // Behind it: under it.
                        int const keep = 255 - qAlpha(d);
                        to[x] = qRgba(qRed(d) + qRed(s) * keep / 255, qGreen(d) + qGreen(s) * keep / 255, qBlue(d) + qBlue(s) * keep / 255, qAlpha(d) + sa * keep / 255);
                    }
                }
            }
        }
        built.setDevicePixelRatio(dpr);
        painter.save();
        painter.resetTransform();
        painter.drawImage(0, 0, built);
        painter.restore();
        painter.setTransform(base);
        return;
    }
    // The furthest first, so that what is nearer the camera covers it.
    for (int l : order) {
        Layer const& layer = scene.layers[l];
        int const index = layer.drawingAt(frame);
        if (layer.kind == Layer::ColourCard && scene.enabled(l)) {
            // The whole frame, whatever the camera does.
            QColor colour = layer.cardColor;
            colour.setAlphaF(colour.alphaF() * float(layer.opacity));
            painter.setTransform(base);
            painter.fillRect(QRectF(0, 0, scene.width, scene.height), colour);
            continue;
        }
        if (!scene.enabled(l) || index == Layer::kNoDrawing)
            continue;
        painter.setTransform(scene.worldTransform(l, frame) * camera * base);
        Pose const pose = scene.poseOf(l, frame);
        paintLayerDrawing(painter, scene, l, frame,
                          Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose)),
                          layer.opacity * pose.opacity, camera * base);
    }
    painter.setTransform(base);
}

Drawing Canvas::previewDrawing() const
{
    Drawing const* const current = currentDrawing();
    Drawing shown = current ? *current : Drawing();
    if (m_drag == DragSegment && current && contourLine(shown)) {
        Stroke& line = *contourLine(shown);
        line = bentSegment(line, m_segIndex, m_segT, m_dragNow - m_dragStart);
        contourEdited(shown);
    }
    if (m_drag == DragCeBox && contourValid()) {
        // The points picked scaled or turned by the box round them; their handles with them.
        QTransform const t = contourBoxTransform();
        QTransform const linear(t.m11(), t.m12(), t.m21(), t.m22(), 0.0, 0.0);
        Stroke& line = *contourLine(shown);
        QVector<int> moved = contourPointsPicked();
        moved.push_back(m_cePoint);
        for (int i : std::as_const(moved)) {
            StrokePoint& point = line.points[i];
            point.pos = t.map(point.pos);
            if (point.handles) {
                point.in = linear.map(point.in);
                point.out = linear.map(point.out);
            }
        }
        contourEdited(shown);
    }
    if (m_drag == DragPoint && contourValid()) {
        QVector<QPair<int, int>> const shared = sharedPoints(shown);
        Stroke& line = *contourLine(shown);
        if (line.curve)
            line.freezeHandles();
        QPointF const by = m_dragNow - line.points[m_cePoint].pos;
        // Points of lines that lie on it are one point with it: they go where it goes.
        for (auto const& [stroke, point] : shared)
            shown.strokes[stroke].points[point].pos += by;
        line.points[m_cePoint].pos = m_dragNow;
        for (int other : contourPointsPicked())     // the others picked go along
            line.points[other].pos += by;
        contourEdited(shown);
    }
    if (m_drag == DragHandle && contourValid() && m_ceHandle == 2 && pulledHandles()) {
        QVector<QPair<int, int>> const stacked = stackedPoints(shown);
        QPointF const reach = m_dragNow - contourLine(shown)->points[m_cePoint].pos;
        double const pull = std::hypot(reach.x(), reach.y());
        bool first = true;
        for (auto const& [line, point] : stacked) {
            Stroke& stroke = line < 0 || m_ceFill >= 0 ? *contourLine(shown) : shown.strokes[line];
            if (first) {
                first = false;
                stroke.setHandles(point, -reach, reach);
                stroke.points[point].corner = false;
                continue;
            }
            int const n = int(stroke.points.size());
            QPointF const at = stroke.points[point].pos;
            auto along = [&](int neighbour) {
                if (neighbour < 0 || neighbour >= n)
                    return QPointF();
                QPointF const way = stroke.points[neighbour].pos - at;
                double const length = std::hypot(way.x(), way.y());
                return length > 1e-9 ? way / length * std::min(pull, length / 2.0) : QPointF();
            };
            int const before = point > 0 ? point - 1 : (stroke.closed ? n - 1 : -1);
            int const after = point + 1 < n ? point + 1 : (stroke.closed ? 0 : -1);
            stroke.setHandles(point, along(before), along(after));
            stroke.points[point].corner = true;
        }
        contourEdited(shown);
    }
    if (m_drag == DragHandle && contourValid() && (m_ceHandle == 0 || m_ceHandle == 1)) {
        int handled = m_cePoint;
        Stroke& stroke = *handleLine(shown, handled);
        QPointF in, out;
        stroke.handlesAt(handled, in, out);
        QPointF const reach = m_dragNow - stroke.points[handled].pos;
        QPointF& mine = m_ceHandle == 0 ? in : out;
        QPointF& other = m_ceHandle == 0 ? out : in;
        mine = reach;
        double const length = std::hypot(reach.x(), reach.y()), otherLength = std::hypot(other.x(), other.y());
        if (!m_ceBreak && length > 1e-9 && otherLength > 1e-9)
            other = -reach / length * otherLength;
        stroke.setHandles(handled, in, out);
        // A point whose handles are not in line is a cusp, and is shown as a corner.
        double const cross = in.x() * out.y() - in.y() * out.x();
        double const lengths = std::hypot(in.x(), in.y()) * std::hypot(out.x(), out.y());
        stroke.points[handled].corner = lengths > 1e-9 && (std::abs(cross) > lengths * 0.02 || in.x() * out.x() + in.y() * out.y() > 0);
        contourEdited(shown);
    }
    if (m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate) {
        // Scaled, a line keeps its thickness - unless Scale Pencil Line Thickness is on: then it is as much thicker or thinner.
        bool const keepWidth = m_drag == DragScale && !m_scaleThickness;
        for (int i : m_selStrokes) {
            float const width = shown.strokes[i].width;
            shown.strokes[i].transform(m_dragTransform);
            if (keepWidth)
                shown.strokes[i].width = width;
        }
        for (int i : m_selFills)
            shown.fills[i].transform(m_dragTransform);
        if (picturesHere())
            for (int i : m_selPictures)
                if (i >= 0 && i < shown.pictures.size())
                    shown.pictures[i].transform(m_dragTransform);
    }
    // Joints: what was moved takes the points joined to it along (a line moved drags the ends of the lines joined to it).
    if (current)
        shown.keepJoints(*current);
    return shown;
}

// ---- the frame as the node graph puts it together -----------------------------------------------------------------------

bool Canvas::paintGraph(QPainter& painter, Scene const& scene, int frame)
{
    NodeGraph const& graph = scene.graph;
    int const display = graph.display();
    QPaintDevice* const device = painter.device();
    if (!display || !device)
        return false;
    double const dpr = device->devicePixelRatioF();
    QSize const size(int(device->width() * dpr), int(device->height() * dpr));
    QTransform const base = painter.transform();
    QTransform const camera = scene.cameraTransform(frame);
    // A blur is so many scene units wide: in the picture that depends on how big the frame is drawn.
    double const pixelsPerUnit = std::sqrt(std::abs((camera * base).determinant())) * dpr;
    auto blank = [&] {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        return image;
    };
    // Each output of each node, worked out once; a ring (which the Node View does not let one make) gives nothing.
    QHash<QPair<int, int>, QImage> made;
    QSet<QPair<int, int>> working;
    std::function<QImage(int, int)> picture = [&](int id, int port) -> QImage {
        QPair<int, int> const key(id, port);
        if (auto const found = made.constFind(key); found != made.cend())
            return *found;
        if (working.contains(key))
            return blank();
        working.insert(key);
        QImage out = blank();
        auto input = [&](int node, int in) {
            GraphCable const* const c = graph.into(node, in);
            return c ? picture(c->from, c->fromPort) : blank();
        };
        GraphNode const* const n = graph.node(id);
        if (n && n->kind == GraphNode::LayerNode) {
            // A layer's picture: where its pegs put it, with its own effects.
            int const l = scene.layerIndex(n->layer);
            if (l >= 0 && scene.enabled(l)) {
                Layer const& layer = scene.layers[l];
                QPainter p(&out);
                p.setRenderHints(painter.renderHints());
                if (layer.kind == Layer::ColourCard) {
                    QColor colour = layer.cardColor;
                    colour.setAlphaF(colour.alphaF() * float(layer.opacity));
                    p.setTransform(base);
                    p.fillRect(QRectF(0, 0, scene.width, scene.height), colour);
                } else if (int const index = layer.drawingAt(frame); index != Layer::kNoDrawing) {
                    p.setTransform(scene.worldTransform(l, frame) * camera * base);
                    Pose const pose = scene.poseOf(l, frame);
                    Drawing shown = Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose));
                    if ((n->sheets & 0xf) != 0xf) {
                        // Only the art layers it gives (Line Art, Colour Art, Underlay, Overlay).
                        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
                            if (!(n->sheets & (1 << sheet))) {
                                shown.fillsOf(sheet).clear();
                                shown.strokesOf(sheet).clear();
                            }
                        shown.pictures.removeIf([&](Picture const& pic) { return !(n->sheets & (1 << (pic.painted ? pic.sheet : int(Drawing::LineSheet)))); });
                    }
                    paintLayerDrawing(p, scene, l, frame, shown, layer.opacity * pose.opacity, camera * base);
                }
            }
        } else if (n && n->kind == GraphNode::Composite) {
            // Its inputs one over the other, the first in front; a layer that comes straight in is mixed as its blend mode says.
            for (int in = n->inputs() - 1; in >= 0; --in) {
                GraphCable const* const c = graph.into(id, in);
                if (!c)
                    continue;
                QImage const over = picture(c->from, c->fromPort);
                GraphNode const* const from = graph.node(c->from);
                int const l = from && from->kind == GraphNode::LayerNode ? scene.layerIndex(from->layer) : -1;
                int const blend = l >= 0 ? scene.layers[l].blend : int(Layer::BlendNormal);
                if (blend == Layer::BlendNormal) {
                    QPainter p(&out);
                    p.resetTransform();
                    p.drawImage(0, 0, over);
                    continue;
                }
                for (int y = 0; y < out.height(); ++y) {
                    QRgb const* const s = reinterpret_cast<QRgb const*>(over.constScanLine(y));
                    QRgb* const d = reinterpret_cast<QRgb*>(out.scanLine(y));
                    for (int x = 0; x < out.width(); ++x)
                        if (qAlpha(s[x]) > 0)
                            d[x] = blendPixel(s[x], d[x], blend);
                }
            }
        } else if (n && n->kind == GraphNode::EffectNode) {
            out = input(id, 0);
            Effect const& e = n->effect;
            double const amount = n->amountAt(frame);
            if (!e.enabled) {
                // (Turned off: the picture goes through as it is.)
            } else if ((e.type == Effect::Blur && !blursEnabled()) || (e.type == Effect::Focus && !focusEnabled())) {
                // (Switched off in the preferences.)
            } else if (e.type == Effect::Blur || e.type == Effect::Focus) {
                Effects::blur(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Tint) {
                Effects::tint(out, e.color, amount);
            } else if (e.type == Effect::Transparency) {
                Effects::transparency(out, amount);
            } else if (e.type == Effect::DirectionalBlur) {
                Effects::directionalBlur(out, amount * pixelsPerUnit, e.direction);
            } else if (e.type == Effect::HueSaturation) {
                Effects::hueSaturation(out, e.hue, e.saturation, e.lightness, amount);
            } else if (e.type == Effect::BrightnessContrast) {
                Effects::brightnessContrast(out, e.brightness, e.contrast, amount);
            } else if (e.type == Effect::Grain) {
                Effects::grain(out, amount, quint32(e.seed) * 7919u + quint32(frame));
            } else if (e.type == Effect::RadialBlur) {
                Effects::radialBlur(out, amount);
            } else if (e.type == Effect::ColourScale) {
                Effects::colourScale(out, e.color, amount);
            } else if (e.type == Effect::Negate) {
                Effects::negate(out, amount);
            } else if (e.type == Effect::Median) {
                Effects::median(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Ripple) {
                Effects::ripple(out, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
            } else if (e.type == Effect::Glow || e.type == Effect::Shadow) {
                Effects::halo(out, e.color, amount * pixelsPerUnit, e.type == Effect::Shadow ? e.offset * (pixelsPerUnit / dpr) : QPointF());
            } else if (e.type == Effect::MatteGenerator) {
                Effects::matteGenerator(out, e.direction * pixelsPerUnit, amount * pixelsPerUnit, e.color);
            } else if (e.type == Effect::Bloom) {
                Effects::bloom(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Pixelate) {
                Effects::pixelate(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Turbulence) {
                Effects::turbulence(out, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
            } else if (e.type == Effect::Gradient) {
                Effects::gradient(out, e.color, e.endColor, e.direction, e.invert, amount);
            } else if (e.type == Effect::BlackWhite) {
                Effects::blackWhite(out, amount);
            } else if (e.type == Effect::MatteResize) {
                Effects::matteResize(out, amount * pixelsPerUnit, e.invert);
            } else if (e.type == Effect::FieldChart) {
                Effects::fieldChart(out, QTransform::fromScale(dpr, dpr).mapRect((camera * base).mapRect(QRectF(0, 0, scene.width, scene.height))), e.color,
                                    amount * dpr);
            } else if (e.type == Effect::ColourCurves) {
                Effects::colourCurves(out, e.curves, amount);
            } else if (e.type == Effect::ApplyPeg) {
                if (int const peg = scene.layerIndex(e.matte); peg >= 0) {
                    QTransform const toDevice = camera * base * QTransform::fromScale(dpr, dpr);
                    Effects::transformImage(out, toDevice.inverted() * scene.worldTransform(peg, frame) * toDevice);
                }
            } else if (e.type == Effect::ScaleOutput) {
                Effects::scaleOutput(out, amount);
            } else if (e.type == Effect::Script) {
                Effects::runScript(out, e.text, frame, amount, pixelsPerUnit);
            } else if (e.type == Effect::External) {
                Effects::runExternal(out, e.text, frame);
            } else if (e.type == Effect::ChannelSelector) {
                Effects::channelSelect(out, e.channel);
            } else if (e.type == Effect::Crop) {
                if (!e.area.isEmpty())
                    Effects::crop(out, QTransform::fromScale(dpr, dpr).mapRect((camera * base).mapRect(e.area)));
            } else if (e.type == Effect::RemoveTransparency) {
                Effects::removeTransparency(out, amount);
            } else if (e.type == Effect::MatteComposite) {
                Effects::matteComposite(out);
            } else if (e.type == Effect::Blending) {
                // Over what comes in at the second input, as the blend mode says.
                QImage under = input(id, 1);
                Effects::blendOnto(under, out, e.blendMode);
                out = under;
            } else if (e.type == Effect::VariableBlur) {
                Effects::variableBlur(out, input(id, 1), amount * pixelsPerUnit);
            } else if (e.type == Effect::Glue) {
                // Joined to what comes in at the second input.
                QImage under = input(id, 1);
                Effects::glue(under, out, amount * pixelsPerUnit);
                out = under;
            } else if (GraphNode::takesMatte(e.type) && graph.into(id, 1)) {
                // The matte comes in at the second input: what is painted there (or, inverted, what is not).
                QImage where = input(id, 1);
                if (e.invert) {
                    QImage outside = blank();
                    outside.fill(Qt::black);
                    QPainter op(&outside);
                    op.resetTransform();
                    op.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                    op.drawImage(0, 0, where);
                    op.end();
                    where = outside;
                }
                if (e.type == Effect::Cutter) {
                    QPainter cut(&out);
                    cut.resetTransform();
                    cut.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    cut.drawImage(0, 0, where);
                } else if (e.type == Effect::MatteBlur) {
                    Effects::blur(where, std::max(1.0, amount * pixelsPerUnit / 3.0));
                    QImage blurred = out;
                    Effects::blur(blurred, amount * pixelsPerUnit);
                    {
                        QPainter bp(&blurred);
                        bp.resetTransform();
                        bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        bp.drawImage(0, 0, where);
                    }
                    QPainter ip(&out);
                    ip.resetTransform();
                    ip.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                    ip.drawImage(0, 0, where);
                    ip.setCompositionMode(QPainter::CompositionMode_Plus);
                    ip.drawImage(0, 0, blurred);
                } else {
                    // Tone or Highlight: shaded (or lit) where the matte is, the edge softened.
                    Effects::blur(where, amount * pixelsPerUnit);
                    QImage shaded = out;
                    {
                        QPainter sp(&shaded);
                        sp.resetTransform();
                        sp.setCompositionMode(e.type == Effect::Tone ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_Screen);
                        sp.fillRect(shaded.rect(), QColor(e.color.red(), e.color.green(), e.color.blue()));
                        sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        sp.drawImage(0, 0, out);
                        sp.drawImage(0, 0, where);
                    }
                    QPainter onto(&out);
                    onto.resetTransform();
                    onto.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    onto.drawImage(0, 0, shaded);
                }
            }
        } else if (n && n->kind == GraphNode::Group) {
            // What leaves a group's output: what comes into that input of its output node, inside.
            if (int const inside = graph.groupOut(id))
                out = input(inside, port);
        } else if (n && n->kind == GraphNode::GroupIn) {
            // What an output of a group's input node gives: what comes into that input of the group, outside.
            out = input(n->group, port);
        }
        working.remove(key);
        made.insert(key, out);
        return out;
    };
    GraphCable const* const shown = graph.into(display, 0);
    QImage const frameImage = shown ? picture(shown->from, shown->fromPort) : blank();
    painter.save();
    painter.resetTransform();
    painter.drawImage(0, 0, frameImage);
    painter.restore();
    return true;
}
