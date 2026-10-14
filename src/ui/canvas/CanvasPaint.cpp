// Eclipse - the Camera view: painting a line, an area, a drawing, a layer with its effects, a whole frame.
#include "ui/canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/canvas/PencilTexture.h"
#include "ui/panels/Effects.h"

// An area with a paper texture: the area in solid paint, seen through the grain lying flat.  Towards its edge
// the grain thins out, so that the edge is the grain's own and not a cut through it.
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
    // (A box blur, across and then down: sums of what lies within `soft` pixels.  Past the rim of what was worked
    // out, things are taken to go on as at the rim.)
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
        // Crisp: the edge of the area does not cut through the bits of the grain.  Each bit of the paper is there
        // whole or not at all, by whether its middle lies in the area (and by how the edge frays there).
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

// A line with a pencil texture.  Every place the line covers is asked which place of the line is nearest to
// it: how far along the line that is, and how far to the side, say where in the strip of grain it lies.  So the
// grain bends with the line without a seam, and the edge of the line is the edge of the grain itself.
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
                    // Crisp: the shape of the line does not cut through the bits of the grain.  Each bit of the strip
                    // is there whole or not at all - so here the question is only which bit this pixel lies in, and
                    // whether that bit, taken at its middle, belongs to the line.
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
        // One thickness all along: the whole line as one outline, which is what lets a
        // corner come to a point or be cut flat, and an end be cut square.
        QPainterPath path(line[0].pos);
        int const last = stroke.closed ? int(line.size()) - 1 : int(line.size());  // a closed path ends where it began
        for (int i = 1; i < last; ++i)
            path.lineTo(line[i].pos);
        if (stroke.closed)
            path.closeSubpath();
        pen.setWidthF(widthAt(0));
        pen.setCapStyle(stroke.capStart == Stroke::FlatCap ? Qt::FlatCap : Qt::RoundCap);
        // The kind of mitre that gives up and cuts flat past the limit, rather than one
        // that is merely shortened.
        pen.setJoinStyle(stroke.join == Stroke::MitreJoin ? Qt::SvgMiterJoin : stroke.join == Stroke::BevelJoin ? Qt::BevelJoin : Qt::RoundJoin);
        pen.setMiterLimit(Stroke::kMitreLimit);
        if (stroke.closed || stroke.capStart == stroke.capEnd) {
            painter.setPen(pen);
            painter.drawPath(path);
            return;
        }
        // One end round, the other flat: a pen has only one kind of end, so the outline of
        // the line with flat ends is taken and a disc added at the round one.  Filled as
        // one shape, so that a translucent colour is not doubled where they overlap.
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
    // The thickness changes along the line: its outline is built by stepping half the
    // thickness to either side of each point, and filled as one shape.
    QVector<QPointF> pos;
    QVector<double> half;
    for (StrokePoint const& p : line) {
        if (!pos.isEmpty() && QLineF(pos.last(), p.pos).length() < 1e-6)
            continue;  // a doubled point has no direction
        pos.push_back(p.pos);
        half.push_back(std::max(0.0, double(stroke.width) * p.pressure / 2.0));
    }
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
    // (The body of the line and the discs that round it off are kept apart: laid into one outline, a disc
    // would be cancelled out wherever the body happens to wind the other way under it.)
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
    // Tips: a disc where the end is round.  Where the line has thinned to nothing the disc
    // is nothing too, and the line simply comes to a point.
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

void Canvas::paintFill(QPainter& painter, Fill const& fill, QBrush const& color)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
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
    // Pictures lie under everything drawn.  One whose file has gone shows where it was.
    for (Picture const& picture : drawing.pictures) {
        painter.save();
        painter.setTransform(picture.placement, true);
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
    }
    // From the sheet underneath to the one on top: underlay, colour art, lines, overlay.
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill const& fill : drawing.fillsOf(sheet))
            Canvas::paintFill(painter, fill, tint ? QBrush(*tint) : scene.brushFor(fill));
        for (Stroke const& stroke : drawing.strokesOf(sheet))
            Canvas::paintStroke(painter, stroke, tint ? QBrush(*tint) : scene.brushFor(stroke));
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
    // A translucent drawing is painted opaque into its own image and blended once: painting
    // the pieces translucent one by one would darken every place where two of them overlap.
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
        Pose const pose = layer.poseAt(f);
        paintLayerDrawing(painter, inner, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose),
                          layer.opacity * pose.opacity, base);
    }
    painter.setTransform(base);
    --depth;
}

