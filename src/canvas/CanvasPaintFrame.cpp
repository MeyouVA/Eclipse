// Eclipse - the Camera view: painting a whole frame, the preview of what is being drawn, the node graph's frame.
#include "canvas/Canvas.h"
#include "model/scene/StrokePrivate.h"

#include <atomic>
#include <QPainter>
#include <QPainterPath>

#include <QHash>
#include <QSet>

#include <cmath>
#include <functional>
#include <limits>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "canvas/PencilTexture.h"
#include "render/effects/Effects.h"
#include "canvas/CanvasPaintShared.h"

using namespace canvas_paint_shared;

static QRgb blendPixel(QRgb s, QRgb d, int mode)
{
    double const sa = qAlpha(s) / 255.0, da = qAlpha(d) / 255.0;
    if (sa <= 0.0)
        return d;
    auto mix = [mode](double cs, double cb) {
        auto hard = [](double a, double b) { return a <= 0.5 ? b * 2.0 * a : b + (2.0 * a - 1.0) - b * (2.0 * a - 1.0); };
        switch (mode) {
        case Layer::BlendMultiply: return cs * cb;
        case Layer::BlendScreen: return cs + cb - cs * cb;
        case Layer::BlendOverlay: return hard(cb, cs);
        case Layer::BlendDarken: return std::min(cs, cb);
        case Layer::BlendLighten: return std::max(cs, cb);
        case Layer::BlendColourDodge: return cb <= 0.0 ? 0.0 : cs >= 1.0 ? 1.0 : std::min(1.0, cb / (1.0 - cs));
        case Layer::BlendColourBurn: return cb >= 1.0 ? 1.0 : cs <= 0.0 ? 0.0 : 1.0 - std::min(1.0, (1.0 - cb) / cs);
        case Layer::BlendHardLight: return hard(cs, cb);
        case Layer::BlendSoftLight: {
            double const dd = cb <= 0.25 ? ((16.0 * cb - 12.0) * cb + 4.0) * cb : std::sqrt(cb);
            return cs <= 0.5 ? cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb) : cb + (2.0 * cs - 1.0) * (dd - cb);
        }
        case Layer::BlendDifference: return std::abs(cb - cs);
        case Layer::BlendExclusion: return cb + cs - 2.0 * cb * cs;
        case Layer::BlendAdd: return std::min(1.0, cs + cb);
        default: return cs;
        }
    };
    auto one = [&](int sc, int dc) {
        double const cs = sc / 255.0 / sa, cb = da > 0.0 ? dc / 255.0 / da : 0.0;
        double const v = sc / 255.0 * (1.0 - da) + dc / 255.0 * (1.0 - sa) + sa * da * std::clamp(mix(std::min(cs, 1.0), std::min(cb, 1.0)), 0.0, 1.0);
        return std::clamp(int(std::lround(v * 255.0)), 0, 255);
    };
    int const a = std::clamp(int(std::lround((sa + da - sa * da) * 255.0)), 0, 255);
    return qRgba(std::min(one(qRed(s), qRed(d)), a), std::min(one(qGreen(s), qGreen(d)), a), std::min(one(qBlue(s), qBlue(d)), a), a);
}

void Canvas::setBlursEnabled(bool on)
{
    gBlurs = on;
}

bool Canvas::blursEnabled()
{
    return gBlurs;
}

void Canvas::setFocusEnabled(bool on)
{
    gFocus = on;
}

bool Canvas::focusEnabled()
{
    return gFocus;
}

