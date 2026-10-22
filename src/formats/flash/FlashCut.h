// Eclipse - cutting what a Flash mask covers to the mask: shared by the .swf and .fla readers.
//
// What lies wholly inside the mask is kept as it is and what lies wholly outside it is left out - told quickly from a
// picture of the mask; only what crosses its edge is cut, as outlines (what comes from a Flash file has no curves to
// keep), lines as the areas they cover.  Pictures are given a clip.
#pragma once

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "model/scene/Scene.h"

namespace FlashCut {

// A mask as a picture of where it lets things through.
class MaskRaster {
public:
    MaskRaster(QVector<QPolygonF> const& area, QRectF const& reach)
    {
        double const side = std::max(reach.width(), reach.height());
        double const scale = side > 0.0 ? std::min(2.0, 1024.0 / side) : 1.0;
        m_toImage = QTransform::fromTranslate(-reach.left() + 2.0 / scale, -reach.top() + 2.0 / scale) * QTransform::fromScale(scale, scale);
        m_image = QImage(std::max(1, int(std::ceil(reach.width() * scale)) + 4), std::max(1, int(std::ceil(reach.height() * scale)) + 4), QImage::Format_Grayscale8);
        m_image.fill(0);
        QPainter p(&m_image);
        p.setTransform(m_toImage);
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (QPolygonF const& ring : area)
            path.addPolygon(ring);
        p.fillPath(path, Qt::white);
        m_unit = 1.0 / scale;
    }
    // Whether the place, and all round it within `margin` (and a pixel of the raster more), is inside (or outside).
    bool inside(QPointF at, double margin) const { return all(at, margin, true); }
    bool outside(QPointF at, double margin) const { return all(at, margin, false); }
    // Every place along the line through the points (a step of the raster at a time) passes the test.
    template <typename Test>
    bool along(QPolygonF const& points, bool closed, Test test) const
    {
        int const n = int(points.size());
        for (int i = 0; i < n; ++i) {
            if (!test(points[i]))
                return false;
            if (i + 1 == n && !closed)
                break;
            QPointF const a = points[i], b = points[(i + 1) % n];
            int const steps = int(QLineF(a, b).length() / m_unit);
            for (int s = 1; s < steps; ++s)
                if (!test(a + (b - a) * (double(s) / steps)))
                    return false;
        }
        return true;
    }

private:
    bool all(QPointF at, double margin, bool in) const
    {
        double const m = margin + 1.5 * m_unit;
        for (QPointF d : {QPointF(0, 0), QPointF(m, 0), QPointF(-m, 0), QPointF(0, m), QPointF(0, -m)})
            if (set(at + d) != in)
                return false;
        return true;
    }
    bool set(QPointF at) const
    {
        QPointF const p = m_toImage.map(at);
        int const x = int(std::floor(p.x())), y = int(std::floor(p.y()));
        return x >= 0 && y >= 0 && x < m_image.width() && y < m_image.height() && m_image.constScanLine(y)[x] > 127;
    }
    QImage m_image;
    QTransform m_toImage;
    double m_unit = 1.0;
};

// The part of a picture that shows (its clip, in its own pixels; empty: all of it), cut to an area of the drawing.
inline QVector<QPolygonF> clipTo(Picture const& picture, QVector<QPolygonF> const& area)
{
    QTransform const back = picture.placement.inverted();
    QPainterPath cut;
    cut.setFillRule(Qt::WindingFill);
    for (QPolygonF const& ring : area)
        cut.addPolygon(back.map(ring));
    // (No need to merge the outlines first: the winding rule makes one area of them all the same.)
    if (!picture.clip.isEmpty()) {
        QPainterPath own;
        for (QPolygonF const& ring : picture.clip)
            own.addPolygon(ring);
        cut = own.intersected(cut);
    }
    QVector<QPolygonF> out;
    for (QPolygonF const& ring : cut.toSubpathPolygons())
        if (ring.size() >= 3)
            out.push_back(ring);
    return out;
}

// The area a drawing covers as a mask: its areas and its pictures' boxes.  (Flash leaves a mask's lines out: only what is
// filled lets anything through.)
inline QVector<QPolygonF> areaOf(Drawing const& d)
{
    QVector<QPolygonF> area;
    for (Fill const& f : d.fills)
        area += f.outlines;
    for (Picture const& p : d.pictures)
        area.push_back(p.placement.map(QPolygonF(QRectF(QPointF(0, 0), QSizeF(p.size)))));
    return area;
}

// A mask made ready to cut with (its picture and its outline worked out once, for all that lies under it).
class Cutter {
public:
    explicit Cutter(QVector<QPolygonF> area) : m_area(std::move(area))
    {
        for (QPolygonF const& ring : std::as_const(m_area))
            m_reach = m_reach.united(ring.boundingRect());
        if (m_area.isEmpty())
            return;
        m_raster = std::make_unique<MaskRaster>(m_area, m_reach);
        m_mask.setFillRule(Qt::WindingFill);
        for (QPolygonF const& ring : std::as_const(m_area))
            m_mask.addPolygon(ring);

    }

    // What of `in` lies inside the mask, added to `out` (an empty mask lets nothing through).
    void cut(Drawing const& in, Drawing& out) const
    {
        if (m_area.isEmpty())
            return;
        MaskRaster const& raster = *m_raster;
        auto area = [&](Fill const& whole) {
            if (!whole.bounds().intersects(m_reach))
                return;
            bool within = true;
            for (QPolygonF const& ring : whole.outlines)
                within = within && raster.along(ring, true, [&](QPointF p) { return raster.inside(p, 0.0); });
            if (within) {
                out.fills.push_back(whole);
                return;
            }
            QPainterPath own;
            own.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& ring : whole.outlines)
                own.addPolygon(ring);
            Fill piece = whole;
            piece.outlines.clear();
            piece.contours.clear();
            for (QPolygonF const& ring : own.intersected(m_mask).toSubpathPolygons())
                if (ring.size() >= 3)
                    piece.outlines.push_back(ring);
            if (!piece.outlines.isEmpty())
                out.fills.push_back(piece);
        };
        for (Fill const& f : in.fills)
            area(f);
        for (Stroke const& s : in.strokes) {
            QPolygonF line;
            for (StrokePoint const& p : s.points)
                line << p.pos;
            double const half = s.width / 2.0;
            if (!line.boundingRect().adjusted(-half, -half, half, half).intersects(m_reach)
                || raster.along(line, s.closed, [&](QPointF p) { return raster.outside(p, half); }))
                continue;
            if (raster.along(line, s.closed, [&](QPointF p) { return raster.inside(p, half); })) {
                out.strokes.push_back(s);
                continue;
            }
            Fill asArea = Fill::fromStroke(s, 0.25);
            asArea.color = s.color;
            asArea.order = s.order;
            asArea.swatch = s.swatch;
            area(asArea);
        }
        for (Picture p : in.pictures) {
            if (!p.bounds().intersects(m_reach))
                continue;
            p.clip = clipTo(p, m_area);
            if (!p.clip.isEmpty())
                out.pictures.push_back(p);
        }
    }

private:
    QVector<QPolygonF> m_area;
    QRectF m_reach;
    std::unique_ptr<MaskRaster> m_raster;
    QPainterPath m_mask;
};

// What of `in` lies inside `area`, added to `out` (an empty area lets nothing through).
inline void cutInto(Drawing const& in, QVector<QPolygonF> const& area, Drawing& out)
{
    Cutter(area).cut(in, out);
}

}  // namespace FlashCut
