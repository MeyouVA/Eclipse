// Eclipse - the Camera view: painting a line, an area, a drawing, a layer with its effects, a whole frame.
#include "ui/canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

// A stroke is drawn as round-capped segments whose width follows the pressure.
void Canvas::paintStroke(QPainter& painter, Stroke const& stroke, QBrush const& color)
{
    if (stroke.points.isEmpty() || stroke.width <= 0.0f)
        return;     // nothing drawn; or a line with no thickness, which is not seen
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
    QPainterPath outline;
    outline.setFillRule(Qt::WindingFill);
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
                outline.addEllipse(pos[i], half[i], half[i]);  // and rounded over
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
        outline.addEllipse(pos[0], half[0], half[0]);
    if (!stroke.closed && stroke.capEnd == Stroke::RoundCap)
        outline.addEllipse(pos[n - 1], half[n - 1], half[n - 1]);
    if (stroke.closed && stroke.join == Stroke::RoundJoin)
        outline.addEllipse(pos[0], half[0], half[0]);  // where it closes
    painter.drawPath(outline);
}

void Canvas::paintFill(QPainter& painter, Fill const& fill, QBrush const& color)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (QPolygonF const& outline : fill.outlines) {
        path.addPolygon(outline);
        path.closeSubpath();
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
                               QTransform const& sceneToDevice)
{
    Layer const& layer = scene.layers[l];
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
        if (symbol)
            paintSymbol(off, scene, layer.symbolId, symbolFrame);
        else
            paintDrawingOpaque(off, scene, drawing, nullptr);
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
        if (m_art == LineArt)
            for (int i : m_selPictures)
                if (i >= 0 && i < shown.pictures.size())
                    shown.pictures[i].transform(m_dragTransform);
    }
    return shown;
}