void Canvas::paintFrame(QPainter& painter, Scene const& scene, int frame)
{
    painter.fillRect(QRectF(0, 0, scene.width, scene.height), scene.background);
    // Wired by hand in the Node View: the graph says what the frame is.
    if (!scene.graph.isEmpty() && paintGraph(painter, scene, frame))
        return;
    QTransform const base = painter.transform();
    QTransform const camera = scene.cameraTransform(frame);
    QVector<int> const order = scene.drawOrder(frame);
    bool turnedAny = false;
    int drawn = 0;
    for (int l : order) {
        Layer const& layer = scene.layers[l];
        if (!scene.enabled(l) || (layer.kind != Layer::ColourCard && layer.drawingAt(frame) == Layer::kNoDrawing))
            continue;
        ++drawn;
        turnedAny = turnedAny || (layer.kind != Layer::ColourCard && scene.turned(l, frame));
    }
    if (turnedAny && drawn >= 2 && painter.device()) {
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        int const wide = int(device->width() * dpr), high = int(device->height() * dpr);
        QImage built(wide, high, QImage::Format_ARGB32_Premultiplied);
        built.fill(Qt::transparent);
        {
            // The ground of the frame in it too, for a layer with a blend mode to be mixed with.
            built.setDevicePixelRatio(dpr);
            QPainter ground(&built);
            ground.setTransform(base);
            ground.fillRect(QRectF(0, 0, scene.width, scene.height), scene.background);
            built.setDevicePixelRatio(1.0);
        }
        QVector<float> nearest(qsizetype(wide) * high, -std::numeric_limits<float>::infinity());
        for (int l : order) {
            Layer const& layer = scene.layers[l];
            int const index = layer.drawingAt(frame);
            bool const card = layer.kind == Layer::ColourCard;
            if (!scene.enabled(l) || (!card && index == Layer::kNoDrawing))
                continue;
            QImage alone(wide, high, QImage::Format_ARGB32_Premultiplied);
            alone.setDevicePixelRatio(dpr);
            alone.fill(Qt::transparent);
            QTransform const onto = card ? base : scene.worldTransform(l, frame) * camera * base;
            {
                QPainter one(&alone);
                one.setRenderHints(painter.renderHints());
                if (card) {
                    QColor colour = layer.cardColor;
                    colour.setAlphaF(colour.alphaF() * float(layer.opacity));
                    one.setTransform(base);
                    one.fillRect(QRectF(0, 0, scene.width, scene.height), colour);
                } else {
                    one.setTransform(onto);
                    Pose const pose = scene.poseOf(l, frame);
                    paintLayerDrawing(one, scene, l, frame,
                                      Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose, index)),
                                      layer.opacity * pose.opacity, camera * base);
                }
            }
            double a = 0.0, b = 0.0, c = scene.depthOf(l, frame);
            if (!card)
                scene.depthPlane(l, frame, a, b, c);
            QTransform const back = onto.isInvertible() ? onto.inverted() : QTransform();
            int const blend = card ? int(Layer::BlendNormal) : layer.blend;
            for (int y = 0; y < high; ++y) {
                QRgb const* const from = reinterpret_cast<QRgb const*>(alone.constScanLine(y));
                QRgb* const to = reinterpret_cast<QRgb*>(built.scanLine(y));
                float* const depth = nearest.data() + qsizetype(y) * wide;
                for (int x = 0; x < wide; ++x) {
                    QRgb const s = from[x];
                    int const sa = qAlpha(s);
                    if (sa == 0)
                        continue;
                    float z = float(c);
                    if (a != 0.0 || b != 0.0) {
                        QPointF const local = back.map(QPointF((x + 0.5) / dpr, (y + 0.5) / dpr));
                        z = float(a * local.x() + b * local.y() + c);
                    }
                    QRgb const d = to[x];
                    if (z >= depth[x] - 0.5f && blend != Layer::BlendNormal) {
                        // In front of what is there, and mixed with it as the layer's blend mode says.
                        to[x] = blendPixel(s, d, blend);
                        if (sa > 127)
                            depth[x] = z;
                    } else if (z >= depth[x] - 0.5f) {
                        // In front of what is there: over it.
                        int const keep = 255 - sa;
                        to[x] = qRgba(qRed(s) + qRed(d) * keep / 255, qGreen(s) + qGreen(d) * keep / 255, qBlue(s) + qBlue(d) * keep / 255, sa + qAlpha(d) * keep / 255);
                        if (sa > 127)
                            depth[x] = z;
                    } else {
                        // Behind it: under it.
                        int const keep = 255 - qAlpha(d);
                        to[x] = qRgba(qRed(d) + qRed(s) * keep / 255, qGreen(d) + qGreen(s) * keep / 255, qBlue(d) + qBlue(s) * keep / 255, qAlpha(d) + sa * keep / 255);
                    }
                }
            }
        }
        built.setDevicePixelRatio(dpr);
        painter.save();
        painter.resetTransform();
        painter.drawImage(0, 0, built);
        painter.restore();
        painter.setTransform(base);
        return;
    }
    // The furthest first, so that what is nearer the camera covers it.
    for (int l : order) {
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
        Pose const pose = scene.poseOf(l, frame);
        paintLayerDrawing(painter, scene, l, frame,
                          Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose, index)),
                          layer.opacity * pose.opacity, camera * base);
    }
    painter.setTransform(base);
}

