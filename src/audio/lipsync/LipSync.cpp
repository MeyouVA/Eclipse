#include "audio/lipsync/LipSync.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace LipSync {

int indexOf(QChar shape)
{
    for (int i = 0; i < kShapeCount; ++i)
        if (shape.toUpper() == QLatin1Char(kShapes[i]))
            return i;
    return -1;
}

QString describe(QChar shape)
{
    switch (indexOf(shape)) {
    case 0: return QCoreApplication::translate("LipSync", "A - closed (m, b, p)");
    case 1: return QCoreApplication::translate("LipSync", "B - teeth together (s, t, d, k, i)");
    case 2: return QCoreApplication::translate("LipSync", "C - open (e)");
    case 3: return QCoreApplication::translate("LipSync", "D - wide open (a)");
    case 4: return QCoreApplication::translate("LipSync", "E - round (o)");
    case 5: return QCoreApplication::translate("LipSync", "F - small and pushed out (u, w)");
    case 6: return QCoreApplication::translate("LipSync", "G - teeth on the lip (f, v)");
    case 7: return QCoreApplication::translate("LipSync", "X - at rest");
    default: return QString();
    }
}

QString detect(QVector<qint16> const& stereo, int rate, double fps)
{
    qint64 const total = stereo.size() / 2;
    if (total <= 0 || rate <= 0 || fps <= 0.0)
        return QString();
    double const perFrame = rate / fps;
    int const frames = int(std::ceil(total / perFrame));
    // What each frame sounds like: how loud, how hissy, how much of it lies low.
    struct Heard {
        double loud = 0.0;      // root mean square, 0..1
        double hiss = 0.0;      // how often the wave crosses nought, per sample
        double low = 0.0;       // the share of its strength below some 700 Hz
    };
    QVector<Heard> heard(frames);
    double const ease = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 700.0 / rate);
    double loudest = 0.0;
    for (int f = 0; f < frames; ++f) {
        qint64 const first = qint64(f * perFrame), last = std::min(total, qint64((f + 1) * perFrame));
        if (last <= first)
            continue;
        double sum = 0.0, lowSum = 0.0, smooth = 0.0, before = 0.0;
        int crossings = 0;
        for (qint64 i = first; i < last; ++i) {
            double const v = (double(stereo[i * 2]) + double(stereo[i * 2 + 1])) / 65536.0;
            sum += v * v;
            smooth += ease * (v - smooth);
            lowSum += smooth * smooth;
            if (i > first && ((v >= 0.0) != (before >= 0.0)))
                ++crossings;
            before = v;
        }
        qint64 const n = last - first;
        heard[f].loud = std::sqrt(sum / n);
        heard[f].hiss = n > 1 ? double(crossings) / double(n - 1) : 0.0;
        heard[f].low = sum > 1e-12 ? lowSum / sum : 0.0;
        loudest = std::max(loudest, heard[f].loud);
    }
    QString shapes(frames, QLatin1Char('X'));
    if (loudest < 0.004)
        return shapes;      // nothing but silence
    for (int f = 0; f < frames; ++f) {
        Heard const& h = heard[f];
        double const level = h.loud / loudest;
        QChar shape = QLatin1Char('X');
        if (h.loud < 0.004 || level < 0.08)
            shape = QLatin1Char('X');
        else if (h.hiss > 0.2)
            shape = QLatin1Char(level < 0.4 ? 'G' : 'B');           // a hiss: f and v soft, s and t sharp
        else if (level < 0.2)
            shape = QLatin1Char('A');                               // a murmur behind closed lips
        else if (h.low > 0.7)
            shape = QLatin1Char(level > 0.55 ? 'E' : 'F');          // a hum low down: the rounded vowels
        else if (level > 0.65)
            shape = QLatin1Char('D');
        else if (level > 0.4)
            shape = QLatin1Char('C');
        else
            shape = QLatin1Char('B');
        shapes[f] = shape;
    }
    // A shape held for a single frame between two frames of another is a flicker: it goes.
    QString const raw = shapes;
    for (int f = 1; f + 1 < frames; ++f)
        if (raw[f - 1] == raw[f + 1] && raw[f] != raw[f - 1])
            shapes[f] = raw[f - 1];
    return shapes;
}

}
