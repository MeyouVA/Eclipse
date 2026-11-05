#include "model/geometry/Morph.h"

#include <QPainterPath>
#include <QSet>
#include <algorithm>
#include <cmath>
#include "model/geometry/MorphParts.h"

using namespace morph_parts;

double Morph::s_quality = 0.2;

int Morphing::shapeKey(int sheet, bool stroke, int index)
{
    return ((sheet * 2 + (stroke ? 1 : 0)) << 24) | (index & 0xffffff);
}

namespace {

// The hints of the types asked for.
QVector<QLineF> ofTypes(QVector<QLineF> const& hints, QVector<int> const& types, std::initializer_list<int> wanted)
{
    QVector<QLineF> out;
    for (int k = 0; k < hints.size(); ++k) {
        int const type = k < types.size() ? types[k] : Morph::Contour;
        if (std::find(wanted.begin(), wanted.end(), type) != wanted.end())
            out.push_back(hints[k]);
    }
    return out;
}

// Of the hints a shape was given, the ones that pin it (not the ones that only paired it).
QVector<QLineF> pinning(QVector<QLineF> const& given, QVector<QLineF> const& pins)
{
    QVector<QLineF> out;
    for (QLineF const& h : given)
        if (pins.contains(h))
            out.push_back(h);
    return out;
}

// A shape of one drawing alone near the end of an Appearing (or Vanishing) hint: where it grows from (or shrinks into).
template <typename Shape>
bool pointFor(Shape const& shape, QVector<QLineF> const& hints, bool appearing, QPointF& at)
{
    double nearest = kHintReach;
    bool found = false;
    for (QLineF const& h : hints) {
        double const away = awayFrom(shape, appearing ? h.p2() : h.p1());
        if (away <= nearest) {
            nearest = away;
            at = appearing ? h.p1() : h.p2();
            found = true;
        }
    }
    return found;
}

QTransform movedBy(QPointF d)
{
    return QTransform::fromTranslate(d.x(), d.y());
}

Drawing typedBetween(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints, QVector<int> const& types)
{
    t = std::clamp(t, 0.0, 1.0);
    if (t <= 0.0)
        return a;
    if (t >= 1.0)
        return b;
    QVector<QLineF> const areaPairs = ofTypes(hints, types, {Morph::Contour, Morph::Zone});
    QVector<QLineF> const areaPins = ofTypes(hints, types, {Morph::Contour});
    QVector<QLineF> const linePairs = ofTypes(hints, types, {Morph::Contour, Morph::Zone, Morph::Pencil});
    QVector<QLineF> const linePins = ofTypes(hints, types, {Morph::Contour, Morph::Pencil});
    QVector<QLineF> const appearing = ofTypes(hints, types, {Morph::Appearing});
    QVector<QLineF> const vanishing = ofTypes(hints, types, {Morph::Vanishing});
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
            if (!areaPairs.isEmpty())
                pairByHints(from, to, areaPairs, partner, left, hintsOf);
            QVector<Fill>& made = out.fillsOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                Fill fill;
                QPointF into;
                if (partner[i] >= 0) {
                    fill = fillBetween(from[i], to[partner[i]], t, pinning(hintsOf[i], areaPins));
                } else {
                    fill = fillShrunk(from[i], 1.0 - t);
                    if (pointFor(from[i], vanishing, false, into))
                        fill.transform(movedBy((into - from[i].bounds().center()) * t));
                }
                if (!fill.outlines.isEmpty())
                    made.push_back(fill);
            }
            for (int k : std::as_const(left)) {
                Fill fill = fillShrunk(to[k], t);
                if (QPointF grows; pointFor(to[k], appearing, true, grows))
                    fill.transform(movedBy((grows - to[k].bounds().center()) * (1.0 - t)));
                made.push_back(fill);
            }
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
            if (!linePairs.isEmpty())
                pairByHints(from, to, linePairs, partner, left, hintsOf);
            QVector<Stroke>& made = out.strokesOf(sheet);
            for (int i = 0; i < from.size(); ++i) {
                if (from[i].points.isEmpty())
                    continue;
                if (partner[i] >= 0 && !to[partner[i]].points.isEmpty()) {
                    made.push_back(strokeBetween(from[i], to[partner[i]], t, pinning(hintsOf[i], linePins)));
                    continue;
                }
                Stroke s = strokeShrunk(from[i], 1.0 - t);
                if (QPointF into; pointFor(from[i], vanishing, false, into))
                    s.transform(movedBy((into - from[i].bounds().center()) * t));
                made.push_back(s);
            }
            for (int k : std::as_const(left)) {
                if (to[k].points.isEmpty())
                    continue;
                Stroke s = strokeShrunk(to[k], t);
                if (QPointF grows; pointFor(to[k], appearing, true, grows))
                    s.transform(movedBy((grows - to[k].bounds().center()) * (1.0 - t)));
                made.push_back(s);
            }
        }
    }
    return out;
}

