// Eclipse - the Camera view: painting a drawing, a symbol, a layer with its effects.
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

static void paintDrawingOpaque(QPainter& painter, Scene const& scene, Drawing const& drawing, QColor const* tint)
{
    auto paintPicture = [&painter](Picture const& picture) {
        painter.save();
        painter.setTransform(picture.placement, true);
        if (!picture.clip.isEmpty()) {
            // Only the part inside its outlines shows.
            QPainterPath inside;
            for (QPolygonF const& ring : picture.clip)
                inside.addPolygon(ring);
            painter.setClipPath(inside, Qt::IntersectClip);
        }
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
    };
    bool placed = false;    // pictures placed among the lines and areas of the line art
    for (Picture const& picture : drawing.pictures) {
        if (!picture.painted && picture.order < 0)
            paintPicture(picture);
        placed = placed || (!picture.painted && picture.order >= 0);
    }
    // From the sheet underneath to the one on top: underlay, colour art, lines, overlay.
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Picture const& picture : drawing.pictures)
            if (picture.painted && picture.sheet == sheet)
                paintPicture(picture);
        QVector<Fill> const& fills = drawing.fillsOf(sheet);
        QVector<Stroke> const& strokes = drawing.strokesOf(sheet);
        bool const withPictures = placed && sheet == Drawing::LineSheet;
        // Paint that follows its lines: with its fine edge, on the lines themselves (unless things are moving).
        bool fine = false;
        if (Canvas::s_fineFills)
            for (Fill const& fill : fills)
                fine = fine || (fill.follows && fill.contours.isEmpty());
        QVector<Stroke> const walls = fine ? Canvas::wallsOf(drawing, sheet) : QVector<Stroke>();
        quint64 const key = fine ? Canvas::wallsKey(walls) : 0;
        auto drawFill = [&](Fill const& fill) {
            QBrush const brush = tint ? QBrush(*tint) : scene.brushFor(fill);
            if (!fine || !fill.follows || !fill.contours.isEmpty()) {
                Canvas::paintFill(painter, fill, brush);
                return;
            }
            Fill shown = fill;
            shown.outlines = Canvas::fineOutlinesOf(fill, walls, key);
            Canvas::paintFill(painter, shown, brush);
        };
        if (!withPictures && Drawing::topOrder(fills, strokes) == Drawing::bottomOrder(fills, strokes)) {
            // All in one place in the order (as nearly always): the areas, then the lines over them.
            for (Fill const& fill : fills)
                drawFill(fill);
            for (Stroke const& stroke : strokes)
                Canvas::paintStroke(painter, stroke, tint ? QBrush(*tint) : scene.brushFor(stroke));
            continue;
        }
        // From the bottom of the order up: areas under lines of the same place, each list in its own order.
        struct Item {
            int order, kind, index;
        };
        QVector<Item> items;
        for (int i = 0; i < fills.size(); ++i)
            items.push_back({fills[i].order, 0, i});
        for (int i = 0; i < strokes.size(); ++i)
            items.push_back({strokes[i].order, 1, i});
        if (withPictures)
            for (int i = 0; i < drawing.pictures.size(); ++i)
                if (!drawing.pictures[i].painted && drawing.pictures[i].order >= 0)
                    items.push_back({drawing.pictures[i].order, -1, i});
        std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) {
            return a.order != b.order ? a.order < b.order : a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
        });
        for (Item const& item : std::as_const(items)) {
            if (item.kind < 0)
                paintPicture(drawing.pictures[item.index]);
            else if (item.kind == 0)
                drawFill(fills[item.index]);
            else
                Canvas::paintStroke(painter, strokes[item.index], tint ? QBrush(*tint) : scene.brushFor(strokes[item.index]));
        }
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
        Pose const pose = inner.poseOf(l, f);
        paintLayerDrawing(painter, inner, l, f, Deform::ridden(inner, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose)),
                          layer.opacity * pose.opacity, base);
    }
    painter.setTransform(base);
    --depth;
}

// While a layer with a blend mode is painted alone, to be mixed with what is under it as a whole.
static thread_local bool tPaintingToBlend = false;

