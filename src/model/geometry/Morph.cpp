#include "model/geometry/Morph.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include "model/geometry/MorphParts.h"

using namespace morph_parts;

int Morphing::shapeKey(int sheet, bool stroke, int index)
{
    return ((sheet * 2 + (stroke ? 1 : 0)) << 24) | (index & 0xffffff);
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints, QVector<Morph::Group> const& groups)
{
    if (groups.isEmpty())
        return between(a, b, t, hints);
    // The shapes of a drawing that a set of keys names (or, `rest`, that none of the morphing layers names).
    auto part = [](Drawing const& d, QSet<int> const& keys, bool rest) {
        Drawing out;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            QVector<Fill> const& fills = d.fillsOf(sheet);
            for (int i = 0; i < fills.size(); ++i)
                if (keys.contains(shapeKey(sheet, false, i)) != rest)
                    out.fillsOf(sheet).push_back(fills[i]);
            QVector<Stroke> const& strokes = d.strokesOf(sheet);
            for (int i = 0; i < strokes.size(); ++i)
                if (keys.contains(shapeKey(sheet, true, i)) != rest)
                    out.strokesOf(sheet).push_back(strokes[i]);
        }
        return out;
    };
    Drawing out;
    auto add = [&](Drawing const& d) {
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            out.fillsOf(sheet) += d.fillsOf(sheet);
            out.strokesOf(sheet) += d.strokesOf(sheet);
        }
    };
    QSet<int> allFirst, allSecond;
    for (Morph::Group const& g : groups) {
        QSet<int> const first(g.first.cbegin(), g.first.cend()), second(g.second.cbegin(), g.second.cend());
        allFirst += first;
        allSecond += second;
        add(between(part(a, first, false), part(b, second, false), t, hints));
    }
    Drawing const rest = between(part(a, allFirst, true), part(b, allSecond, true), t, hints);
    add(rest);
    out.pictures = t < 0.5 ? a.pictures : b.pictures;
    return out;
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0)
        return a;
    if (t >= 1.0)
        return b;
    Drawing out;
    out.pictures = t < 0.5 ? a.pictures : b.pictures;     // a picture is not morphed: one, then the other
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        // The painted areas.
        {
            QVector<Fill> const& from = a.fillsOf(sheet);
            QVector<Fill> const& to = b.fillsOf(sheet);
            QVector<QPointF> first, second;
            for (Fill const& fill : from)
                first.push_back(fill.bounds().center());
            for (Fill const& fill : to)
                second.push_back(fill.bounds().center());
            QVector<int> partner, left;
            pairUp(first, second, partner, left);
            QVector<QVector<QLineF>> hintsOf(from.size());
            if (!hints.isEmpty())
                pairByHints(from, to, hints, partner, left, hintsOf);
            QVector<Fill>& made = out.fillsOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                Fill const fill = partner[i] >= 0 ? fillBetween(from[i], to[partner[i]], t, hintsOf[i]) : fillShrunk(from[i], 1.0 - t);
                if (!fill.outlines.isEmpty())
                    made.push_back(fill);
            }
            for (int k : std::as_const(left))
                made.push_back(fillShrunk(to[k], t));
        }
        // The lines.
        {
            QVector<Stroke> const& from = a.strokesOf(sheet);
            QVector<Stroke> const& to = b.strokesOf(sheet);
            QVector<QPointF> first, second;
            for (Stroke const& stroke : from)
                first.push_back(stroke.bounds().center());
            for (Stroke const& stroke : to)
                second.push_back(stroke.bounds().center());
            QVector<int> partner, left;
            pairUp(first, second, partner, left);
            QVector<QVector<QLineF>> hintsOf(from.size());
            if (!hints.isEmpty())
                pairByHints(from, to, hints, partner, left, hintsOf);
            QVector<Stroke>& made = out.strokesOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                if (from[i].points.isEmpty())
                    continue;
                made.push_back(partner[i] >= 0 && !to[partner[i]].points.isEmpty() ? strokeBetween(from[i], to[partner[i]], t, hintsOf[i]) : strokeShrunk(from[i], 1.0 - t));
            }
            for (int k : std::as_const(left))
                if (!to[k].points.isEmpty())
                    made.push_back(strokeShrunk(to[k], t));
        }
    }
    return out;
}