// The shapes of a drawing that a set of keys names (or, `rest`, that none of the morphing layers names).
Drawing part(Drawing const& d, QSet<int> const& keys, bool rest)
{
    Drawing out;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Fill> const& fills = d.fillsOf(sheet);
        for (int i = 0; i < fills.size(); ++i)
            if (keys.contains(Morphing::shapeKey(sheet, false, i)) != rest)
                out.fillsOf(sheet).push_back(fills[i]);
        QVector<Stroke> const& strokes = d.strokesOf(sheet);
        for (int i = 0; i < strokes.size(); ++i)
            if (keys.contains(Morphing::shapeKey(sheet, true, i)) != rest)
                out.strokesOf(sheet).push_back(strokes[i]);
    }
    return out;
}

void addInto(Drawing& out, Drawing const& d)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        out.fillsOf(sheet) += d.fillsOf(sheet);
        out.strokesOf(sheet) += d.strokesOf(sheet);
    }
}

void transformAll(Drawing& d, QTransform const& m)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill& f : d.fillsOf(sheet))
            f.transform(m);
        for (Stroke& s : d.strokesOf(sheet))
            s.transform(m);
    }
}

QRectF boundsOf(Drawing const& d)
{
    QRectF box;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill const& f : d.fillsOf(sheet))
            box = box.united(f.bounds());
        for (Stroke const& s : d.strokesOf(sheet))
            box = box.united(s.bounds());
    }
    return box;
}

// How far along its own timing a morphing layer is when the morphing as a whole is `t` of the way.
double groupProgress(Morph::Group const& g, double t)
{
    double const start = std::clamp(g.start, 0.0, 1.0), stop = std::clamp(g.stop, 0.0, 1.0);
    if (stop <= start + 1e-9)
        return t >= start ? 1.0 : 0.0;
    return std::clamp((t - start) / (stop - start), 0.0, 1.0);
}

}  // namespace

QPointF Morphing::alongPath(QPolygonF const& path, double t, double* angle)
{
    if (path.isEmpty())
        return QPointF();
    if (path.size() == 1) {
        if (angle)
            *angle = 0.0;
        return path.first();
    }
    double length = 0.0;
    for (int i = 1; i < path.size(); ++i)
        length += QLineF(path[i - 1], path[i]).length();
    double want = std::clamp(t, 0.0, 1.0) * length;
    for (int i = 1; i < path.size(); ++i) {
        QLineF const seg(path[i - 1], path[i]);
        if (want <= seg.length() || i + 1 == path.size()) {
            if (angle)
                *angle = -seg.angle();      // (QLineF counts its angles the other way round)
            return seg.length() > 0.0 ? seg.pointAt(std::clamp(want / seg.length(), 0.0, 1.0)) : seg.p2();
        }
        want -= seg.length();
    }
    return path.last();
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints, QVector<Morph::Group> const& groups)
{
    Morph morph;
    morph.hints = hints;
    morph.groups = groups;
    for (Morph::Group& g : morph.groups)
        g.path = -1;
    morph.quality = t_quality;
    return between(a, b, t, morph);
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, QVector<QLineF> const& hints)
{
    return typedBetween(a, b, t, hints, {});
}

Drawing Morphing::between(Drawing const& a, Drawing const& b, double t, Morph const& morph)
{
    double const keep = t_quality;
    t_quality = morph.quality;
    Drawing out;
    if (morph.groups.isEmpty()) {
        out = typedBetween(a, b, t, morph.hints, morph.hintTypes);
    } else {
        QSet<int> allFirst, allSecond;
        for (Morph::Group const& g : morph.groups) {
            QSet<int> const first(g.first.cbegin(), g.first.cend()), second(g.second.cbegin(), g.second.cend());
            allFirst += first;
            allSecond += second;
            double const local = groupProgress(g, t);
            Drawing const from = part(a, first, false), to = part(b, second, false);
            Drawing made = typedBetween(from, to, local, morph.hints, morph.hintTypes);
            // Along its deformation path: moved off the straight way by as much as the path is, and turned as it turns.
            if (g.path >= 0 && g.path < morph.paths.size() && morph.paths[g.path].used && morph.paths[g.path].points.size() >= 2
                && local > 0.0 && local < 1.0) {
                Morph::Path const& path = morph.paths[g.path];
                double angle = 0.0;
                QPointF const on = alongPath(path.points, local, &angle);
                QPointF const straight = path.points.first() + (path.points.last() - path.points.first()) * local;
                QTransform m;
                if (path.rotation) {
                    double const chord = -QLineF(path.points.first(), path.points.last()).angle();
                    QPointF const middle = boundsOf(made).center();
                    m = QTransform::fromTranslate(-middle.x(), -middle.y()) * QTransform().rotate(angle - chord)
                        * QTransform::fromTranslate(middle.x(), middle.y());
                }
                if (path.translation)
                    m = m * movedBy(on - straight);
                transformAll(made, m);
            }
            addInto(out, made);
        }
        addInto(out, typedBetween(part(a, allFirst, true), part(b, allSecond, true), t, morph.hints, morph.hintTypes));
        out.pictures = t < 0.5 ? a.pictures : b.pictures;
    }
    if (morph.flatten && t > 0.0 && t < 1.0)
        flatten(out);
    t_quality = keep;
    return out;
}

