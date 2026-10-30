// Eclipse - the Camera view: painting a drawing, a symbol, a layer with its effects.
#include "canvas/Canvas.h"
#include "canvas/CanvasPaintPrivate.h"
#include "model/scene/StrokePrivate.h"

#include <array>
#include <atomic>
#include <QPainter>
#include <QPainterPath>
#include <QScopeGuard>

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
#include "colour/images/ColourTransform.h"
#include "colour/images/DeepPixels.h"

using namespace canvas_paint;

using namespace canvas_paint_shared;

namespace {

// Preferences > Render: a gradient or textured colour shown in the Camera view as its colour alone, to be quicker.
QBrush flatInView(Scene const& scene, QBrush const& brush, int swatch)
{
    if (brush.style() == Qt::SolidPattern || !leftOut(Canvas::ViewTextures))
        return brush;
    Swatch const* const s = scene.findSwatch(swatch);
    return s ? QBrush(s->color) : brush;
}

// The conversions of the colour management in use while a drawing is painted.
struct Managed {
    Cms::Transform colours, textures, pictures;
    bool any() const { return !colours.isIdentity() || !textures.isIdentity() || !pictures.isIdentity(); }
};

// A palette colour, gradient or texture put in the working space.
QBrush managedBrush(QBrush brush, Managed const& m)
{
    switch (brush.style()) {
    case Qt::NoBrush:
        return brush;
    case Qt::LinearGradientPattern:
    case Qt::RadialGradientPattern:
    case Qt::ConicalGradientPattern: {
        if (m.colours.isIdentity() || !brush.gradient())
            return brush;
        QGradient g = *brush.gradient();
        QGradientStops stops = g.stops();
        for (QGradientStop& stop : stops)
            stop.second = m.colours.apply(stop.second);
        g.setStops(stops);
        QBrush out(g);
        out.setTransform(brush.transform());
        return out;
    }
    case Qt::TexturePattern: {
        if (m.textures.isIdentity())
            return brush;
        QBrush out(convertedPicture(brush.textureImage(), m.textures));
        out.setTransform(brush.transform());
        return out;
    }
    default:
        if (!m.colours.isIdentity())
            brush.setColor(m.colours.apply(brush.color()));
        return brush;
    }
}

}  // namespace

void canvas_paint::paintDrawingOpaque(QPainter& painter, Scene const& scene, Drawing const& drawing, QColor const* tint)
{
    // Colour management: palette colours, textures and pictures put in the working space.
    Managed managed;
    if (scene.colourManaged && !tint) {
        managed.colours = scene.swatchTransform();
        managed.textures = scene.textureTransform();
        if (gPictureLayer)
            managed.pictures = scene.pictureTransform(*gPictureLayer, false);
    }
    bool const managing = managed.any();
    auto brushOf = [&](QBrush const& brush) { return managing ? managedBrush(brush, managed) : brush; };
    auto paintPicture = [&painter, &managed, managing, &scene, tint](Picture const& picture) {
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
            painter.setRenderHint(QPainter::SmoothPixmapTransform, !gNearestPictures && !leftOut(Canvas::ViewSmoothPictures));
            PictureWash const& wash = pictureWashState();
            // (A picture with a colour space of its own - a linked one, say - is converted from that.)
            bool const ownSpace = !picture.painted && !picture.colourSpace.isEmpty() && scene.colourManaged && gPictureLayer && !tint;
            Cms::Transform const own = ownSpace ? scene.pictureTransform(*gPictureLayer, picture) : Cms::Transform();
            Cms::Transform const& t = picture.painted ? managed.colours : ownSpace ? own : managed.pictures;
            QImage const shown = (managing || ownSpace) && !t.isIdentity() ? convertedPicture(picture.image, t, Deep::isDeep(painter.device())) : picture.image;
            painter.drawImage(box, !picture.painted && wash.percent > 0 ? washedPicture(shown, wash.percent, wash.colour) : shown);
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
        if (gFineFills)
            for (Fill const& fill : fills)
                fine = fine || (fill.follows && fill.contours.isEmpty());
        QVector<Stroke> const walls = fine ? Canvas::wallsOf(drawing, sheet) : QVector<Stroke>();
        quint64 const key = fine ? Canvas::wallsKey(walls) : 0;
        auto drawFill = [&](Fill const& fill) {
            QBrush const brush = tint ? QBrush(*tint) : brushOf(flatInView(scene, scene.brushFor(fill), fill.swatch));
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
                Canvas::paintStroke(painter, stroke, tint ? QBrush(*tint) : brushOf(flatInView(scene, scene.brushFor(stroke), stroke.swatch)));
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
                Canvas::paintStroke(painter, strokes[item.index], tint ? QBrush(*tint) : brushOf(flatInView(scene, scene.brushFor(strokes[item.index]), strokes[item.index].swatch)));
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
    QImage image(QSize(int(device->width() * dpr), int(device->height() * dpr)), Deep::formatFor(device));
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
    static thread_local int depth = 0;       // symbols inside symbols: so deep and no deeper (and never round in a ring)
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
        paintLayerDrawing(painter, inner, l, f, Deform::ridden(inner, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose, index)),
                          layer.opacity * pose.opacity, base);
    }
    painter.setTransform(base);
    --depth;
}

