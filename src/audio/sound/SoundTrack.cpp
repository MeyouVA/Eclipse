// Eclipse - a sound layer's track: its clips placed, looped and shaped, then its effects (filters, echo, reverb, speed, tempo, pitch).
#include "audio/sound/Sound.h"

#include <QHash>
#include <QMutex>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/scene/Layer.h"

namespace Sound {

namespace {

constexpr double kRate = SoundData::kRate;

// RBJ biquad, low or high pass, run over each channel.
void biquad(QVector<float>& s, double cutoff, bool high)
{
    double const w = 2.0 * std::numbers::pi * std::clamp(cutoff, 10.0, kRate * 0.45) / kRate;
    double const alpha = std::sin(w) / (2.0 * 0.7071);
    double const cosw = std::cos(w);
    double b0 = high ? (1.0 + cosw) / 2.0 : (1.0 - cosw) / 2.0;
    double b1 = high ? -(1.0 + cosw) : 1.0 - cosw;
    double b2 = b0;
    double const a0 = 1.0 + alpha, a1 = -2.0 * cosw, a2 = 1.0 - alpha;
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    for (int c = 0; c < 2; ++c) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (qsizetype i = c; i < s.size(); i += 2) {
            double const x = s[i];
            double const y = b0 * x + b1 * x1 + b2 * x2 - a1 / a0 * y1 - a2 / a0 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            s[i] = float(y);
        }
    }
}

void echo(QVector<float>& s, double delayMs, double feedback, double mix)
{
    qsizetype const d = std::max<qsizetype>(1, qsizetype(delayMs / 1000.0 * kRate)) * 2;
    QVector<float> y = s;
    for (qsizetype i = d; i < y.size(); ++i)
        y[i] += float(feedback * y[i - d]);
    for (qsizetype i = 0; i < s.size(); ++i)
        s[i] = float(s[i] * (1.0 - mix) + y[i] * mix);
}

// Schroeder: four damped combs in parallel, two all-passes after them, for each channel.
void reverb(QVector<float>& s, double room, double damping, double mix)
{
    static int const combs[4] = {1116, 1188, 1277, 1356};
    static int const passes[2] = {556, 441};
    double const feedback = 0.7 + 0.28 * room, damp = 0.4 * damping;
    qsizetype const frames = s.size() / 2;
    for (int c = 0; c < 2; ++c) {
        QVector<double> wet(frames, 0.0);
        for (int k = 0; k < 4; ++k) {
            int const length = combs[k] + c * 23;
            QVector<double> line(length, 0.0);
            double filtered = 0.0;
            for (qsizetype i = 0, at = 0; i < frames; ++i, at = (at + 1) % length) {
                double const out = line[at];
                filtered = out * (1.0 - damp) + filtered * damp;
                line[at] = s[i * 2 + c] * 0.015 + filtered * feedback;
                wet[i] += out;
            }
        }
        for (int k = 0; k < 2; ++k) {
            int const length = passes[k] + c * 23;
            QVector<double> line(length, 0.0);
            for (qsizetype i = 0, at = 0; i < frames; ++i, at = (at + 1) % length) {
                double const held = line[at];
                double const out = -wet[i] + held;
                line[at] = wet[i] + held * 0.5;
                wet[i] = out;
            }
        }
        for (qsizetype i = 0; i < frames; ++i)
            s[i * 2 + c] = float(s[i * 2 + c] * (1.0 - mix) + wet[i] * 3.0 * mix);
    }
}

// Faster or slower by `speed`, the pitch going with it.
void resample(QVector<float>& s, double speed)
{
    qsizetype const frames = s.size() / 2;
    if (frames < 2 || std::abs(speed - 1.0) < 1e-6)
        return;
    qsizetype const out = std::max<qsizetype>(1, qsizetype(frames / speed));
    QVector<float> r(out * 2);
    for (qsizetype i = 0; i < out; ++i) {
        double const at = i * speed;
        qsizetype const a = std::min<qsizetype>(frames - 1, qsizetype(at));
        qsizetype const b = std::min<qsizetype>(frames - 1, a + 1);
        double const t = at - double(a);
        for (int c = 0; c < 2; ++c)
            r[i * 2 + c] = float(s[a * 2 + c] * (1.0 - t) + s[b * 2 + c] * t);
    }
    s = r;
}

// Faster or slower by `speed`, the pitch kept: windows of the sound overlapped anew, each moved to where it fits best (WSOLA).
void tempo(QVector<float>& s, double speed)
{
    qsizetype const frames = s.size() / 2;
    if (frames < 2 || std::abs(speed - 1.0) < 1e-6)
        return;
    if(frames<8) { resample(s,speed); return; }
    int window=8;
    while(window<2048 && window*2<=frames/2) window*=2;
    int const hop=window/2,search=std::min(384,hop/2);
    qsizetype const out = qsizetype(frames / speed);
    QVector<float> r((out + window) * 2, 0.0f);
    QVector<float> weight(out + window, 0.0f);
    QVector<double> hann(window);
    for (int i = 0; i < window; ++i)
        hann[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / window);
    auto mono = [&](qsizetype i) { return i >= 0 && i < frames ? double(s[i * 2]) + double(s[i * 2 + 1]) : 0.0; };
    qsizetype previous = -1;
    for (qsizetype to = 0; to < out; to += hop) {
        qsizetype from = qsizetype(to * speed);
        if (previous >= 0) {
            // Near the place asked for: the one whose start is most like what follows the window put down last.
            qsizetype const natural = previous + hop;
            double best = -1e300;
            qsizetype bestAt = from;
            for (qsizetype at = std::max<qsizetype>(0, from - search); at <= std::min(frames - 1, from + search); at += 8) {
                double sum = 0.0;
                for (int i = 0; i < hop; i += 4)
                    sum += mono(natural + i) * mono(at + i);
                if (sum > best) {
                    best = sum;
                    bestAt = at;
                }
            }
            from = bestAt;
        }
        for (int i = 0; i < window && to + i < out + window; ++i) {
            qsizetype const src = from + i;
            if (src >= frames)
                break;
            r[(to + i) * 2] += float(s[src * 2] * hann[i]);
            r[(to + i) * 2 + 1] += float(s[src * 2 + 1] * hann[i]);
            weight[to + i] += float(hann[i]);
        }
        previous = from;
    }
    r.resize(out * 2);
    for (qsizetype i = 0; i < out; ++i)
        if (weight[i] > 1e-3f) {
            r[i * 2] /= weight[i];
            r[i * 2 + 1] /= weight[i];
        }
    s = r;
}

QString keyOf(Layer const& layer, double fps)
{
    QString key = QString::number(fps, 'g', 10) + QLatin1Char('|') + QString::number(layer.soundVolume, 'g', 10);
    for (SoundClip const& c : layer.clipsOfSound()) {
        key += QStringLiteral("|%1;%2;%3;%4;%5;%6;%7").arg(c.file).arg(c.start).arg(c.stop).arg(c.offset, 0, 'g', 10).arg(c.length, 0, 'g', 10).arg(c.loops).arg(c.volume, 0, 'g', 10);
        for (QPointF const& p : c.envelope)
            key += QStringLiteral(",%1:%2").arg(p.x(), 0, 'g', 10).arg(p.y(), 0, 'g', 10);
    }
    for (SoundEffect const& e : layer.soundEffects)
        key += QStringLiteral("|e%1;%2;%3;%4;%5").arg(e.type).arg(int(e.enabled)).arg(e.a, 0, 'g', 10).arg(e.b, 0, 'g', 10).arg(e.c, 0, 'g', 10);
    return key;
}

}  // namespace

