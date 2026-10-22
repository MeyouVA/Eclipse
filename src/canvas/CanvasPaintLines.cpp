// Eclipse - the Camera view: painting a line and an area (plain, fading, textured).
#include "canvas/Canvas.h"
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
#include "canvas/PencilTexture.h"
#include "render/effects/Effects.h"
#include "canvas/CanvasPaintShared.h"

using namespace canvas_paint_shared;

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
