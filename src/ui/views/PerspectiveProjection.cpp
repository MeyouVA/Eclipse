#include "ui/views/PerspectiveView.h"
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include <cmath>

PerspectiveView::Point PerspectiveView::viewPoint(Point point) const
{
    Scene const& scene = m_document->scene();
    point.x -= scene.width / 2.0 + m_viewX;
    point.y -= scene.height / 2.0 + m_viewY;
    point.z -= m_viewZ;
    constexpr double radians = 3.14159265358979323846 / 180.0;
    double const cy = std::cos(m_yaw * radians), sy = std::sin(m_yaw * radians);
    double const cp = std::cos(m_pitch * radians), sp = std::sin(m_pitch * radians);
    double const x = point.x * cy + point.z * sy, z = -point.x * sy + point.z * cy;
    return {x, point.y * cp - z * sp, point.y * sp + z * cp};
}

QPointF PerspectiveView::projectView(Point point) const
{
    Scene const& scene = m_document->scene();
    double const eye = 3.5 * std::max(scene.width, scene.height);
    double const left = eye - point.z;
    double const scale = eye / std::copysign(std::max(std::abs(left), 1e-9), left);
    double const fit = std::min(width() / double(scene.width), height() / double(scene.height)) * 0.5 * m_zoom;
    return QPointF(width() / 2.0, height() / 2.0) + m_pan + QPointF(point.x, point.y) * (scale * fit);
}

double PerspectiveView::distanceOf(double x, double y, double z) const
{
    return -viewPoint({x, y, z}).z;
}

QPointF PerspectiveView::project(double x, double y, double z, bool* seen) const
{
    Point const point = viewPoint({x, y, z});
    Scene const& scene = m_document->scene();
    double const distance = 3.5 * std::max(scene.width, scene.height) - point.z;
    if (seen)
        *seen = distance >= m_nearDistance && distance <= m_farDistance;
    return projectView(point);
}

QPolygonF PerspectiveView::projectPolygon(QVector<Point> polygon) const
{
    for (Point& point : polygon)
        point = viewPoint(point);
    double const eye = 3.5 * std::max(m_document->scene().width, m_document->scene().height);
    auto cut = [&](double plane, bool near) {
        QVector<Point> kept;
        if (polygon.isEmpty())
            return;
        auto value = [&](Point p) { return (near ? 1.0 : -1.0) * (eye - p.z - plane); };
        Point previous = polygon.last();
        double before = value(previous);
        for (Point const& point : std::as_const(polygon)) {
            double const after = value(point);
            if ((before >= 0) != (after >= 0)) {
                double const t = before / (before - after);
                kept.push_back({previous.x + (point.x - previous.x) * t, previous.y + (point.y - previous.y) * t,
                                previous.z + (point.z - previous.z) * t});
            }
            if (after >= 0)
                kept.push_back(point);
            previous = point;
            before = after;
        }
        polygon = std::move(kept);
    };
    cut(m_nearDistance, true);
    cut(m_farDistance, false);
    QPolygonF projected;
    for (Point const& point : std::as_const(polygon))
        projected << projectView(point);
    return projected;
}

bool PerspectiveView::projectSegment(Point a, Point b, QLineF& line) const
{
    a = viewPoint(a); b = viewPoint(b);
    double const eye = 3.5 * std::max(m_document->scene().width, m_document->scene().height);
    double const start = eye - a.z, delta = a.z - b.z;
    double from = 0.0, to = 1.0;
    if (std::abs(delta) < 1e-12) {
        if (start < m_nearDistance || start > m_farDistance)
            return false;
    } else {
        double first = (m_nearDistance - start) / delta, last = (m_farDistance - start) / delta;
        if (first > last)
            std::swap(first, last);
        from = std::max(from, first); to = std::min(to, last);
        if (from > to)
            return false;
    }
    auto at = [&](double t) { return Point{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; };
    line = QLineF(projectView(at(from)), projectView(at(to)));
    return true;
}

QPolygonF PerspectiveView::layerPolygon(int layer, QTransform* projection, Drawing* displayed) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || !scene.layers[layer].holdsDrawings() || !scene.enabled(layer))
        return {};
    Layer const& item = scene.layers[layer];
    int const index = item.drawingAt(m_frame);
    if (index < 0 || index >= item.drawings.size() || item.drawings[index].isEmpty())
        return {};
    auto world = [&](QPointF local) {
        Point point;
        scene.toSpace(layer, m_frame, local, point.x, point.y, point.z);
        return point;
    };
    Pose const pose = scene.poseOf(layer, m_frame);
    Drawing const shown = Deform::ridden(scene, layer, m_frame, Deform::shown(item.shownAt(m_frame), item, pose, index));
    if (displayed)
        *displayed = shown;
    QRectF const bounds = shown.bounds().adjusted(-4, -4, 4, 4);
    QVector<Point> polygon;
    for (QPointF const& corner : {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()})
        polygon.push_back(world(corner));
    QPolygonF const clipped = projectPolygon(polygon);
    if (projection && clipped.size() >= 3)
        *projection = layerProjection(layer);
    return clipped;
}

QTransform PerspectiveView::layerProjection(int layer) const
{
    Scene const& scene = m_document->scene();
    auto world = [&](QPointF local) {
        Point point;
        scene.toSpace(layer, m_frame, local, point.x, point.y, point.z);
        return point;
    };
    Point const o = viewPoint(world(QPointF(0, 0))), u = viewPoint(world(QPointF(1, 0))), v = viewPoint(world(QPointF(0, 1)));
    double const ax = u.x - o.x, ay = u.y - o.y, az = u.z - o.z;
    double const bx = v.x - o.x, by = v.y - o.y, bz = v.z - o.z;
    double const eye = 3.5 * std::max(scene.width, scene.height);
    double const k = eye * std::min(width() / double(scene.width), height() / double(scene.height)) * 0.5 * m_zoom;
    double const cx = width() / 2.0 + m_pan.x(), cy = height() / 2.0 + m_pan.y(), depth = eye - o.z;
    return QTransform(k * ax - cx * az, k * ay - cy * az, -az,
                      k * bx - cx * bz, k * by - cy * bz, -bz,
                      k * o.x + cx * depth, k * o.y + cy * depth, depth);
}
