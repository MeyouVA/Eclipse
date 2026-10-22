// Eclipse - the Camera view: pencil lines - profiles, retouch, wiggle, soft tips, the eraser's area, opacity, even samples.
#include "ui/canvas/Canvas.h"
#include <QSet>
#include <QRandomGenerator>
#include <QLineF>
#include <algorithm>

#include <QInputDialog>
#include <QPainterPath>
#include <QSettings>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "ui/window/MainWindowPrivate.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include "ui/canvas/CanvasTidyShared.h"

using namespace canvas_tidy_shared;
#include "ui/canvas/CanvasPencilLinesShared.h"

using namespace canvas_pencil_lines_shared;

float Canvas::profileThickness(QVector<float> const& profile, double along)
{
    if (profile.isEmpty())
        return 1.0f;
    if (profile.size() == 1)
        return profile.first();
    double const at = std::clamp(along, 0.0, 1.0) * (profile.size() - 1);
    int const k = std::min(int(at), int(profile.size()) - 2);
    double const t = at - k;
    return float(std::max(0.05, profile[k] + (profile[k + 1] - profile[k]) * t));
}

void Canvas::applyProfile(Stroke& stroke, QVector<float> const& profile)
{
    QVector<double> const along = distancesAlong(stroke.points);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (total <= 0.0)
        return;
    for (int i = 0; i < stroke.points.size(); ++i)
        stroke.points[i].pressure *= profileThickness(profile, along[i] / total);
}

QVector<float> Canvas::selectedLineProfile() const
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty() || m_selStrokes.first() < 0 || m_selStrokes.first() >= d->strokes.size())
        return {};
    // Along the line as drawn (its curve), the thickness at 32 evenly spaced places, as shares of the thickest.
    QVector<StrokePoint> const run = d->strokes[m_selStrokes.first()].path();
    QVector<double> const along = distancesAlong(run);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (run.size() < 2 || total <= 0.0)
        return {};
    QVector<float> profile;
    int k = 0;
    for (int i = 0; i < 32; ++i) {
        double const at = total * i / 31.0;
        while (k + 2 < run.size() && along[k + 1] < at)
            ++k;
        double const span = along[k + 1] - along[k];
        double const t = span > 0.0 ? std::clamp((at - along[k]) / span, 0.0, 1.0) : 0.0;
        profile.push_back(float(run[k].pressure + (run[k + 1].pressure - run[k].pressure) * t));
    }
    float const most = *std::max_element(profile.begin(), profile.end());
    if (most <= 0.0f)
        return {};
    for (float& share : profile)
        share /= most;
    return profile;
}

void Canvas::retouchAt(QPointF p, int mode)
{
    bool const thinner = mode == 1 || mode == 3, opacity = mode >= 2;
    double const reach = std::max(m_brushWidth / 2.0, 4.0 / std::max(m_zoom, 1e-6));
    for (int index = 0; index < m_eraseArt.strokes.size(); ++index) {
        Stroke& line = m_eraseArt.strokes[index];
        if (line.width <= 0.0f || line.points.size() < 2 || !line.hit(p, reach))
            continue;
        // Apply to Single Stroke: the first line rubbed, and that one only.
        if (m_singleNow && m_onlyLine == -1)
            m_onlyLine = index;
        if (m_singleNow && m_onlyLine != index)
            continue;
        if (!opacity) {
            // Thicker or thinner: the thickness points under the pen (one is put there when none is near), most at its middle.
            line.ownThickness();
            bool keyNear = false;
            for (ThicknessPoint const& key : std::as_const(line.thickness))
                keyNear = keyNear || QLineF(line.pointAt(key.at), p).length() <= reach * 0.5;
            if (!keyNear)
                line.thicknessPointAt(line.placeAt(p));
            for (ThicknessPoint& key : line.thickness) {
                double const distance = QLineF(line.pointAt(key.at), p).length();
                if (distance > reach)
                    continue;
                double const fall = 1.0 - distance / reach;
                double const step = 1.0 + 0.15 * fall * (thinner ? -1.0 : 1.0);
                key.left = float(std::clamp(key.left * step, 0.02, 24.0));
                key.right = float(std::clamp(key.right * step, 0.02, 24.0));
                m_inked = true;
            }
            continue;
        }
        bool pointNear = false;
        for (StrokePoint const& point : std::as_const(line.points))
            pointNear = pointNear || QLineF(point.pos, p).length() <= reach * 0.5;
        if (!pointNear)
            line.insertPoint(p);
        for (StrokePoint& point : line.points) {
            double const distance = QLineF(point.pos, p).length();
            if (distance > reach)
                continue;
            double const fall = 1.0 - distance / reach;
            double const step = 1.0 + 0.15 * fall * (thinner ? -1.0 : 1.0);
            if (opacity)
                point.opacity = float(std::clamp(point.opacity * step, 0.02, 1.0));
            else
                point.pressure = float(std::clamp(point.pressure * step, 0.05, 4.0));
            m_inked = true;
        }
    }
    update();
}

