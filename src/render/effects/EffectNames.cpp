// Eclipse - the names effects are listed by.
#include "render/effects/Effects.h"

#include <QColor>
#include <QDir>
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
        return QObject::tr("Apply Peg  %1").arg(l >= 0 ? scene.layers[l].name : QObject::tr("(none)"));
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
    case Effect::Tone:
    case Effect::Highlight: {
        int const matte = scene.layerIndex(effect.matte);
        return (effect.type == Effect::Tone ? QObject::tr("Tone  %1%2") : QObject::tr("Highlight  %1%2"))
            .arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"), effect.invert ? QObject::tr("  inverted") : QString());
    }
    default: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Cutter  %1%2").arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"),
                                               effect.invert ? QObject::tr("  inverted") : QString());
    }
    }
}

}
