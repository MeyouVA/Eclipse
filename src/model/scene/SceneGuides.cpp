// Eclipse - the scene: drawing guides - what kinds there are, and which ways a line may run at a place.
#include "model/scene/Scene.h"

#include <cmath>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

QString Scene::guideName(Guide::Kind kind)
{
    switch (kind) {
    case Guide::Ruler: return QCoreApplication::translate("Scene", "Ruler");
    case Guide::SquareGrid: return QCoreApplication::translate("Scene", "Square Grid");
    case Guide::Isometric: return QCoreApplication::translate("Scene", "Isometric Perspective");
    case Guide::OnePoint: return QCoreApplication::translate("Scene", "1-Point Perspective");
    case Guide::TwoPoint: return QCoreApplication::translate("Scene", "2-Point Perspective");
    case Guide::ThreePoint: return QCoreApplication::translate("Scene", "3-Point Perspective");
    case Guide::Symmetry: return QCoreApplication::translate("Scene", "Symmetry");
    case Guide::FourPoint: return QCoreApplication::translate("Scene", "4-Point Continuous Perspective");
    case Guide::FivePoint: return QCoreApplication::translate("Scene", "5-Point Perspective (Fish-Eye)");
    default: return QCoreApplication::translate("Scene", "Line");
    }
}

QJsonValue Scene::guideToJson(Guide const& guide)
{
    // A line across or down: [down, where] (an object when it has a name); a guide to draw along: its kind and its points.
    if (guide.kind == Guide::Line) {
        if (guide.name.isEmpty())
            return QJsonArray{guide.vertical, guide.at};
        return QJsonObject{{"line", true}, {"vertical", guide.vertical}, {"at", guide.at}, {"name", guide.name}};
    }
    QJsonArray points;
    for (QPointF const& point : guide.points)
        points.append(QJsonArray{point.x(), point.y()});
    QJsonObject object{{"kind", int(guide.kind)}, {"points", points}};
    if (!guide.name.isEmpty())
        object["name"] = guide.name;
    return object;
}

bool Scene::guideFromJson(QJsonValue const& value, Guide* guide)
{
    *guide = Guide();
    if (value.isArray() && value.toArray().size() == 2) {
        guide->vertical = value.toArray().at(0).toBool();
        guide->at = value.toArray().at(1).toDouble();
        return true;
    }
    if (!value.isObject())
        return false;
    QJsonObject const g = value.toObject();
    guide->name = g["name"].toString();
    if (g["line"].toBool()) {
        guide->vertical = g["vertical"].toBool();
        guide->at = g["at"].toDouble();
        return true;
    }
    guide->kind = Guide::Kind(std::clamp(g["kind"].toInt(1), 1, int(Guide::kLastKind)));
    for (QJsonValue const& pv : g["points"].toArray())
        guide->points.push_back(QPointF(pv.toArray().at(0).toDouble(), pv.toArray().at(1).toDouble()));
    return true;
}

Scene::Guide Scene::makeGuide(Guide::Kind kind) const
{
    Guide guide;
    guide.kind = kind;
    double const w = width, h = height;
    switch (kind) {
    case Guide::Ruler:
        // A level line across the middle half of the picture, to turn and move.
        guide.points = {QPointF(w * 0.25, h * 0.5), QPointF(w * 0.75, h * 0.5)};
        break;
    case Guide::OnePoint:
        guide.points = {QPointF(w * 0.5, h * 0.5)};
        break;
    case Guide::TwoPoint:
        // The two vanishing points start out beyond the sides of the picture, on a level horizon.
        guide.points = {QPointF(-w * 0.5, h * 0.5), QPointF(w * 1.5, h * 0.5)};
        break;
    case Guide::ThreePoint:
        // ... and the third far below: the scene looked down on.
        guide.points = {QPointF(-w * 0.5, h * 0.35), QPointF(w * 1.5, h * 0.35), QPointF(w * 0.5, h * 2.5)};
        break;
    case Guide::Symmetry:
        // An upright axis down the middle of the picture: what is drawn on one side appears on the other.
        guide.points = {QPointF(w * 0.5, h * 0.2), QPointF(w * 0.5, h * 0.8)};
        break;
    case Guide::FourPoint:
        // The two ends of the horizon: the four vanishing points are spread evenly along it.
        guide.points = {QPointF(-w * 0.25, h * 0.5), QPointF(w * 1.25, h * 0.5)};
        break;
    case Guide::FivePoint:
        // The middle of the picture, and a point on the circle round it (the vanishing point at the right).
        guide.points = {QPointF(w * 0.5, h * 0.5), QPointF(w * 0.5 + h * 0.45, h * 0.5)};
        break;
    default:
        break;      // a grid has no points: it is the same everywhere
    }
    return guide;
}

