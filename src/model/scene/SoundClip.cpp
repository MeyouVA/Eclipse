// Eclipse - sound clips and sound effects: a clip's gain envelope, and what each kind of effect has to set.
#include "model/scene/Layer.h"

#include <QCoreApplication>

double SoundClip::envelopeAt(double frame) const
{
    if (envelope.isEmpty())
        return 1.0;
    if (frame <= envelope.first().x())
        return envelope.first().y();
    for (int i = 1; i < envelope.size(); ++i) {
        QPointF const a = envelope[i - 1], b = envelope[i];
        if (frame <= b.x()) {
            double const span = b.x() - a.x();
            return span <= 0.0 ? b.y() : a.y() + (b.y() - a.y()) * (frame - a.x()) / span;
        }
    }
    return envelope.last().y();
}

QString SoundEffect::name(int type)
{
    static char const* const names[TypeCount] = {"Volume", "Balance", "Echo", "Reverb", "Low Pass", "High Pass", "Pitch Shift", "Resample", "Tempo"};
    return type >= 0 && type < TypeCount ? QCoreApplication::translate("SoundEffect", names[type]) : QString();
}

QVector<SoundEffect::Setting> SoundEffect::settings(int type)
{
    auto tr = [](char const* text) { return QCoreApplication::translate("SoundEffect", text); };
    switch (type) {
    case Volume:
        return {{tr("Gain (dB)"), -60.0, 24.0, 0.0}};
    case Balance:
        return {{tr("Pan (left -100, right 100)"), -100.0, 100.0, 0.0}};
    case Echo:
        return {{tr("Delay (ms)"), 10.0, 2000.0, 300.0}, {tr("Feedback (%)"), 0.0, 95.0, 40.0}, {tr("Mix (%)"), 0.0, 100.0, 50.0}};
    case Reverb:
        return {{tr("Room Size (%)"), 0.0, 100.0, 50.0}, {tr("Damping (%)"), 0.0, 100.0, 50.0}, {tr("Mix (%)"), 0.0, 100.0, 30.0}};
    case LowPass:
        return {{tr("Cutoff (Hz)"), 20.0, 20000.0, 2000.0}};
    case HighPass:
        return {{tr("Cutoff (Hz)"), 20.0, 20000.0, 300.0}};
    case PitchShift:
        return {{tr("Semitones"), -24.0, 24.0, 0.0}};
    case Resample:
        return {{tr("Speed (%) - pitch goes with it"), 25.0, 400.0, 100.0}};
    case Tempo:
        return {{tr("Speed (%) - pitch kept"), 25.0, 400.0, 100.0}};
    default:
        return {};
    }
}

SoundEffect SoundEffect::make(int type)
{
    SoundEffect effect;
    effect.type = type;
    QVector<Setting> const s = settings(type);
    effect.a = s.value(0).initial;
    effect.b = s.value(1).initial;
    effect.c = s.value(2).initial;
    return effect;
}