// While a layer with a blend mode is painted alone, to be mixed with what is under it as a whole.
static thread_local bool tPaintingToBlend = false;
static thread_local bool tOverridden = false;
static thread_local bool tSmoothed = false;

namespace {

// The soft edge of what is painted made harder (above 1) or softer (below 1): the part-covered pixels' coverage raised to it.
void antialiasExponent(QImage& image, double exponent)
{
    Deep::prepare(image);
    for (int y = 0; y < image.height(); ++y) {
        Deep::Line const row(image, y);
        for (int x = 0; x < image.width(); ++x) {
            Deep::Pixel p = row[x];
            if (p.a <= 0.0 || p.a >= 1.0)
                continue;
            double const to = std::pow(p.a, exponent), k = to / p.a;
            p = {p.r * k, p.g * k, p.b * k, to};
            row.set(x, p);
        }
    }
}

}  // namespace

void Canvas::paintLayerDrawing(QPainter& painter, Scene const& scene, int l, int frame, Drawing const& drawing, double opacity,
                               QTransform const& sceneToDevice, bool everySheet, bool cameraClipping)
{
    Layer const& layer = scene.layers[l];
    painter.save();
    auto const restore = qScopeGuard([&painter] { painter.restore(); });
    bool const nearestBefore = gNearestPictures;
    gNearestPictures = layer.textureFilter == Layer::FilterNearest;
    Layer const* const pictureLayerBefore = gPictureLayer;
    gPictureLayer = &layer;
    auto const restoreNearest = qScopeGuard([nearestBefore, pictureLayerBefore] {
        gNearestPictures = nearestBefore;
        gPictureLayer = pictureLayerBefore;
    });
    if (cameraClipping && scene.cameraIndex() >= 0 && layer.holdsDrawings() && !drawing.isEmpty()) {
        QRectF const bounds = drawing.bounds().adjusted(-2, -2, 2, 2);
        QPolygonF const clipped = scene.cameraClip(l, frame, bounds);
        if (clipped.isEmpty())
            return;
        if (clipped != QPolygonF(bounds)) {
            QPainterPath area;
            area.addPolygon(clipped);
            painter.setClipPath(area, Qt::IntersectClip);
        }
    }
    if (layer.clipping && layer.holdsDrawings()) {
        // Layer Properties > Clipping: only what lies in the frame (made larger or smaller by the factors) shows, in the layer's own space.
        double const w = scene.width * layer.clipX, h = scene.height * layer.clipY;
        painter.setClipRect(QRectF(scene.width / 2.0 - w / 2.0, scene.height / 2.0 - h / 2.0, w, h), Qt::IntersectClip);
    }
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
        QImage alone(QSize(int(device->width() * dpr), int(device->height() * dpr)), Deep::formatFor(device));
        alone.setDevicePixelRatio(dpr);
        alone.fill(Qt::transparent);
        {
            QPainter off(&alone);
            off.setRenderHints(painter.renderHints());
            off.setTransform(painter.transform());
            off.setClipRegion(painter.clipRegion(), painter.hasClipping() ? Qt::ReplaceClip : Qt::NoClip);
            tPaintingToBlend = true;
            paintLayerDrawing(off, scene, l, frame, drawing, opacity, sceneToDevice, everySheet, cameraClipping);
            tPaintingToBlend = false;
        }
        painter.save();
        painter.resetTransform();
        painter.setCompositionMode(kModes[std::clamp(layer.blend, 0, int(Layer::BlendCount) - 1)]);
        painter.drawImage(0, 0, alone);
        painter.restore();
        return;
    }
    // Its vector render options: hard edges; or the edges worked out at twice the size; their soft edge made harder or softer.
    if (layer.antialiasing == Layer::AntialiasLow)
        painter.setRenderHint(QPainter::Antialiasing, false);
    bool const veryHigh = layer.antialiasing == Layer::AntialiasVeryHigh && painter.testRenderHint(QPainter::Antialiasing);
    bool const exponent = std::abs(layer.antialiasExponent - 1.0) > 1e-6;
    if ((veryHigh || exponent) && !tSmoothed && opacity > 0.0 && painter.device()) {
        QPaintDevice* const device = painter.device();
        double const dpr = device->devicePixelRatioF();
        int const times = veryHigh ? 2 : 1;
        QSize const size(int(device->width() * dpr), int(device->height() * dpr));
        QImage alone(size * times, Deep::formatFor(device));
        alone.setDevicePixelRatio(dpr * times);
        alone.fill(Qt::transparent);
        {
            QPainter off(&alone);
            off.setRenderHints(painter.renderHints());
            off.setTransform(painter.transform());
            off.setClipRegion(painter.clipRegion(), painter.hasClipping() ? Qt::ReplaceClip : Qt::NoClip);
            tSmoothed = true;
            paintLayerDrawing(off, scene, l, frame, drawing, opacity, sceneToDevice, everySheet, cameraClipping);
            tSmoothed = false;
        }
        if (times > 1)
            alone = alone.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (exponent)
            antialiasExponent(alone, layer.antialiasExponent);
        alone.setDevicePixelRatio(dpr);
        painter.save();
        painter.resetTransform();
        painter.drawImage(0, 0, alone);
        painter.restore();
        return;
    }
    if (!layer.paletteOverrides.isEmpty() && !tOverridden) {
        // Its palette overrides: the colours they clone are drawn in the clones.
        QHash<int, int> const swap = scene.overridesOf(l);
        if (!swap.isEmpty()) {
            Drawing swapped = drawing;
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Stroke& stroke : swapped.strokesOf(sheet))
                    stroke.swatch = swap.value(stroke.swatch, stroke.swatch);
                for (Fill& fill : swapped.fillsOf(sheet))
                    fill.swatch = swap.value(fill.swatch, fill.swatch);
            }
            tOverridden = true;
            paintLayerDrawing(painter, scene, l, frame, swapped, opacity, sceneToDevice, everySheet, cameraClipping);
            tOverridden = false;
            return;
        }
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
        paintLayerDrawing(painter, scene, l, frame, kept, opacity, sceneToDevice, true, cameraClipping);
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
        QImage image(QSize(int(device->width() * dpr), int(device->height() * dpr)), Deep::formatFor(device));
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
    QImage image(size, Deep::formatFor(device));
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
                    plain = QImage((area.size() * dpr).toSize().expandedTo(QSize(1, 1)), Deep::formatFor(device));
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
        paintLayerDrawing(mp, scene, m, frame, shownMatte, 1.0, sceneToDevice, false, cameraClipping);
        --tMatteDepth;
    };
    Pose const pose = scene.poseOf(l, frame);
    LayerEffectSpace const space{painter, scene, l, frame, sceneToDevice, layer, size, dpr, pixelsPerUnit, pose, paintMatte, symbol};
    for (int i = 0; i < layer.effects.size(); ++i)
        applyLayerEffect(image, i, space);
    painter.save();
    painter.resetTransform();
    painter.setOpacity(painter.opacity() * opacity);
    painter.drawImage(0, 0, image);
    painter.restore();
}