QVector<QPointF> Scene::guideVanishingPoints(Guide const& guide)
{
    if (guide.points.size() < 2)
        return {};
    QPointF const a = guide.points[0], b = guide.points[1];
    if (guide.kind == Guide::FourPoint)
        return {a, a + (b - a) / 3.0, a + (b - a) * 2.0 / 3.0, b};
    if (guide.kind == Guide::FivePoint) {
        QPointF const out = b - a, turned(-out.y(), out.x());
        return {a - out, b, a - turned, a + turned};
    }
    return {};
}

// The circle through three places; false when they lie in a line (or as good as).
static bool circleThrough(QPointF a, QPointF p, QPointF b, QPointF* centre, double* radius)
{
    QPointF const u = p - a, v = b - a;
    double const cross = u.x() * v.y() - u.y() * v.x();
    double const lu = u.x() * u.x() + u.y() * u.y(), lv = v.x() * v.x() + v.y() * v.y();
    if (std::abs(cross) < 1e-6 * std::sqrt(lu * lv) || lu < 1e-12 || lv < 1e-12)
        return false;
    QPointF const c = a + QPointF(v.y() * lu - u.y() * lv, u.x() * lv - v.x() * lu) / (2.0 * cross);
    *centre = c;
    *radius = std::hypot(c.x() - a.x(), c.y() - a.y());
    return *radius < 1e7;
}

bool Scene::guideArc(Guide const& guide, QPointF at, int way, QPointF* centre, double* radius)
{
    QVector<QPointF> const v = guideVanishingPoints(guide);
    if (v.size() != 4 || way < 0 || way > 1)
        return false;
    if (guide.kind == Guide::FourPoint)
        return way == 0 ? circleThrough(v[0], at, v[2], centre, radius) : circleThrough(v[1], at, v[3], centre, radius);
    return way == 0 ? circleThrough(v[0], at, v[1], centre, radius) : circleThrough(v[2], at, v[3], centre, radius);
}

static QPointF unit(QPointF v)
{
    double const length = std::hypot(v.x(), v.y());
    return length > 1e-9 ? v / length : QPointF();
}

QVector<QPointF> Scene::guideDirections(Guide const& guide, QPointF at)
{
    static QPointF const across(1.0, 0.0), up(0.0, 1.0);
    QVector<QPointF> ways;
    auto towards = [&](int point) {
        if (point < guide.points.size()) {
            QPointF const way = unit(guide.points[point] - at);
            if (!way.isNull())
                ways.push_back(way);
        }
    };
    switch (guide.kind) {
    case Guide::Ruler:
        if (guide.points.size() >= 2) {
            QPointF const way = unit(guide.points[1] - guide.points[0]);
            if (!way.isNull())
                ways.push_back(way);
        }
        break;
    case Guide::SquareGrid:
        ways = {across, up};
        break;
    case Guide::Isometric: {
        // Thirty degrees up to the right, thirty up to the left, and straight up.
        double const sixth = 3.14159265358979323846 / 6.0;
        double const c = std::cos(sixth), s = std::sin(sixth);
        ways = {QPointF(c, -s), QPointF(c, s), up};
        break;
    }
    case Guide::OnePoint:
        towards(0);
        ways.push_back(across);
        ways.push_back(up);
        break;
    case Guide::TwoPoint:
        towards(0);
        towards(1);
        ways.push_back(up);
        break;
    case Guide::ThreePoint:
        towards(0);
        towards(1);
        towards(2);
        break;
    case Guide::FourPoint:
    case Guide::FivePoint: {
        QVector<QPointF> const v = guideVanishingPoints(guide);
        if (v.size() != 4)
            break;
        for (int way = 0; way < 2; ++way) {
            QPointF centre;
            double radius = 0.0;
            QPointF const chord = guide.kind == Guide::FourPoint ? v[way + 2] - v[way] : (way == 0 ? v[1] - v[0] : v[3] - v[2]);
            if (guideArc(guide, at, way, &centre, &radius)) {
                QPointF const out = unit(at - centre);
                ways.push_back(QPointF(-out.y(), out.x()));
            } else {
                ways.push_back(unit(chord));
            }
        }
        if (guide.kind == Guide::FourPoint) {
            QPointF const along = unit(guide.points[1] - guide.points[0]);
            ways.push_back(QPointF(-along.y(), along.x()));
        } else {
            QPointF const out = unit(at - guide.points[0]);
            ways.push_back(out.isNull() ? up : out);
        }
        break;
    }
    default:
        break;      // a plain line is snapped to, not drawn along
    }
    return ways;
}

QPointF Scene::alongGuide(QPointF from, QPointF to, QPointF way)
{
    // The place on the line through `from` running `way` that is nearest to `to`.
    QPointF const d = to - from;
    return from + way * (d.x() * way.x() + d.y() * way.y());
}

int Scene::nearestGuideDirection(QVector<QPointF> const& ways, QPointF drag)
{
    QPointF const d = unit(drag);
    int best = -1;
    double most = -1.0;
    for (int i = 0; i < ways.size(); ++i) {
        double const along = std::abs(d.x() * ways[i].x() + d.y() * ways[i].y());      // either way along it
        if (along > most) {
            most = along;
            best = i;
        }
    }
    return best;
}
