// Eclipse - the Camera view: each effect of a layer done to its picture.
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
#include "canvas/CanvasPaintPrivate.h"

using namespace canvas_paint;

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
