// Eclipse - the names effects are listed by.
#include "render/effects/Effects.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include <QObject>

namespace Effects {

QString kindName(Effect::Type type)
{
    switch (type) {
    case Effect::GaussianBlur: return QObject::tr("Blur-Gaussian");
    case Effect::BoxBlur: return QObject::tr("Blur-Box");
    case Effect::ZoomBlur: return QObject::tr("Blur-Radial-Zoom");
    case Effect::BokehBlur: return QObject::tr("Bokeh Blur");
    case Effect::AntiFlicker: return QObject::tr("Anti-Flicker");
    case Effect::ColourFade: return QObject::tr("Colour-Fade");
    case Effect::ColourOverride: return QObject::tr("Colour-Override");
    case Effect::ChannelSwap: return QObject::tr("Channel-Swap");
    case Effect::ColourLevels: return QObject::tr("Colour Levels");
    case Effect::ColourScreen: return QObject::tr("Colour-Screen");
    case Effect::Contrast: return QObject::tr("Contrast");
    case Effect::Gamma: return QObject::tr("Gamma");
    case Effect::ColourBanding: return QObject::tr("Colour Banding");
    case Effect::Dither: return QObject::tr("Dither");
    case Effect::Shine: return QObject::tr("Shine");
    case Effect::LensFlare: return QObject::tr("Lens Flare");
    case Effect::BurnIn: return QObject::tr("Burn-In");
    case Effect::TurbulentNoise: return QObject::tr("Turbulent Noise");
    case Effect::Grid: return QObject::tr("Grid");
    case Effect::Refract: return QObject::tr("Refract");
    case Effect::Shake: return QObject::tr("Shake");
    case Effect::Quake: return QObject::tr("Quake");
    case Effect::UnsharpMask: return QObject::tr("Unsharp Mask");
    case Effect::IncreaseOpacity: return QObject::tr("Increase Opacity");
    case Effect::AnimatePencilTexture: return QObject::tr("Animate Pencil Texture");
    case Effect::ColourSpace: return QObject::tr("Colour Space");
    case Effect::LookUpTable: return QObject::tr("Look-Up Table");
    case Effect::HoldTiming: return QObject::tr("Hold Timing");
    case Effect::HoldTransformationTiming: return QObject::tr("Hold Transformation Timing");
    case Effect::Retime: return QObject::tr("Retime");
    case Effect::RetimeTransformation: return QObject::tr("Retime Transformation");
    case Effect::Loop: return QObject::tr("Loop");
    case Effect::LoopTransformation: return QObject::tr("Loop Transformation");
    case Effect::ShapeCurve: return QObject::tr("Shape-Curve");
    default: return name(Effect::made(type), Scene()).section(QStringLiteral("  "), 0, 0);
    }
}

QString name(Effect const& effect, Scene const& scene)
{
    switch (effect.type) {
    case Effect::Glow:
        return QObject::tr("Glow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Shadow:
        return QObject::tr("Drop Shadow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Blur:
        return QObject::tr("Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Tint:
        return QObject::tr("Colour Tint  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Particles:
        return QObject::tr("Particles  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Transparency:
        return QObject::tr("Transparency  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::DirectionalBlur:
        return QObject::tr("Directional Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::HueSaturation:
        return QObject::tr("Hue-Saturation  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::BrightnessContrast:
        return QObject::tr("Brightness-Contrast  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Grain:
        return QObject::tr("Grain  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::RadialBlur:
        return QObject::tr("Radial Blur  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::ColourScale:
        return QObject::tr("Colour-Scale  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Negate:
        return QObject::tr("Negate  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Median:
        return QObject::tr("Median  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Ripple:
        return QObject::tr("Ripple  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Bloom:
        return QObject::tr("Bloom  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Pixelate:
        return QObject::tr("Pixelate  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Turbulence:
        return QObject::tr("Turbulence  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Gradient:
        return QObject::tr("Gradient  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::BlackWhite:
        return QObject::tr("Black & White  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::MatteResize:
        return QObject::tr("Matte Resize  %1%2").arg(effect.invert ? QStringLiteral("-") : QStringLiteral("+")).arg(effect.amount, 0, 'f', 1);
    case Effect::MatteBlur: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Matte Blur  %1  %2%3").arg(effect.amount, 0, 'f', 1)
            .arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"), effect.invert ? QObject::tr("  inverted") : QString());
    }
    case Effect::MatteGenerator:
        return QObject::tr("Matte Generator  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::ChannelSelector: {
        static char const* const channels[4] = {QT_TR_NOOP("Red"), QT_TR_NOOP("Green"), QT_TR_NOOP("Blue"), QT_TR_NOOP("Opacity")};
        return QObject::tr("Channel Selector  %1").arg(QObject::tr(channels[std::clamp(effect.channel, 0, 3)]));
    }
    case Effect::Crop:
        return effect.area.isEmpty() ? QObject::tr("Crop  (all)")
                                     : QObject::tr("Crop  %1 x %2").arg(effect.area.width(), 0, 'f', 0).arg(effect.area.height(), 0, 'f', 0);
    case Effect::RemoveTransparency:
        return QObject::tr("Remove Transparency  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::MatteComposite:
        return QObject::tr("Matte Composite");
    case Effect::MotionBlur:
        return QObject::tr("Motion Blur  %1 frames").arg(int(std::lround(effect.amount)));
    case Effect::FieldChart:
        return QObject::tr("Field Chart");
    case Effect::ColourCurves:
        return QObject::tr("Colour Curves");
    case Effect::ApplyPeg: {
        int const l = scene.layerIndex(effect.matte);
        return QObject::tr("Apply Peg  %1").arg(l >= 0 ? scene.layers[l].name : QObject::tr("No input peg connected"));
    }
    case Effect::ApplyCamera: {
        int const l = scene.layerIndex(effect.matte);
        return QObject::tr("Apply Camera  %1").arg(l >= 0 ? scene.layers[l].name : QObject::tr("(none)"));
    }
    case Effect::ScaleOutput:
        return QObject::tr("Scale Output  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Focus:
        return QObject::tr("Focus  %1 at %2").arg(effect.amount, 0, 'f', 1).arg(effect.direction, 0, 'f', 0);
    case Effect::Blending:
        return QObject::tr("Blending");
    case Effect::Glue:
        return QObject::tr("Glue  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Script:
        return QObject::tr("Script");
    case Effect::External: {
        QString const program = effect.text.section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
        return program.isEmpty() ? QObject::tr("External") : QObject::tr("External  %1").arg(program);
    }
    case Effect::VariableBlur: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Variable Blur  %1  %2").arg(effect.amount, 0, 'f', 1).arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"));
    }
    case Effect::GenericComposite:
        return QObject::tr("Generic Composite  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::LayerSelector: {
        QStringList arts;
        QString const names[4] = {QObject::tr("Underlay"), QObject::tr("Colour Art"), QObject::tr("Line Art"), QObject::tr("Overlay")};
        for (int sheet = 0; sheet < 4; ++sheet)
            if (effect.sheets & (1 << sheet))
                arts.push_back(names[sheet]);
        return QObject::tr("Layer Selector  %1").arg(arts.isEmpty() ? QObject::tr("(nothing)") : arts.join(QStringLiteral(", ")));
    }
    case Effect::ChromaKey:
        return QObject::tr("Chroma Keying  %1").arg(effect.color.name());
    case Effect::DifferenceKeyer:
        return QObject::tr("RGB Difference Keyer  %1").arg(effect.amount, 0, 'f', 2);
    case Effect::LuminanceThreshold:
        return QObject::tr("Luminance Threshold  %1").arg(effect.amount, 0, 'f', 2);
    case Effect::ImageSwitch:
        return QObject::tr("Image Switch  %1").arg(int(std::lround(effect.amount)) + 1);
    case Effect::Visibility:
        return QObject::tr("Visibility  %1").arg(effect.showInViews && effect.showInRender ? QObject::tr("everywhere")
                                                 : effect.showInViews ? QObject::tr("views only") : effect.showInRender ? QObject::tr("render only")
                                                 : QObject::tr("hidden"));
    case Effect::Tone:
    case Effect::Highlight: {
        int const matte = scene.layerIndex(effect.matte);
        return (effect.type == Effect::Tone ? QObject::tr("Tone  %1%2") : QObject::tr("Highlight  %1%2"))
            .arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"), effect.invert ? QObject::tr("  inverted") : QString());
    }
    case Effect::GaussianBlur:
    case Effect::BoxBlur:
    case Effect::ZoomBlur:
    case Effect::BokehBlur:
    case Effect::AntiFlicker:
    case Effect::ColourFade:
    case Effect::ColourOverride:
    case Effect::ChannelSwap:
    case Effect::ColourLevels:
    case Effect::ColourScreen:
    case Effect::Contrast:
    case Effect::Gamma:
    case Effect::ColourBanding:
    case Effect::Dither:
    case Effect::Shine:
    case Effect::LensFlare:
    case Effect::BurnIn:
    case Effect::TurbulentNoise:
    case Effect::Grid:
    case Effect::Refract:
    case Effect::Shake:
    case Effect::Quake:
    case Effect::UnsharpMask:
    case Effect::IncreaseOpacity:
    case Effect::ColourSpace:
    case Effect::LookUpTable:
    case Effect::HoldTiming:
    case Effect::HoldTransformationTiming:
    case Effect::Retime:
    case Effect::RetimeTransformation:
    case Effect::Loop:
    case Effect::LoopTransformation:
    case Effect::ShapeCurve:
    case Effect::AnimatePencilTexture: {
        Effect::Amount const a = Effect::amountOf(effect.type);
        QString const kind = kindName(effect.type);
        if (effect.type == Effect::ColourSpace)
            return QStringLiteral("%1  %2 > %3").arg(kind, effect.fromSpace.isEmpty() ? QObject::tr("(working)") : effect.fromSpace,
                                                     effect.toSpace.isEmpty() ? QObject::tr("(working)") : effect.toSpace);
        if (effect.type == Effect::LookUpTable)
            return QStringLiteral("%1  %2").arg(kind, effect.file.isEmpty() ? QObject::tr("(no file)") : QFileInfo(effect.file).fileName());
        if (effect.type == Effect::HoldTiming || effect.type == Effect::HoldTransformationTiming)
            return QObject::tr("%1  on %2s").arg(kind).arg(int(effect.value("hold")));
        if (effect.type == Effect::Retime || effect.type == Effect::RetimeTransformation)
            return effect.value("useTarget") > 0.5 ? kind : QObject::tr("%1  %2 frames").arg(kind).arg(int(effect.value("offset")));
        if (effect.type == Effect::Loop || effect.type == Effect::LoopTransformation)
            return QObject::tr("%1  %2-%3").arg(kind).arg(int(effect.value("start"))).arg(int(effect.value("end")));
        if (effect.type == Effect::ColourOverride)
            return QObject::tr("%1  %n colour(s)", nullptr, int(effect.overrides.size())).arg(kind);
        if (a.shown == 1 && effect.amount < 1.0)
            return QStringLiteral("%1  %2%").arg(kind).arg(int(std::lround(effect.amount * 100.0)));
        if (a.shown == 2 || a.shown == 3)
            return QStringLiteral("%1  %2").arg(kind).arg(effect.amount, 0, 'f', 1);
        return kind;
    }
    default: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Cutter  %1%2").arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"),
                                               effect.invert ? QObject::tr("  inverted") : QString());
    }
    }
}

}