void Canvas::wiggleSelectedLines(double amount, double every, quint32 seed)
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty() || amount <= 0.0 || every <= 0.0) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    QRandomGenerator random(seed);
    Drawing edited = *d;
    for (int index : std::as_const(m_selStrokes)) {
        if (index < 0 || index >= edited.strokes.size())
            continue;
        Stroke& line = edited.strokes[index];
        QVector<StrokePoint> const run = line.path();
        QVector<double> const along = distancesAlong(run);
        double const total = along.isEmpty() ? 0.0 : along.last();
        if (run.size() < 2 || total <= 0.0)
            continue;
        // Places every so far along the line, each pushed to one side or the other of it (the ends stay put).
        int const count = std::max(2, int(std::ceil(total / every)));
        QVector<StrokePoint> wiggled;
        int k = 0;
        for (int i = 0; i <= count; ++i) {
            double const at = total * i / count;
            while (k + 2 < run.size() && along[k + 1] < at)
                ++k;
            double const span = along[k + 1] - along[k];
            double const t = span > 0.0 ? std::clamp((at - along[k]) / span, 0.0, 1.0) : 0.0;
            QPointF const pos = run[k].pos + (run[k + 1].pos - run[k].pos) * t;
            QPointF const way = run[k + 1].pos - run[k].pos;
            double const length = std::hypot(way.x(), way.y());
            QPointF const side = length > 1e-9 ? QPointF(-way.y(), way.x()) / length : QPointF();
            double const push = (i == 0 || i == count) ? 0.0 : (random.generateDouble() * 2.0 - 1.0) * amount;
            wiggled.push_back({pos + side * push, float(run[k].pressure + (run[k + 1].pressure - run[k].pressure) * t)});
        }
        line.points = wiggled;
        line.thickness.clear();     // (its places carry the thickness the line had there)
        line.curve = false;
        line.fit(std::max(amount * 0.05, 0.01), 180.0);
    }
    commitDrawing(edited, tr("Pencil Line Wiggle"));
    update();
}

void Canvas::onActionPencilLineWiggle()
{
    if (m_selStrokes.isEmpty()) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    bool ok = false;
    double const amount = QInputDialog::getDouble(this, tr("Pencil Line Wiggle"), tr("How far the line bends to each side:"), 4.0, 0.1, 500.0, 1, &ok);
    if (!ok)
        return;
    double const every = QInputDialog::getDouble(this, tr("Pencil Line Wiggle"), tr("A bend every:"), 20.0, 1.0, 2000.0, 1, &ok);
    if (ok)
        wiggleSelectedLines(amount, every, QRandomGenerator::global()->generate());
}

void Canvas::softenTips(Stroke& line, double share)
{
    // Points this share of the way in from each end (where the line has none near), then the ends made clear.
    QVector<StrokePoint> const run = line.path();
    QVector<double> const along = distancesAlong(run);
    double const total = along.isEmpty() ? 0.0 : along.last();
    if (line.points.size() < 2 || total <= 0.0)
        return;
    for (double at : {total * share, total * (1.0 - share)}) {
        int k = 0;
        while (k + 2 < run.size() && along[k + 1] < at)
            ++k;
        double const span = along[k + 1] - along[k];
        QPointF const place = run[k].pos + (run[k + 1].pos - run[k].pos) * (span > 0.0 ? (at - along[k]) / span : 0.0);
        bool near = false;
        for (StrokePoint const& point : std::as_const(line.points))
            near = near || QLineF(point.pos, place).length() < total * 0.02;
        if (!near)
            line.insertPoint(place);
    }
    // The opacity along the line: clear at the ends, solid from that share in.
    QVector<double> const placed = distancesAlong(line.points);
    double const length = placed.last();
    for (int i = 0; i < line.points.size(); ++i) {
        double const from = std::min(placed[i], length - placed[i]) / std::max(length * share, 1e-9);
        line.points[i].opacity = float(std::clamp(from, 0.0, 1.0));
    }
}

void Canvas::onActionSetPencilSoftTip()
{
    Drawing const* const d = currentDrawing();
    if (!d || m_selStrokes.isEmpty()) {
        emit message(tr("Select pencil lines first (the Select tool)."));
        return;
    }
    Drawing edited = *d;
    for (int index : std::as_const(m_selStrokes))
        if (index >= 0 && index < edited.strokes.size() && edited.strokes[index].width > 0.0f && !edited.strokes[index].closed)
            softenTips(edited.strokes[index], 0.15);
    commitDrawing(edited, tr("Set Pencil Opacity Soft Tip"));
    update();
}