Drawing Canvas::previewDrawing() const
{
    Drawing const* const current = currentDrawing();
    Drawing shown = current ? *current : Drawing();
    if (m_drag == DragSegment && current && contourLine(shown)) {
        Stroke& line = *contourLine(shown);
        line = bentSegment(line, m_segIndex, m_segT, m_dragNow - m_dragStart);
        contourEdited(shown);
    }
    if (m_drag == DragCeBox && contourValid()) {
        // The points picked scaled or turned by the box round them; their handles with them.
        QTransform const t = contourBoxTransform();
        QTransform const linear(t.m11(), t.m12(), t.m21(), t.m22(), 0.0, 0.0);
        Stroke& line = *contourLine(shown);
        QVector<int> moved = contourPointsPicked();
        moved.push_back(m_cePoint);
        for (int i : std::as_const(moved)) {
            StrokePoint& point = line.points[i];
            point.pos = t.map(point.pos);
            if (point.handles) {
                point.in = linear.map(point.in);
                point.out = linear.map(point.out);
            }
        }
        contourEdited(shown);
    }
    if (m_drag == DragPoint && contourValid()) {
        QVector<QPair<int, int>> const shared = sharedPoints(shown);
        Stroke& line = *contourLine(shown);
        if (line.curve)
            line.freezeHandles();
        QPointF const by = m_dragNow - line.points[m_cePoint].pos;
        // Points of lines that lie on it are one point with it: they go where it goes.
        for (auto const& [stroke, point] : shared)
            shown.strokes[stroke].points[point].pos += by;
        line.points[m_cePoint].pos = m_dragNow;
        for (int other : contourPointsPicked())     // the others picked go along
            line.points[other].pos += by;
        contourEdited(shown);
    }
    if (m_drag == DragHandle && contourValid() && m_ceHandle == 2 && pulledHandles()) {
        QVector<QPair<int, int>> const stacked = stackedPoints(shown);
        QPointF const reach = m_dragNow - contourLine(shown)->points[m_cePoint].pos;
        double const pull = std::hypot(reach.x(), reach.y());
        bool first = true;
        for (auto const& [line, point] : stacked) {
            Stroke& stroke = line < 0 || m_ceFill >= 0 ? *contourLine(shown) : shown.strokes[line];
            if (first) {
                first = false;
                stroke.setHandles(point, -reach, reach);
                stroke.points[point].corner = false;
                continue;
            }
            int const n = int(stroke.points.size());
            QPointF const at = stroke.points[point].pos;
            auto along = [&](int neighbour) {
                if (neighbour < 0 || neighbour >= n)
                    return QPointF();
                QPointF const way = stroke.points[neighbour].pos - at;
                double const length = std::hypot(way.x(), way.y());
                return length > 1e-9 ? way / length * std::min(pull, length / 2.0) : QPointF();
            };
            int const before = point > 0 ? point - 1 : (stroke.closed ? n - 1 : -1);
            int const after = point + 1 < n ? point + 1 : (stroke.closed ? 0 : -1);
            stroke.setHandles(point, along(before), along(after));
            stroke.points[point].corner = true;
        }
        contourEdited(shown);
    }
    if (m_drag == DragHandle && contourValid() && (m_ceHandle == 0 || m_ceHandle == 1)) {
        int handled = m_cePoint;
        Stroke& stroke = *handleLine(shown, handled);
        QPointF in, out;
        stroke.handlesAt(handled, in, out);
        QPointF const reach = m_dragNow - stroke.points[handled].pos;
        QPointF& mine = m_ceHandle == 0 ? in : out;
        QPointF& other = m_ceHandle == 0 ? out : in;
        mine = reach;
        double const length = std::hypot(reach.x(), reach.y()), otherLength = std::hypot(other.x(), other.y());
        if (!m_ceBreak && length > 1e-9 && otherLength > 1e-9)
            other = -reach / length * otherLength;
        stroke.setHandles(handled, in, out);
        // A point whose handles are not in line is a cusp, and is shown as a corner.
        double const cross = in.x() * out.y() - in.y() * out.x();
        double const lengths = std::hypot(in.x(), in.y()) * std::hypot(out.x(), out.y());
        stroke.points[handled].corner = lengths > 1e-9 && (std::abs(cross) > lengths * 0.02 || in.x() * out.x() + in.y() * out.y() > 0);
        contourEdited(shown);
    }
    if (m_drag == DragMove || m_drag == DragScale || m_drag == DragRotate) {
        // Scaled, a line keeps its thickness - unless Scale Pencil Line Thickness is on: then it is as much thicker or thinner.
        bool const keepWidth = m_drag == DragScale && !m_scaleThickness;
        for (int i : m_selStrokes) {
            float const width = shown.strokes[i].width;
            shown.strokes[i].transform(m_dragTransform);
            if (keepWidth)
                shown.strokes[i].width = width;
        }
        for (int i : m_selFills)
            shown.fills[i].transform(m_dragTransform);
        if (picturesHere())
            for (int i : m_selPictures)
                if (i >= 0 && i < shown.pictures.size())
                    shown.pictures[i].transform(m_dragTransform);
    }
    // Joints: what was moved takes the points joined to it along (a line moved drags the ends of the lines joined to it).
    if (current)
        shown.keepJoints(*current);
    return shown;
}