void Morphing::flatten(Drawing& d)
{
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Fill>& fills = d.fillsOf(sheet);
        QVector<Fill> made;
        QVector<bool> taken(fills.size(), false);
        for (int i = 0; i < fills.size(); ++i) {
            if (taken[i])
                continue;
            Fill merged = fills[i];
            if (merged.gradPlaced || !merged.text.isEmpty()) {
                made.push_back(merged);
                continue;
            }
            QPainterPath area;
            area.setFillRule(Qt::WindingFill);
            for (QPolygonF const& o : fills[i].outlines)
                area.addPolygon(o);
            int joined = 0;
            for (int k = i + 1; k < fills.size(); ++k) {
                if (taken[k] || fills[k].gradPlaced || !fills[k].text.isEmpty() || fills[k].color != merged.color || fills[k].swatch != merged.swatch)
                    continue;
                QPainterPath other;
                other.setFillRule(Qt::WindingFill);
                for (QPolygonF const& o : fills[k].outlines)
                    other.addPolygon(o);
                area = area.united(other);
                taken[k] = true;
                ++joined;
            }
            if (joined > 0) {
                merged.outlines = area.simplified().toFillPolygons();
                for (QPolygonF& o : merged.outlines)
                    if (o.size() > 1 && o.first() == o.last())
                        o.removeLast();
                merged.contours.clear();
            }
            made.push_back(merged);
        }
        fills = made;
    }
}

QVector<QLineF> Morphing::suggestedHints(Drawing const& a, Drawing const& b, QVector<int>* types)
{
    QVector<QLineF> hints;
    if (types)
        types->clear();
    // The point of a run furthest from its middle, and the point of another run lying the same way from its own middle.
    auto furthest = [](QVector<QPointF> const& run, QPointF middle) {
        int best = 0;
        double far = -1.0;
        for (int i = 0; i < run.size(); ++i)
            if (double const d = QLineF(middle, run[i]).length(); d > far) {
                far = d;
                best = i;
            }
        return best;
    };
    auto sameWay = [](QVector<QPointF> const& run, QPointF middle, double angle) {
        int best = 0;
        double least = 1e18;
        for (int i = 0; i < run.size(); ++i) {
            double d = std::abs(QLineF(middle, run[i]).angle() - angle);
            d = std::min(d, 360.0 - d);
            if (d < least) {
                least = d;
                best = i;
            }
        }
        return best;
    };
    auto suggest = [&](QVector<QPointF> const& from, QVector<QPointF> const& to, QPointF ma, QPointF mb, int type) {
        if (from.isEmpty() || to.isEmpty())
            return;
        QPointF const p = from[furthest(from, ma)];
        QPointF const q = to[sameWay(to, mb, QLineF(ma, p).angle())];
        hints.push_back(QLineF(p, q));
        if (types)
            types->push_back(type);
    };
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        QVector<Fill> const& fa = a.fillsOf(sheet);
        QVector<Fill> const& fb = b.fillsOf(sheet);
        QVector<QPointF> first, second;
        for (Fill const& f : fa)
            first.push_back(f.bounds().center());
        for (Fill const& f : fb)
            second.push_back(f.bounds().center());
        QVector<int> partner, left;
        pairUp(first, second, partner, left);
        for (int i = 0; i < fa.size(); ++i)
            if (partner[i] >= 0 && !fa[i].outlines.isEmpty() && !fb[partner[i]].outlines.isEmpty())
                suggest(fa[i].outlines.first(), fb[partner[i]].outlines.first(), first[i], second[partner[i]], Morph::Contour);
        QVector<Stroke> const& sa = a.strokesOf(sheet);
        QVector<Stroke> const& sb = b.strokesOf(sheet);
        first.clear();
        second.clear();
        for (Stroke const& s : sa)
            first.push_back(s.bounds().center());
        for (Stroke const& s : sb)
            second.push_back(s.bounds().center());
        pairUp(first, second, partner, left);
        for (int i = 0; i < sa.size(); ++i) {
            if (partner[i] < 0)
                continue;
            QVector<QPointF> ra, rb;
            for (StrokePoint const& p : sa[i].points)
                ra.push_back(p.pos);
            for (StrokePoint const& p : sb[partner[i]].points)
                rb.push_back(p.pos);
            suggest(ra, rb, first[i], second[partner[i]], Morph::Pencil);
        }
    }
    return hints;
}