void Canvas::paintLayerDrawing(QPainter& painter, Scene const& scene, int l, int frame, Drawing const& drawing, double opacity,
                               QTransform const& sceneToDevice, bool everySheet)
{
    Layer const& layer = scene.layers[l];
    if (!everySheet && layer.hiddenSheets != 0 && !layer.showsSymbol()) {
        // The art layers left out of the picture are not in it.
        Drawing kept = drawing;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            if (!layer.sheetShown(sheet)) {
                kept.fillsOf(sheet).clear();
                kept.strokesOf(sheet).clear();
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
            // Turning to another colour as they age: the picture, once, as an image; and from it, the picture part way to that
            // colour (in eight steps), each made when first wanted.  A particle with any of the colour is drawn from those.
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
            for (Effect::Particle const& particle : emitter.particles(rate, frame)) {
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
    Pose const pose = layer.poseAt(frame);
    for (int i = 0; i < layer.effects.size(); ++i) {
        Effect const& e = layer.effects[i];
        if (!e.enabled)
            continue;
        double const amount = layer.effectAmount(i, pose);
        if (e.type == Effect::Blur) {
            Effects::blur(image, amount * pixelsPerUnit);
        } else if (e.type == Effect::Tint) {
            Effects::tint(image, e.color, amount);
        } else if (e.type == Effect::Glow || e.type == Effect::Shadow) {
            // The offset is a direction on the picture: it does not turn with the layer.
            Effects::halo(image, e.color, amount * pixelsPerUnit, e.type == Effect::Shadow ? e.offset * (pixelsPerUnit / dpr) : QPointF());
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
                paintDrawingOpaque(mp, scene, Deform::shown(scene.layers[m].drawings[index], scene.layers[m], scene.layers[m].poseAt(frame)), nullptr);
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

void Canvas::paintFrame(QPainter& painter, Scene const& scene, int frame)
{
    painter.fillRect(QRectF(0, 0, scene.width, scene.height), scene.background);
    QTransform const base = painter.transform();
    QTransform const camera = scene.cameraTransform(frame);
    // The furthest first, so that what is nearer the camera covers it.
    for (int l : scene.drawOrder(frame)) {
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
        Pose const pose = layer.poseAt(frame);
        paintLayerDrawing(painter, scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose),
                          layer.opacity * pose.opacity, camera * base);
    }
    painter.setTransform(base);
}

Drawing Canvas::previewDrawing() const
{
    Drawing const* const current = currentDrawing();
    Drawing shown = current ? *current : Drawing();
    if (m_drag == DragPoint && contourValid()) {
        Stroke& line = *contourLine(shown);
        QPointF const by = m_dragNow - line.points[m_cePoint].pos;
        line.points[m_cePoint].pos = m_dragNow;
        for (int other : contourPointsPicked())     // the others picked go along
            line.points[other].pos += by;
        contourEdited(shown);
    }
    if (m_drag == DragHandle && contourValid() && (m_ceHandle == 0 || m_ceHandle == 1)) {
        Stroke& stroke = *contourLine(shown);
        QPointF in, out;
        stroke.handlesAt(m_cePoint, in, out);
        QPointF const reach = m_dragNow - stroke.points[m_cePoint].pos;
        QPointF& mine = m_ceHandle == 0 ? in : out;
        QPointF& other = m_ceHandle == 0 ? out : in;
        mine = reach;
        double const length = std::hypot(reach.x(), reach.y()), otherLength = std::hypot(other.x(), other.y());
        // The other handle keeps its length and stays in line with this one, so that the
        // curve goes smoothly through the point - unless the handle was pulled away on its own.
        if (!m_ceBreak && length > 1e-9 && otherLength > 1e-9)
            other = -reach / length * otherLength;
        stroke.setHandles(m_cePoint, in, out);
        // A point whose handles are not in line is a cusp, and is shown as a corner.
        double const cross = in.x() * out.y() - in.y() * out.x();
        double const lengths = std::hypot(in.x(), in.y()) * std::hypot(out.x(), out.y());
        stroke.points[m_cePoint].corner = lengths > 1e-9 && (std::abs(cross) > lengths * 0.02 || in.x() * out.x() + in.y() * out.y() > 0);
        contourEdited(shown);
    }
    if (m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate) {
        for (int i : m_selStrokes)
            shown.strokes[i].transform(m_dragTransform);
        for (int i : m_selFills)
            shown.fills[i].transform(m_dragTransform);
        if (picturesHere())
            for (int i : m_selPictures)
                if (i >= 0 && i < shown.pictures.size())
                    shown.pictures[i].transform(m_dragTransform);
    }
    return shown;
}