void Canvas::onActionSetPencilSoftTipAllDrawings()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[m_layer];
    m_document->beginMacro(tr("Set Pencil Opacity Soft Tip On All Drawings"));
    // Every drawing the layer shows, at the first frame that shows it.
    QSet<int> done;
    for (int frame = 0; frame < layer.cells.size(); ++frame) {
        int const index = layer.cells[frame];
        if (index < 0 || index >= layer.drawings.size() || done.contains(index))
            continue;
        done.insert(index);
        Drawing edited = layer.drawings[index];
        bool any = false;
        for (Stroke& line : edited.strokes)
            if (line.width > 0.0f && !line.closed && line.points.size() >= 2) {
                softenTips(line, 0.15);
                any = true;
            }
        if (any)
            m_document->setDrawing(m_layer, frame, edited, tr("Set Pencil Opacity Soft Tip On All Drawings"));
    }
    m_document->endMacro();
    update();
}

QVector<QPolygonF> Canvas::eraserArea(Stroke const& path) const
{
    if (!m_eraserSquare)
        return path.outlinePolygons();
    double const r = std::max(path.width / 2.0, 0.25);
    auto square = [r](QPointF c) { return QPolygonF({c + QPointF(-r, -r), c + QPointF(r, -r), c + QPointF(r, r), c + QPointF(-r, r)}); };
    auto hull = [](QVector<QPointF> points) {
        std::sort(points.begin(), points.end(), [](QPointF a, QPointF b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
        auto cross = [](QPointF o, QPointF a, QPointF b) { return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
        QVector<QPointF> lower, upper;
        for (QPointF const& p : points) {
            while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.last(), p) <= 0)
                lower.removeLast();
            lower.push_back(p);
        }
        for (int i = int(points.size()) - 1; i >= 0; --i) {
            while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper.last(), points[i]) <= 0)
                upper.removeLast();
            upper.push_back(points[i]);
        }
        lower.removeLast();
        upper.removeLast();
        return QPolygonF(lower + upper);
    };
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    if (path.points.size() == 1)
        area.addPolygon(square(path.points[0].pos));
    for (int i = 0; i + 1 < path.points.size(); ++i) {
        QPolygonF const a = square(path.points[i].pos), b = square(path.points[i + 1].pos);
        QPolygonF swept = hull(QVector<QPointF>(a.begin(), a.end()) + QVector<QPointF>(b.begin(), b.end()));
        area.addPolygon(swept);
        area.closeSubpath();
    }
    QVector<QPolygonF> polygons;
    for (QPolygonF polygon : area.simplified().toSubpathPolygons()) {
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        if (polygon.size() >= 3)
            polygons.push_back(polygon);
    }
    return polygons;
}

void Canvas::smoothOpacity(Stroke& line, int percent)
{
    int const n = int(line.points.size());
    int const reach = int(std::lround(percent / 10.0));     // up to ten points on each side
    if (n < 3 || reach <= 0)
        return;
    QVector<float> was(n);
    for (int i = 0; i < n; ++i)
        was[i] = line.points[i].opacity;
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        int count = 0;
        for (int k = std::max(0, i - reach); k <= std::min(n - 1, i + reach); ++k) {
            sum += was[k];
            ++count;
        }
        line.points[i].opacity = float(sum / count);
    }
}

QVector<StrokePoint> Canvas::evenlySampled(QVector<StrokePoint> const& samples, double step)
{
    if (samples.size() < 2 || step <= 0.0)
        return samples;
    QVector<StrokePoint> out;
    out.push_back(samples.first());
    for (int i = 1; i < samples.size(); ++i) {
        StrokePoint const& a = samples[i - 1];
        StrokePoint const& b = samples[i];
        int const pieces = std::clamp(int(std::ceil(QLineF(a.pos, b.pos).length() / step)), 1, 2000);
        for (int k = 1; k < pieces; ++k) {
            double const t = double(k) / pieces;
            StrokePoint between = a;
            between.pos = a.pos + (b.pos - a.pos) * t;
            between.pressure = float(a.pressure + (b.pressure - a.pressure) * t);
            between.lean = float(a.lean + (b.lean - a.lean) * t);
            between.opacity = float(a.opacity + (b.opacity - a.opacity) * t);
            between.handles = between.corner = false;
            out.push_back(between);
        }
        out.push_back(b);
    }
    return out;
}
