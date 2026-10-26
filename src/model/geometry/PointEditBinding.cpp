#include "model/geometry/PointEditBinding.h"
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace point_edit {
Stroke const* lineAt(Drawing const& art, Binding const& b)
{
    if (b.sheet < 0 || b.line < 0)
        return nullptr;
    if (b.fill < 0)
        return &art.strokesOf(b.sheet)[b.line];
    return &art.fillsOf(b.sheet)[b.fill].contours[b.line];
}

namespace {
QPointF tangent(Stroke const& line, double place)
{
    double const low = std::max(0.0, place - 0.0001), high = std::min(double(line.pieces()), place + 0.0001);
    return high > low ? (line.pointAt(high) - line.pointAt(low)) / (high - low) : QPointF();
}

Stroke frozen(Stroke line) { line.freezeHandles(); return line; }
}

QVector<Binding> bind(Stroke const& line, int sheet, int fill, Drawing const& reference)
{
    QVector<Binding> bindings(line.points.size());
    for (int p = 0; p < line.points.size(); ++p) {
        StrokePoint const& point = line.points[p];
        Binding& binding = bindings[p];
        double best = std::numeric_limits<double>::max();
        auto consider = [&](Stroke const& source, Binding address) {
            for (int q = 0; q < source.points.size(); ++q)
                if (point.editIdentity && point.editIdentity == source.points[q].editIdentity) {
                    address.point = q;
                    address.place = q;
                    binding = address;
                    best = -1.0;
                    return;
                }
            if (best < 0 || !line.sourceIdentity || line.sourceIdentity != source.sourceIdentity)
                return;
            Stroke const shape = frozen(source);
            address.place = shape.placeAt(point.pos);
            double const distance = QLineF(shape.pointAt(address.place), point.pos).length();
            if (distance < best) { best = distance; binding = address; }
        };
        for (int s = 0; s < Drawing::SheetCount; ++s) {
            for (int l = 0; l < reference.strokesOf(s).size(); ++l)
                consider(reference.strokesOf(s)[l], {s, -1, l});
            for (int f = 0; f < reference.fillsOf(s).size(); ++f)
                for (int c = 0; c < reference.fillsOf(s)[f].contours.size(); ++c)
                    consider(reference.fillsOf(s)[f].contours[c], {s, f, c});
        }
        // Cut fill contours inherit their parent area's boundaries even when a new contour was fitted.
        if (binding.sheet < 0 && fill >= 0 && sheet >= 0) {
            Fill const& area = reference.fillsOf(sheet)[fill];
            auto const& contours = area.contours;
            for (int c = 0; c < contours.size(); ++c) {
                Stroke const shape = frozen(contours[c]);
                double const place = shape.placeAt(point.pos);
                double const distance = QLineF(shape.pointAt(place), point.pos).length();
                if (distance < best) { best = distance; binding = {sheet, fill, c, -1, place}; }
            }
            if (contours.isEmpty() && (area.link != 0 || area.follows))
                for (int s : {sheet, sheet == Drawing::ColourSheet ? int(Drawing::LineSheet) : sheet})
                    for (int l = 0; l < reference.strokesOf(s).size(); ++l) {
                        Stroke const& wall = reference.strokesOf(s)[l];
                        if (wall.points.isEmpty() || (area.link != 0 && wall.link != area.link)) continue;
                        Stroke const shape = frozen(wall);
                        double const place = shape.placeAt(point.pos);
                        double const distance = QLineF(shape.pointAt(place), point.pos).length();
                        if (distance < best) { best = distance; binding = {s, -1, l, -1, place}; }
                    }
        }
        if (Stroke const* source = lineAt(reference, binding)) {
            QPointF const direction = tangent(frozen(*source), binding.place);
            double const square = QPointF::dotProduct(direction, direction);
            QPointF in, out;
            line.handlesAt(p, in, out);
            if (square > 1e-12) {
                binding.in = QPointF::dotProduct(in, direction) / square;
                binding.out = QPointF::dotProduct(out, direction) / square;
            }
        }
    }
    for (int p = 0; p < bindings.size(); ++p) {
        Binding& b = bindings[p];
        if (b.point < 0)
            continue;
        int const next = p + 1 < bindings.size() ? p + 1 : p - 1;
        if (next < 0)
            continue;
        Binding const& n = bindings[next];
        if (n.sheet == b.sheet && n.fill == b.fill && n.line == b.line) {
            Stroke const* source = lineAt(reference, b);
            double delta = n.place - b.place;
            if (source->closed && std::abs(delta) > source->pieces() / 2.0)
                delta += delta > 0 ? -source->pieces() : source->pieces();
            b.reverse = (next > p ? delta : -delta) < 0;
        }
    }
    return bindings;
}

PointMove displacement(Binding const& b, Drawing const& before, Drawing const& posed)
{
    PointMove move;
    Stroke const* rest = lineAt(before, b);
    Stroke const* now = lineAt(posed, b);
    if (!rest || !now)
        return move;
    if (b.point >= 0) {
        Stroke const a = frozen(*rest), z = frozen(*now);
        move.by = z.points[b.point].pos - a.points[b.point].pos;
        move.in = z.points[b.point].in - a.points[b.point].in;
        move.out = z.points[b.point].out - a.points[b.point].out;
        if (b.reverse)
            std::swap(move.in, move.out);
    } else {
        Stroke const a = frozen(*rest), z = frozen(*now);
        move.by = z.pointAt(b.place) - a.pointAt(b.place);
        QPointF const change = tangent(z, b.place) - tangent(a, b.place);
        move.in = change * b.in;
        move.out = change * b.out;
    }
    return move;
}

QPointF edgeDisplacement(QPointF p, int sheet, Drawing const& before, Drawing const& posed, bool nearby)
{
    double best = std::numeric_limits<double>::max();
    QPointF delta;
    auto consider = [&](Stroke const& a, Stroke const& b) {
        if (a.points.isEmpty()) return;
        Stroke const from = frozen(a), to = frozen(b);
        double const place = from.placeAt(p);
        double const distance = QLineF(from.pointAt(place), p).length();
        if (distance < best && (!nearby || distance <= std::max(4.0, a.width / 2.0 + 4.0))) {
            best = distance;
            delta = to.pointAt(place) - from.pointAt(place);
        }
    };
    for (int s : {sheet, sheet == Drawing::ColourSheet ? int(Drawing::LineSheet) : sheet}) {
        for (int l = 0; l < before.strokesOf(s).size(); ++l)
            consider(before.strokesOf(s)[l], posed.strokesOf(s)[l]);
        for (int f = 0; f < before.fillsOf(s).size(); ++f)
            for (int c = 0; c < before.fillsOf(s)[f].contours.size(); ++c)
                consider(before.fillsOf(s)[f].contours[c], posed.fillsOf(s)[f].contours[c]);
    }
    return delta;
}
}