QTransform Canvas::viewedThrough(Scene const& scene, int camera, int frame)
{
    if (camera < 0 || camera >= scene.layers.size() || scene.layers[camera].kind != Layer::Camera)
        return scene.cameraTransform(frame);
    Scene viewed = scene;
    viewed.layers[camera].cameraAvailable = true;
    viewed.activeCamera = scene.layers[camera].id;
    return viewed.cameraTransform(frame);
}

bool Canvas::paintGraph(QPainter& painter, Scene const& scene, int frame)
{
    NodeGraph const& graph = scene.graph;
    int const display = graph.display();
    QPaintDevice* const device = painter.device();
    if (!display || !device)
        return false;
    double const dpr = device->devicePixelRatioF();
    QSize const size(int(device->width() * dpr), int(device->height() * dpr));
    QTransform const base = painter.transform();
    QTransform const camera = scene.cameraTransform(frame);
    // A blur is so many scene units wide: in the picture that depends on how big the frame is drawn.
    double const pixelsPerUnit = std::sqrt(std::abs((camera * base).determinant())) * dpr;
    auto blank = [&] {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        return image;
    };
    // Each output of each node, worked out once; a ring (which the Node View does not let one make) gives nothing.
    QHash<QPair<int, int>, QImage> made;
    QSet<QPair<int, int>> working;
    std::function<QImage(int, int)> picture = [&](int id, int port) -> QImage {
        QPair<int, int> const key(id, port);
        if (auto const found = made.constFind(key); found != made.cend())
            return *found;
        if (working.contains(key))
            return blank();
        working.insert(key);
        QImage out = blank();
        auto input = [&](int node, int in) {
            GraphCable const* const c = graph.into(node, in);
            return c ? picture(c->from, c->fromPort) : blank();
        };
        GraphNode const* const n = graph.node(id);
        if (n && n->kind == GraphNode::LayerNode) {
            // A layer's picture: where its pegs put it, with its own effects.
            int const l = scene.layerIndex(n->layer);
            if (l >= 0 && scene.enabled(l)) {
                Layer const& layer = scene.layers[l];
                QPainter p(&out);
                p.setRenderHints(painter.renderHints());
                if (layer.kind == Layer::ColourCard) {
                    QColor colour = layer.cardColor;
                    colour.setAlphaF(colour.alphaF() * float(layer.opacity));
                    p.setTransform(base);
                    p.fillRect(QRectF(0, 0, scene.width, scene.height), colour);
                } else if (int const index = layer.drawingAt(frame); index != Layer::kNoDrawing) {
                    p.setTransform(scene.worldTransform(l, frame) * camera * base);
                    Pose const pose = scene.poseOf(l, frame);
                    Drawing shown = Deform::ridden(scene, l, frame, Deform::shown(layer.morphing(frame) ? layer.shownAt(frame) : layer.drawings[index], layer, pose, index));
                    if ((n->sheets & 0xf) != 0xf) {
                        // Only the art layers it gives (Line Art, Colour Art, Underlay, Overlay).
                        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
                            if (!(n->sheets & (1 << sheet))) {
                                shown.fillsOf(sheet).clear();
                                shown.strokesOf(sheet).clear();
                            }
                        shown.pictures.removeIf([&](Picture const& pic) { return !(n->sheets & (1 << (pic.painted ? pic.sheet : int(Drawing::LineSheet)))); });
                    }
                    if (n->symbolLayer && layer.showsSymbol()) {
                        Scene selected = scene;
                        for (Scene::Symbol& symbol : selected.symbols)
                            if (symbol.id == layer.symbolId)
                                for (Layer& output : symbol.layers)
                                    if (output.holdsDrawings() && output.id != n->symbolLayer)
                                        output.visible = false;
                        paintLayerDrawing(p, selected, l, frame, shown, layer.opacity * pose.opacity, camera * base);
                    } else {
                        paintLayerDrawing(p, scene, l, frame, shown, layer.opacity * pose.opacity, camera * base);
                    }
                }
            }
        } else if (n && n->kind == GraphNode::Composite) {
            // Its inputs one over the other, the first in front; a layer that comes straight in is mixed as its blend mode says.
            for (int in = n->inputs() - 1; in >= 0; --in) {
                GraphCable const* const c = graph.into(id, in);
                if (!c)
                    continue;
                QImage const over = picture(c->from, c->fromPort);
                GraphNode const* const from = graph.node(c->from);
                int const l = from && from->kind == GraphNode::LayerNode ? scene.layerIndex(from->layer) : -1;
                int const blend = l >= 0 ? scene.layers[l].blend : int(Layer::BlendNormal);
                if (blend == Layer::BlendNormal) {
                    QPainter p(&out);
                    p.resetTransform();
                    p.drawImage(0, 0, over);
                    continue;
                }
                for (int y = 0; y < out.height(); ++y) {
                    QRgb const* const s = reinterpret_cast<QRgb const*>(over.constScanLine(y));
                    QRgb* const d = reinterpret_cast<QRgb*>(out.scanLine(y));
                    for (int x = 0; x < out.width(); ++x)
                        if (qAlpha(s[x]) > 0)
                            d[x] = blendPixel(s[x], d[x], blend);
                }
            }
        } else if (n && n->kind == GraphNode::EffectNode) {
            out = input(id, 0);
            Effect const& e = n->effect;
            double amount = n->amountAt(frame);
            if (e.truckFactor && Effect::trucks(e.type)) {
                // The layer the picture comes from (along the first inputs): its depth sizes the blur.
                int from = id;
                int fed = -1;
                for (int guard = 0; guard < 256 && fed < 0; ++guard) {
                    GraphCable const* const c = graph.into(from, 0);
                    GraphNode const* const up = c ? graph.node(c->from) : nullptr;
                    if (!up)
                        break;
                    if (up->kind == GraphNode::LayerNode)
                        fed = scene.layerIndex(up->layer);
                    from = c->from;
                }
                if (fed >= 0)
                    amount *= scene.truckFactor(fed, frame);
            }
            if (!e.enabled) {
                // (Turned off: the picture goes through as it is.)
            } else if ((e.type == Effect::Blur && !blursEnabled()) || (e.type == Effect::Focus && !focusEnabled())) {
                // (Switched off in the preferences.)
            } else if (e.type == Effect::Blur || e.type == Effect::Focus) {
                Effects::blur(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Tint) {
                Effects::tint(out, e.color, amount);
            } else if (e.type == Effect::Transparency) {
                Effects::transparency(out, amount);
            } else if (e.type == Effect::DirectionalBlur) {
                Effects::directionalBlur(out, amount * pixelsPerUnit, e.direction);
            } else if (e.type == Effect::HueSaturation) {
                Effects::hueSaturation(out, e.hue, e.saturation, e.lightness, amount);
            } else if (e.type == Effect::BrightnessContrast) {
                Effects::brightnessContrast(out, e.brightness, e.contrast, amount);
            } else if (e.type == Effect::Grain) {
                Effects::grain(out, amount, quint32(e.seed) * 7919u + quint32(frame));
            } else if (e.type == Effect::RadialBlur) {
                Effects::radialBlur(out, std::min(amount, 1.0));
            } else if (e.type == Effect::ColourScale) {
                Effects::colourScale(out, e.color, amount);
            } else if (e.type == Effect::Negate) {
                Effects::negate(out, amount);
            } else if (e.type == Effect::Median) {
                Effects::median(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Ripple) {
                Effects::ripple(out, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
            } else if (e.type == Effect::Glow || e.type == Effect::Shadow) {
                Effects::halo(out, e.color, amount * pixelsPerUnit, e.type == Effect::Shadow ? e.offset * (pixelsPerUnit / dpr) : QPointF());
            } else if (e.type == Effect::MatteGenerator) {
                Effects::matteGenerator(out, e.direction * pixelsPerUnit, amount * pixelsPerUnit, e.color);
            } else if (e.type == Effect::Bloom) {
                Effects::bloom(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Pixelate) {
                Effects::pixelate(out, amount * pixelsPerUnit);
            } else if (e.type == Effect::Turbulence) {
                Effects::turbulence(out, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
            } else if (e.type == Effect::Gradient) {
                Effects::gradient(out, e.color, e.endColor, e.direction, e.invert, amount);
            } else if (e.type == Effect::BlackWhite) {
                Effects::blackWhite(out, amount);
            } else if (e.type == Effect::MatteResize) {
                Effects::matteResize(out, amount * pixelsPerUnit, e.invert);
            } else if (e.type == Effect::FieldChart) {
                Effects::fieldChart(out, QTransform::fromScale(dpr, dpr).mapRect((camera * base).mapRect(QRectF(0, 0, scene.width, scene.height))), e.color,
                                    amount * dpr);
            } else if (e.type == Effect::ColourCurves) {
                Effects::colourCurves(out, e.curves, amount);
            } else if (e.type == Effect::ApplyPeg) {
                if (int const peg = scene.layerIndex(e.matte); peg >= 0) {
                    QTransform const toDevice = camera * base * QTransform::fromScale(dpr, dpr);
                    Effects::transformImage(out, toDevice.inverted() * scene.worldTransform(peg, frame) * toDevice);
                }
            } else if (e.type == Effect::ApplyCamera) {
                int const other = scene.layerIndex(e.matte);
                if (other >= 0 && scene.layers[other].kind == Layer::Camera && e.cameraViewpoint && !e.flattenDepth) {
                    // Each layer of what comes in, seen in its own depth from the other camera: that part of the graph drawn again through it.
                    if (GraphCable const* const c = graph.into(id, 0)) {
                        Scene viewed = scene;
                        viewed.layers[other].cameraAvailable = true;
                        viewed.activeCamera = scene.layers[other].id;
                        viewed.graph.cables.removeIf([display](GraphCable const& cable) { return cable.to == display && cable.toPort == 0; });
                        viewed.graph.cables.push_back(GraphCable{c->from, c->fromPort, display, 0});
                        QImage again(size, QImage::Format_ARGB32_Premultiplied);
                        again.fill(Qt::transparent);
                        {
                            QPainter ap(&again);
                            ap.setRenderHints(painter.renderHints());
                            ap.setTransform(base * QTransform::fromScale(dpr, dpr));
                            paintGraph(ap, viewed, frame);
                        }
                        again.setDevicePixelRatio(dpr);
                        out = again;
                    }
                } else if (other >= 0 && scene.layers[other].kind == Layer::Camera) {
                    // Flat: the picture as it is, seen through the other camera (or moved with it, as with a peg).
                    QTransform const toDevice = camera * base * QTransform::fromScale(dpr, dpr);
                    QTransform const through = e.cameraViewpoint ? viewedThrough(scene, other, frame) * camera.inverted() : scene.worldTransform(other, frame);
                    Effects::transformImage(out, toDevice.inverted() * through * toDevice);
                }
            } else if (e.type == Effect::ScaleOutput) {
                Effects::scaleOutput(out, amount);
            } else if (e.type == Effect::Script) {
                Effects::runScript(out, e.text, frame, amount, pixelsPerUnit);
            } else if (e.type == Effect::External) {
                Effects::runExternal(out, e.text, frame);
            } else if (e.type == Effect::ChannelSelector) {
                Effects::channelSelect(out, e.channel);
            } else if (e.type == Effect::Crop) {
                if (!e.area.isEmpty())
                    Effects::crop(out, QTransform::fromScale(dpr, dpr).mapRect((camera * base).mapRect(e.area)));
            } else if (e.type == Effect::RemoveTransparency) {
                Effects::removeTransparency(out, amount);
            } else if (e.type == Effect::MatteComposite) {
                Effects::matteComposite(out);
            } else if (e.type == Effect::Blending) {
                // Over what comes in at the second input, as the blend mode says.
                QImage under = input(id, 1);
                Effects::blendOnto(under, out, e.blendMode);
                out = under;
            } else if (e.type == Effect::VariableBlur) {
                Effects::variableBlur(out, input(id, 1), amount * pixelsPerUnit);
            } else if (e.type == Effect::Glue) {
                // Joined to what comes in at the second input.
                QImage under = input(id, 1);
                Effects::glue(under, out, amount * pixelsPerUnit);
                out = under;
            } else if (GraphNode::takesMatte(e.type) && graph.into(id, 1)) {
                // The matte comes in at the second input: what is painted there (or, inverted, what is not).
                QImage where = input(id, 1);
                if (e.invert) {
                    QImage outside = blank();
                    outside.fill(Qt::black);
                    QPainter op(&outside);
                    op.resetTransform();
                    op.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                    op.drawImage(0, 0, where);
                    op.end();
                    where = outside;
                }
                if (e.type == Effect::Cutter) {
                    QPainter cut(&out);
                    cut.resetTransform();
                    cut.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    cut.drawImage(0, 0, where);
                } else if (e.type == Effect::MatteBlur) {
                    Effects::blur(where, std::max(1.0, amount * pixelsPerUnit / 3.0));
                    QImage blurred = out;
                    Effects::blur(blurred, amount * pixelsPerUnit);
                    {
                        QPainter bp(&blurred);
                        bp.resetTransform();
                        bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        bp.drawImage(0, 0, where);
                    }
                    QPainter ip(&out);
                    ip.resetTransform();
                    ip.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                    ip.drawImage(0, 0, where);
                    ip.setCompositionMode(QPainter::CompositionMode_Plus);
                    ip.drawImage(0, 0, blurred);
                } else {
                    // Tone or Highlight: shaded (or lit) where the matte is, the edge softened.
                    Effects::blur(where, amount * pixelsPerUnit);
                    QImage shaded = out;
                    {
                        QPainter sp(&shaded);
                        sp.resetTransform();
                        sp.setCompositionMode(e.type == Effect::Tone ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_Screen);
                        sp.fillRect(shaded.rect(), QColor(e.color.red(), e.color.green(), e.color.blue()));
                        sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                        sp.drawImage(0, 0, out);
                        sp.drawImage(0, 0, where);
                    }
                    QPainter onto(&out);
                    onto.resetTransform();
                    onto.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    onto.drawImage(0, 0, shaded);
                }
            }
        } else if (n && n->kind == GraphNode::Group) {
            // What leaves a group's output: what comes into that input of its output node, inside.
            if (int const inside = graph.groupOut(id))
                out = input(inside, port);
        } else if (n && n->kind == GraphNode::GroupIn) {
            // What an output of a group's input node gives: what comes into that input of the group, outside.
            out = input(n->group, port);
        }
        working.remove(key);
        made.insert(key, out);
        return out;
    };
    GraphCable const* const shown = graph.into(display, 0);
    QImage const frameImage = shown ? picture(shown->from, shown->fromPort) : blank();
    painter.save();
    painter.resetTransform();
    painter.drawImage(0, 0, frameImage);
    painter.restore();
    return true;
}