void applyEffect(SoundEffect const& e, QVector<float>& s)
{
    if (!e.enabled)
        return;
    switch (e.type) {
    case SoundEffect::Volume: {
        float const g = float(std::pow(10.0, e.a / 20.0));
        for (float& v : s)
            v *= g;
        break;
    }
    case SoundEffect::Balance: {
        double const p = std::clamp(e.a / 100.0, -1.0, 1.0);
        float const left = float(std::min(1.0, 1.0 - p)), right = float(std::min(1.0, 1.0 + p));
        for (qsizetype i = 0; i + 1 < s.size(); i += 2) {
            s[i] *= left;
            s[i + 1] *= right;
        }
        break;
    }
    case SoundEffect::Echo:
        echo(s, e.a, std::clamp(e.b / 100.0, 0.0, 0.95), std::clamp(e.c / 100.0, 0.0, 1.0));
        break;
    case SoundEffect::Reverb:
        reverb(s, std::clamp(e.a / 100.0, 0.0, 1.0), std::clamp(e.b / 100.0, 0.0, 1.0), std::clamp(e.c / 100.0, 0.0, 1.0));
        break;
    case SoundEffect::LowPass:
        biquad(s, e.a, false);
        break;
    case SoundEffect::HighPass:
        biquad(s, e.a, true);
        break;
    case SoundEffect::PitchShift: {
        // Stretched by the ratio with its pitch kept, then played that much faster: as long as before, higher or lower.
        double const ratio = std::pow(2.0, std::clamp(e.a, -24.0, 24.0) / 12.0);
        qsizetype const size=s.size();
        tempo(s, 1.0 / ratio);
        resample(s, ratio);
        s.resize(size);
        break;
    }
    case SoundEffect::Resample:
        resample(s, std::clamp(e.a / 100.0, 0.25, 4.0));
        break;
    case SoundEffect::Tempo:
        tempo(s, std::clamp(e.a / 100.0, 0.25, 4.0));
        break;
    default:
        break;
    }
}