// What painting a layer has to hand when it does its effects (see applyLayerEffect).
struct LayerEffectSpace {
    QPainter& painter;
    Scene const& scene;
    int l;
    int frame;
    QTransform const& sceneToDevice;
    Layer const& layer;
    QSize size;
    double dpr;
    double pixelsPerUnit;
    Pose pose;
    std::function<void(QPainter&, int, int)> paintMatte;
    bool symbol;
};

void Canvas::paintLayerDrawing(QPainter& painter, Scene const& scene, int l, int frame, Drawing const& drawing, double opacity,
                               QTransform const& sceneToDevice, bool everySheet)
{
    Layer const& layer = scene.layers[l];
    if (layer.blend != Layer::BlendNormal && !tPaintingToBlend && opacity > 0.0) {
        // The layer alone in a picture of its own (its lines over its paint as usual), mixed with what is under it once.
        static QPainter::CompositionMode const kModes[Layer::BlendCount] = {
            QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Multiply, QPainter::CompositionMode_Screen,
            QPainter::CompositionMode_Overlay, QPainter::CompositionMode_Darken, QPainter::CompositionMode_Lighten,
            QPainter::CompositionMode_ColorDodge, QPainter::CompositionMode_ColorBurn, QPainter::CompositionMode_HardLight,
            QPainter::CompositionMode_SoftLight, QPainter::CompositionMode_Difference, QPainter::CompositionMode_Exclusion,
            QPainter::CompositionMode_Plus};
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        QImage alone(QSize(int(device->width() * dpr), int(device->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
        alone.setDevicePixelRatio(dpr);
        alone.fill(Qt::transparent);
        {
            QPainter off(&alone);
            off.setRenderHints(painter.renderHints());
            off.setTransform(painter.transform());
            off.setClipRegion(painter.clipRegion(), painter.hasClipping() ? Qt::ReplaceClip : Qt::NoClip);
            tPaintingToBlend = true;
            paintLayerDrawing(off, scene, l, frame, drawing, opacity, sceneToDevice, everySheet);
            tPaintingToBlend = false;
        }
        painter.save();
        painter.resetTransform();
        painter.setCompositionMode(kModes[std::clamp(layer.blend, 0, int(Layer::BlendCount) - 1)]);
        painter.drawImage(0, 0, alone);
        painter.restore();
        return;
    }
    if (!everySheet && layer.hiddenSheets != 0 && !layer.showsSymbol()) {
        // The art layers left out of the picture are not in it.
        Drawing kept = drawing;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            if (!layer.sheetShown(sheet)) {
                kept.fillsOf(sheet).clear();
                kept.strokesOf(sheet).clear();
                kept.pictures.removeIf([sheet](Picture const& p) { return p.painted && p.sheet == sheet; });
            }
        }
        paintLayerDrawing(painter, scene, l, frame, kept, opacity, sceneToDevice, true);
        return;
    }
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
        // Particles: the picture is thrown out again and again, each copy where it has got to and as faint as it has grown.
        int thrown = -1;
        for (int i = 0; i < layer.effects.size() && thrown < 0; ++i)
            if (layer.effects[i].enabled && layer.effects[i].type == Effect::Particles)
                thrown = i;
        auto picture = [&] {
            if (symbol)
                paintSymbol(off, scene, layer.symbolId, symbolFrame);
            else
                paintDrawingOpaque(off, scene, drawing, nullptr);
        };
        if (thrown < 0) {
            picture();
        } else {
            Effect const& emitter = layer.effects[thrown];
            double const rate = layer.effectAmount(thrown, layer.poseAt(frame));
            QTransform const now = scene.worldTransform(l, frame);
            QTransform const base = off.transform();
            QImage plain;
            QRectF area;
            QVector<QImage> mixed;
            if (emitter.endColor.isValid()) {
                QRectF const whole(QPointF(0, 0), QSizeF(size) / dpr);
                area = symbol ? whole : base.mapRect(drawing.bounds()).adjusted(-4, -4, 4, 4) & whole;
                if (!area.isEmpty()) {
                    plain = QImage((area.size() * dpr).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
                    plain.setDevicePixelRatio(dpr);
                    plain.fill(Qt::transparent);
                    QPainter once(&plain);
                    once.setRenderHints(painter.renderHints());
                    once.setTransform(base * QTransform::fromTranslate(-area.left(), -area.top()));
                    if (symbol)
                        paintSymbol(once, scene, layer.symbolId, symbolFrame);
                    else
                        paintDrawingOpaque(once, scene, drawing, nullptr);
                    mixed.resize(9);
                }
            }
            auto mixedImage = [&](int step) -> QImage const& {
                if (mixed[step].isNull()) {
                    QImage made = plain.copy();
                    made.setDevicePixelRatio(dpr);
                    QPainter tint(&made);
                    tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
                    QColor colour = emitter.endColor;
                    colour.setAlphaF(float(step / 8.0));
                    tint.fillRect(QRectF(QPointF(0, 0), area.size()), colour);
                    tint.end();
                    mixed[step] = made;
                }
                return mixed[step];
            };
            QTransform const backToScene = base.isInvertible() ? base.inverted() : QTransform();
            // Let go from the drawing's own shape: places on its artwork, found once (the same every time it is painted).
            QVector<QPointF> places;
            if (emitter.regionShape == 3 && !symbol) {
                QPainterPath art;
                art.setFillRule(Qt::WindingFill);
                for (Fill const& f : drawing.fills)
                    for (QPolygonF const& outline : f.outlines)
                        art.addPolygon(outline);
                for (Stroke const& s : drawing.strokes)
                    for (QPolygonF const& outline : s.outlinePolygons())
                        art.addPolygon(outline);
                QRectF const box = art.boundingRect();
                QPointF const middle = drawing.bounds().center();
                quint32 seed = quint32(emitter.seed) * 2654435761u + 12345u;
                auto next = [&seed] {
                    seed = seed * 1664525u + 1013904223u;
                    return double(seed >> 8) / double(1u << 24);
                };
                for (int tries = 0; tries < 4000 && places.size() < 96 && !box.isEmpty(); ++tries) {
                    QPointF const p(box.left() + next() * box.width(), box.top() + next() * box.height());
                    if (art.contains(p))
                        places.push_back(p - middle);
                }
            }
            std::function<QPainterPath(int)> shapeFor;
            int const wall = emitter.matte != 0 ? scene.layerIndex(emitter.matte) : -1;
            QPainterPath obstacle;
            if (wall >= 0 && wall != l) {
                int const index = scene.layers[wall].drawingAt(frame);
                if (index != Layer::kNoDrawing) {
                    Drawing const shown = Deform::shown(scene.layers[wall].drawings[index], scene.layers[wall], scene.layers[wall].poseAt(frame));
                    obstacle.setFillRule(Qt::WindingFill);
                    for (Fill const& f : shown.fills)
                        for (QPolygonF const& outline : f.outlines)
                            obstacle.addPolygon(outline);
                    for (Stroke const& s : shown.strokes)
                        for (QPolygonF const& outline : s.outlinePolygons())
                            obstacle.addPolygon(outline);
                    obstacle = scene.worldTransform(wall, frame).map(obstacle);
                }
            }
            if (!obstacle.isEmpty()) {
                QPointF const middle = drawing.bounds().center();
                shapeFor = [&, middle](int born) {
                    QTransform const at = scene.worldTransform(l, emitter.followLayer ? frame : born);
                    if (!at.isInvertible())
                        return QPainterPath();
                    return at.inverted().map(obstacle).translated(-middle);
                };
            }
            for (Effect::Particle const& particle : emitter.particles(rate, frame, places, shapeFor)) {
                off.save();
                // Each stays where the layer was when it was let go (unless it is to move with the layer).
                if (!emitter.followLayer && particle.born != frame && now.isInvertible())
                    off.setTransform(scene.worldTransform(l, particle.born) * now.inverted() * base);
                off.translate(particle.offset);
                if (particle.scale != 1.0 || particle.angle != 0.0) {
                    // Grown or shrunk, and turned, about the middle of the drawing.
                    QPointF const middle = drawing.bounds().center();
                    off.translate(middle);
                    off.rotate(particle.angle);
                    off.scale(particle.scale, particle.scale);
                    off.translate(-middle);
                }
                off.setOpacity(particle.opacity);
                int const step = int(std::lround(std::clamp(particle.tint, 0.0, 1.0) * 8.0));
                if (step > 0 && !plain.isNull()) {
                    off.setTransform(QTransform::fromTranslate(area.left(), area.top()) * backToScene * off.transform());
                    off.drawImage(QPointF(0, 0), mixedImage(step));
                } else {
                    picture();
                }
                off.restore();
            }
        }
    }
    // A blur is so many scene units wide: on screen that depends on the zoom.
    double const pixelsPerUnit = std::sqrt(std::abs(sceneToDevice.determinant())) * dpr;
    static thread_local int tMatteDepth = 0;
    auto paintMatte = [&](QPainter& mp, int m, int index) {
        Layer const& matteLayer = scene.layers[m];
        bool generated = false;
        for (Effect const& me : matteLayer.effects)
            generated = generated || (me.enabled && me.type == Effect::MatteGenerator);
        Drawing const shownMatte = Deform::shown(matteLayer.drawings[index], matteLayer, matteLayer.poseAt(frame));
        if (!generated || tMatteDepth >= 2) {
            paintDrawingOpaque(mp, scene, shownMatte, nullptr);
            return;
        }
        ++tMatteDepth;
        paintLayerDrawing(mp, scene, m, frame, shownMatte, 1.0, sceneToDevice);
        --tMatteDepth;
    };
    Pose const pose = layer.poseAt(frame);
    LayerEffectSpace const space{painter, scene, l, frame, sceneToDevice, layer, size, dpr, pixelsPerUnit, pose, paintMatte, symbol};
    for (int i = 0; i < layer.effects.size(); ++i)
        applyLayerEffect(image, i, space);
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(0, 0, image);
    painter.restore();
}

// One effect of a layer done to the picture of the layer alone (`image`): effect `i`, with what painting the layer had to hand.
void Canvas::applyLayerEffect(QImage& image, int i, LayerEffectSpace const& space)
{
    [[maybe_unused]] auto const& [painter, scene, l, frame, sceneToDevice, layer, size, dpr, pixelsPerUnit, pose, paintMatte, symbol] = space;
    Effect const& e = layer.effects[i];
    if (!e.enabled)
        return;
    double const amount = layer.effectAmount(i, pose);
    if ((e.type == Effect::Blur && !blursEnabled()) || (e.type == Effect::Focus && !focusEnabled())) {
        // (Switched off in the preferences: the picture goes through as it is.)
    } else if (e.type == Effect::Blur) {
        Effects::blur(image, amount * pixelsPerUnit);
    } else if (e.type == Effect::Focus) {
        // Out of focus as far as the layer stands from the depth that is sharp.
        Effects::blur(image, amount * std::abs(scene.depthOf(l, frame) - e.direction) / 100.0 * pixelsPerUnit);
    } else if (e.type == Effect::Tint) {
        Effects::tint(image, e.color, amount);
    } else if (e.type == Effect::Transparency) {
        Effects::transparency(image, amount);
    } else if (e.type == Effect::DirectionalBlur) {
        // (A direction on the picture, as a shadow's: it does not turn with the layer.)
        Effects::directionalBlur(image, amount * pixelsPerUnit, e.direction);
    } else if (e.type == Effect::HueSaturation) {
        Effects::hueSaturation(image, e.hue, e.saturation, e.lightness, amount);
    } else if (e.type == Effect::BrightnessContrast) {
        Effects::brightnessContrast(image, e.brightness, e.contrast, amount);
    } else if (e.type == Effect::Grain) {
        Effects::grain(image, amount, quint32(e.seed) * 7919u + quint32(frame));     // a new grain each frame
    } else if (e.type == Effect::RadialBlur) {
        Effects::radialBlur(image, amount);
    } else if (e.type == Effect::ColourScale) {
        Effects::colourScale(image, e.color, amount);
    } else if (e.type == Effect::Negate) {
        Effects::negate(image, amount);
    } else if (e.type == Effect::Median) {
        Effects::median(image, amount * pixelsPerUnit);
    } else if (e.type == Effect::Ripple) {
        // (Waves across the picture, as a shadow's offset: they do not turn with the layer.)
        Effects::ripple(image, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
    } else if (e.type == Effect::Glow || e.type == Effect::Shadow) {
        // The offset is a direction on the picture: it does not turn with the layer.
        Effects::halo(image, e.color, amount * pixelsPerUnit, e.type == Effect::Shadow ? e.offset * (pixelsPerUnit / dpr) : QPointF());
    } else if (e.type == Effect::MatteGenerator) {
        Effects::matteGenerator(image, e.direction * pixelsPerUnit, amount * pixelsPerUnit, e.color);
    } else if (e.type == Effect::Bloom) {
        Effects::bloom(image, amount * pixelsPerUnit);
    } else if (e.type == Effect::Pixelate) {
        Effects::pixelate(image, amount * pixelsPerUnit);
    } else if (e.type == Effect::Turbulence) {
        Effects::turbulence(image, amount * pixelsPerUnit, e.direction * pixelsPerUnit, frame);
    } else if (e.type == Effect::Gradient) {
        Effects::gradient(image, e.color, e.endColor, e.direction, e.invert, amount);
    } else if (e.type == Effect::BlackWhite) {
        Effects::blackWhite(image, amount);
    } else if (e.type == Effect::MatteResize) {
        Effects::matteResize(image, amount * pixelsPerUnit, e.invert);
    } else if (e.type == Effect::FieldChart) {
        Effects::fieldChart(image, QTransform::fromScale(dpr, dpr).mapRect(sceneToDevice.mapRect(QRectF(0, 0, scene.width, scene.height))), e.color,
                            amount * dpr);
    } else if (e.type == Effect::MotionBlur && !symbol) {
        int const back = std::clamp(int(std::lround(amount)), 0, 60);
        QImage trail(size, QImage::Format_ARGB32_Premultiplied);
        trail.setDevicePixelRatio(dpr);
        trail.fill(Qt::transparent);
        {
            QPainter tp(&trail);
            tp.setRenderHints(painter.renderHints());
            for (int k = back; k >= 1; --k) {
                int const f = frame - k;
                int const index = f >= 0 ? layer.drawingAt(f) : Layer::kNoDrawing;
                if (index == Layer::kNoDrawing)
                    continue;
                tp.setOpacity(0.6 * (1.0 - double(k) / (back + 1)));
                tp.setTransform(scene.worldTransform(l, f) * sceneToDevice);
                Pose const then = scene.poseOf(l, f);
                paintDrawingOpaque(tp, scene, Deform::ridden(scene, l, f, Deform::shown(layer.drawings[index], layer, then)), nullptr);
            }
            tp.setOpacity(1.0);
            tp.resetTransform();
            tp.drawImage(0, 0, image);
        }
        image = trail;
    } else if (e.type == Effect::ColourCurves) {
        Effects::colourCurves(image, e.curves, amount);
    } else if (e.type == Effect::ApplyPeg) {
        // The peg's motion, in the picture's pixels: back to the scene, moved as the peg is, onto the picture again.
        if (int const peg = scene.layerIndex(e.matte); peg >= 0 && peg != l) {
            QTransform const toDevice = sceneToDevice * QTransform::fromScale(dpr, dpr);
            Effects::transformImage(image, toDevice.inverted() * scene.worldTransform(peg, frame) * toDevice);
        }
    } else if (e.type == Effect::ScaleOutput) {
        Effects::scaleOutput(image, amount);
    } else if (e.type == Effect::Script) {
        Effects::runScript(image, e.text, frame, amount, pixelsPerUnit);
    } else if (e.type == Effect::External) {
        Effects::runExternal(image, e.text, frame);
    } else if (e.type == Effect::ChannelSelector) {
        Effects::channelSelect(image, e.channel);
    } else if (e.type == Effect::Crop) {
        // (The area is in scene units: where it is in the picture.)
        if (!e.area.isEmpty())
            Effects::crop(image, QTransform::fromScale(dpr, dpr).mapRect(sceneToDevice.mapRect(e.area)));
    } else if (e.type == Effect::RemoveTransparency) {
        Effects::removeTransparency(image, amount);
    } else if (e.type == Effect::MatteComposite) {
        Effects::matteComposite(image);
    } else if (e.type == Effect::VariableBlur) {
        int const m = scene.layerIndex(e.matte);
        if (m < 0)
            return;  // no matte: nothing blurred
        QImage where(size, QImage::Format_ARGB32_Premultiplied);
        where.setDevicePixelRatio(dpr);
        where.fill(Qt::transparent);
        if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
            QPainter mp(&where);
            mp.setRenderHints(painter.renderHints());
            mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
            paintMatte(mp, m, index);
        }
        Effects::variableBlur(image, where, amount * pixelsPerUnit);
    } else if (e.type == Effect::MatteBlur) {
        int const m = scene.layerIndex(e.matte);
        if (m < 0)
            return;  // no matte: nowhere to blur
        QImage where(size, QImage::Format_ARGB32_Premultiplied);
        where.setDevicePixelRatio(dpr);
        where.fill(e.invert ? Qt::black : Qt::transparent);
        if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
            QPainter mp(&where);
            mp.setRenderHints(painter.renderHints());
            if (e.invert)
                mp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
            paintMatte(mp, m, index);
        }
        // The change from sharp to blurred soft-edged as well.
        Effects::blur(where, std::max(1.0, amount * pixelsPerUnit / 3.0));
        QImage blurred = image;
        Effects::blur(blurred, amount * pixelsPerUnit);
        {
            QPainter bp(&blurred);
            bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            bp.drawImage(0, 0, where);
        }
        {
            QPainter ip(&image);
            ip.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            ip.drawImage(0, 0, where);
            ip.setCompositionMode(QPainter::CompositionMode_Plus);
            ip.drawImage(0, 0, blurred);
        }
    } else if (e.type == Effect::Tone || e.type == Effect::Highlight) {
        int const m = scene.layerIndex(e.matte);
        if (m < 0)
            return;  // no matte: nowhere to shade
        QImage where(size, QImage::Format_ARGB32_Premultiplied);
        where.setDevicePixelRatio(dpr);
        where.fill(Qt::transparent);
        if (int const index = scene.layers[m].drawingAt(frame); index != Layer::kNoDrawing) {
            QPainter mp(&where);
            mp.setRenderHints(painter.renderHints());
            mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
            paintMatte(mp, m, index);
        }
        if (e.invert) {
            // Outside the matte: the rest of the picture.
            QImage outside(size, QImage::Format_ARGB32_Premultiplied);
            outside.setDevicePixelRatio(dpr);
            outside.fill(Qt::black);
            QPainter op(&outside);
            op.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            op.drawImage(0, 0, where);
            op.end();
            where = outside;
        }
        Effects::blur(where, amount * pixelsPerUnit);
        // The layer in the shade (or the light), then only where the matte says, over the layer as it is.
        QImage shaded = image;
        {
            QPainter sp(&shaded);
            sp.setCompositionMode(e.type == Effect::Tone ? QPainter::CompositionMode_Multiply : QPainter::CompositionMode_Screen);
            sp.fillRect(shaded.rect(), QColor(e.color.red(), e.color.green(), e.color.blue()));
            sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            sp.drawImage(0, 0, image);
            sp.drawImage(0, 0, where);
        }
        QPainter onto(&image);
        onto.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        onto.drawImage(0, 0, shaded);
    } else if (e.type == Effect::Cutter) {
        int const m = scene.layerIndex(e.matte);
        if (m < 0)
            return;  // no matte: nothing is cut
        QImage matte(size, QImage::Format_ARGB32_Premultiplied);
        matte.setDevicePixelRatio(dpr);
        matte.fill(Qt::transparent);
        int const index = scene.layers[m].drawingAt(frame);
        if (index != Layer::kNoDrawing) {
            QPainter mp(&matte);
            mp.setRenderHints(painter.renderHints());
            mp.setTransform(scene.worldTransform(m, frame) * sceneToDevice);
            paintMatte(mp, m, index);
        }
        QPainter cut(&image);
        cut.setCompositionMode(e.invert ? QPainter::CompositionMode_DestinationOut : QPainter::CompositionMode_DestinationIn);
        cut.drawImage(0, 0, matte);
    }
}

