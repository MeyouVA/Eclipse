// Eclipse - deformers: what their files share.
#pragma once

#include "model/geometry/Deform.h"

#include "model/geometry/Model3D.h"

#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>

namespace Deform {


struct Chain {
    QVector<QPointF> joints;
    QVector<QTransform> bones;
    double smooth = 0.0;    // how far round a joint the bend is shared out (scene units); 0: as good as a fold
    QVector<SkinDab> skin;  // weights painted for the bones

    Chain(QVector<QPointF> const& rest, QVector<double> const& bend, double smoothness = 0.0, QVector<SkinDab> const& painted = {})
        : joints(rest), smooth(smoothness), skin(painted)
    {
        QPointF origin = rest.isEmpty() ? QPointF() : rest[0];  // where the bone starts, posed
        double angle = 0.0;                                      // how far it is turned in all
        for (int i = 0; i + 1 < rest.size(); ++i) {
            angle += i < bend.size() ? bend[i] : 0.0;
            QTransform t;
            t.translate(origin.x(), origin.y());
            t.rotate(angle);
            t.translate(-rest[i].x(), -rest[i].y());
            bones.push_back(t);
            origin = t.map(rest[i + 1]);
        }
    }

    QPointF map(QPointF p) const
    {
        if (bones.isEmpty())
            return p;
        QVarLengthArray<double, 16> pull(bones.size());
        nearness(p, pull);
        if (!skin.isEmpty()) {
            QVarLengthArray<double, 16> painted(bones.size());
            double total = 0.0;
            for (int i = 0; i < bones.size(); ++i) {
                painted[i] = std::max(0.0, pull[i] + 3.0 * paintedWeight(skin, i, p));
                total += painted[i];
            }
            if (total > 1e-12)
                for (int i = 0; i < bones.size(); ++i)
                    pull[i] = painted[i];
        }
        double total = 0.0;
        QPointF sum;
        for (int i = 0; i < bones.size(); ++i) {
            total += pull[i];
            sum += bones[i].map(p) * pull[i];
        }
        return total > 0.0 ? sum / total : p;
    }

    // How strongly each bone pulls on `p` by how near it is.
    void nearness(QPointF p, QVarLengthArray<double, 16>& pull) const
    {
        if (smooth > 1.0) {
            QVarLengthArray<double, 16> away(bones.size());
            double nearest = 1e300;
            for (int i = 0; i < bones.size(); ++i) {
                away[i] = distanceToSegment(p, joints[i], joints[i + 1]);
                nearest = std::min(nearest, away[i]);
            }
            for (int i = 0; i < bones.size(); ++i) {
                double const share = std::max(0.0, 1.0 - (away[i] - nearest) / smooth);
                pull[i] = share * share;
            }
            return;
        }
        for (int i = 0; i < bones.size(); ++i) {
            double const d = distanceToSegment(p, joints[i], joints[i + 1]);
            pull[i] = 1.0 / ((d * d + 1.0) * (d * d + 1.0));
        }
    }
};

template <class Add> void pieces(QPointF a, QPointF b, Add add)
{
    int const steps = std::clamp(int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / 6.0)), 1, 400);
    for (int k = 0; k < steps; ++k)
        add(double(k) / steps);
}



template <class Map> Drawing mapped(Drawing const& drawing, Map const& map)
{
    Drawing out;
    out.pinned = drawing.pinned;
    out.pictures = drawing.pictures;    // a picture is not bent: it stays as it lies
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Fill const& fill : drawing.fillsOf(sheet)) {
            Fill bent = fill;
            bent.outlines.clear();
            for (QPolygonF const& outline : fill.outlines) {
                QPolygonF polygon;
                for (int i = 0; i < outline.size(); ++i) {
                    QPointF const a = outline[i], b = outline[(i + 1) % outline.size()];
                    pieces(a, b, [&](double u) { polygon.push_back(map(a + (b - a) * u)); });
                }
                bent.outlines.push_back(polygon);
            }
            // A gradient placed in the area goes where the area goes.
            if (bent.gradPlaced) {
                bent.gradFrom = map(fill.gradFrom);
                bent.gradTo = map(fill.gradTo);
            }
            out.fillsOf(sheet).push_back(bent);
        }
        for (Stroke const& stroke : drawing.strokesOf(sheet)) {
            Stroke bent = stroke;
            bent.curve = false;
            bent.closed = false;  // the path already comes back to its start
            bent.points.clear();
            bent.thickness.clear();     // (its places carry the thickness)
            QVector<StrokePoint> const line = stroke.path();
            for (int i = 0; i + 1 < line.size(); ++i) {
                StrokePoint const& a = line[i];
                StrokePoint const& b = line[i + 1];
                pieces(a.pos, b.pos, [&](double u) {
                    bent.points.push_back({map(a.pos + (b.pos - a.pos) * u), float(a.pressure + (b.pressure - a.pressure) * u)});
                });
            }
            if (!line.isEmpty())
                bent.points.push_back({map(line.last().pos), line.last().pressure});
            QVector<StrokePoint> kept;
            for (StrokePoint const& q : bent.points) {
                while (kept.size() >= 2 && kept.last().pressure == q.pressure && kept[kept.size() - 2].pressure == q.pressure
                       && distanceToSegment(kept.last().pos, kept[kept.size() - 2].pos, q.pos) < 1e-6)
                    kept.removeLast();
                kept.push_back(q);
            }
            bent.points = kept;
            out.strokesOf(sheet).push_back(bent);
        }
    }
    return out;
}


}  // namespace Deform
