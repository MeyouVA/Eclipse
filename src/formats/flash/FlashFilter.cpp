// Eclipse - Flash's blend modes and filters as a layer's blend mode and effects.
#include "formats/flash/FlashFilter.h"

#include <QHash>

#include <cmath>

#include "model/scene/Scene.h"

namespace Flash {

void applyLook(Layer& layer, QString const& blend, QVector<Filter> const& filters, double scale)
{
    static QHash<QString, int> const blends{{QStringLiteral("add"), Layer::BlendAdd},
                                            {QStringLiteral("multiply"), Layer::BlendMultiply},
                                            {QStringLiteral("screen"), Layer::BlendScreen},
                                            {QStringLiteral("overlay"), Layer::BlendOverlay},
                                            {QStringLiteral("darken"), Layer::BlendDarken},
                                            {QStringLiteral("lighten"), Layer::BlendLighten},
                                            {QStringLiteral("hardlight"), Layer::BlendHardLight},
                                            {QStringLiteral("difference"), Layer::BlendDifference}};
    if (!blend.isEmpty())
        layer.blend = blends.value(blend, Layer::BlendNormal);
    for (Filter const& f : filters) {
        double const soft = (f.blurX + f.blurY) / 4.0 * scale;
        if (f.kind == QLatin1String("BlurFilter")) {
            Effect blur = Effect::made(Effect::Blur);
            blur.amount = soft;
            layer.effects.push_back(blur);
        } else if (f.kind == QLatin1String("GlowFilter") && !f.inner) {
            Effect glow = Effect::made(Effect::Glow);
            glow.amount = soft;
            glow.color = f.colour;
            layer.effects.push_back(glow);
        } else if (f.kind == QLatin1String("DropShadowFilter") && !f.inner) {
            Effect shadow = Effect::made(Effect::Shadow);
            shadow.amount = soft;
            shadow.color = f.colour;
            double const a = f.angle * 3.14159265358979323846 / 180.0;
            shadow.offset = QPointF(std::cos(a), std::sin(a)) * f.distance * scale;
            layer.effects.push_back(shadow);
        }
    }
}

}  // namespace Flash