std::shared_ptr<SoundData> track(Layer const& layer, double fps, QString* error)
{
    QVector<SoundClip> const clips = layer.clipsOfSound();
    if (clips.isEmpty())
        return nullptr;
    // Just one file played as it is: the file itself.
    bool const plain = clips.size() == 1 && clips[0].start == 0 && clips[0].stop < 0 && clips[0].offset == 0.0 && clips[0].length < 0.0
                       && clips[0].loops == 1 && clips[0].volume == 1.0 && clips[0].envelope.isEmpty() && layer.soundVolume == 1.0
                       && std::none_of(layer.soundEffects.cbegin(), layer.soundEffects.cend(), [](SoundEffect const& e) { return e.enabled; });
    if (plain)
        return load(clips[0].file, error);

    static QMutex lock;
    static QHash<QString, std::shared_ptr<SoundData>> made;
    QString key = keyOf(layer, fps);
    QVector<std::shared_ptr<SoundData>> sources;
    QHash<QString,std::shared_ptr<SoundData>> decoded;
    bool cacheable=true;
    for(SoundClip const& clip : clips) {
        QString why;
        auto source=decoded.contains(clip.file)?decoded.value(clip.file):load(clip.file,&why);
        decoded.insert(clip.file,source);
        if(!source && error && error->isEmpty()) *error=why;
        key+=QLatin1Char('|')+(source?QString::fromLatin1(source->contentHash.toHex()):QStringLiteral("missing"));
        if(source && source->contentHash.isEmpty()) cacheable=false;
        sources.push_back(source);
    }
    {
        QMutexLocker locked(&lock);
        if (auto const found = made.constFind(key); cacheable && found != made.constEnd())
            return *found;
    }
    fps = fps > 0.0 ? fps : 24.0;
    QVector<float> sum;
    QVector<QPair<qint64,qint64>> ranges;
    bool any = false;
    for (qsizetype index=0;index<clips.size();++index) {
        SoundClip const& clip=clips[index];
        std::shared_ptr<SoundData> const data=sources[index];
        if(!data) continue;
        qint64 const from = std::clamp<qint64>(qint64(clip.offset * kRate), 0, data->frames());
        qint64 const part = clip.length < 0.0 ? data->frames() - from : std::min<qint64>(data->frames() - from, qint64(clip.length * kRate));
        if (part <= 0)
            continue;
        qint64 const begin = qint64(std::llround(clip.start / fps * kRate));
        qint64 const played = part * std::max(1, clip.loops);
        qint64 const span = clip.stop >= 0 ? std::max<qint64>(0, qint64(std::llround(clip.stop / fps * kRate)) - begin) : played;
        if (begin + span <= 0)
            continue;
        if (sum.size() < (begin + span) * 2)
            sum.resize((begin + span) * 2, 0.0f);
        any = true;
        ranges.push_back({std::max<qint64>(0,begin),begin+std::min(span,played)});
        for (qint64 i = std::max<qint64>(0, -begin); i < span; ++i) {
            if (i >= played)
                break;      // a stop after the loops end: silence
            qint64 const src = from + i % part;
            float const gain = float(clip.volume * clip.envelopeAt(double(i) / kRate * fps));
            sum[(begin + i) * 2] += data->samples[src * 2] / 32768.0f * gain;
            sum[(begin + i) * 2 + 1] += data->samples[src * 2 + 1] / 32768.0f * gain;
        }
    }
    if (!any)
        return nullptr;
    if (layer.soundVolume != 1.0)
        for (float& v : sum)
            v *= float(layer.soundVolume);
    for (SoundEffect const& effect : layer.soundEffects) {
        qsizetype const before=sum.size();
        applyEffect(effect, sum);
        if(before>0 && before!=sum.size()) for(auto& span : ranges) { span.first=qint64(std::floor(double(span.first)*sum.size()/before)); span.second=qint64(std::ceil(double(span.second)*sum.size()/before)); }
        if(effect.enabled && (effect.type==SoundEffect::Echo || effect.type==SoundEffect::Reverb) && effect.c>0) ranges={{0,sum.size()/2}};
    }
    auto data = std::make_shared<SoundData>();
    data->ranges=ranges;
    data->samples.resize(sum.size());
    for (qsizetype i = 0; i < sum.size(); ++i)
        data->samples[i] = qint16(std::clamp(sum[i], -1.0f, 1.0f) * 32767.0f);
    QMutexLocker locked(&lock);
    if (made.size() > 32)
        made.clear();
    if(cacheable) made.insert(key, data);
    return data;
}

}  // namespace Sound
