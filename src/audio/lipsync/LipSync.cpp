#include "audio/lipsync/LipSync.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

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

bool formants(QVector<double> const& samples, int rate, double& first, double& second)
{
    // Down to about 11 kHz (the voice's resonances that tell vowels apart lie below 4 kHz): each sample the mean of so many.
    int const step = std::max(1, int(std::lround(rate / 11025.0)));
    double const fine = double(rate) / step;
    QVector<double> x;
    for (qsizetype i = 0; i + step <= samples.size(); i += step) {
        double sum = 0.0;
        for (int k = 0; k < step; ++k)
            sum += samples[i + k];
        x.push_back(sum / step);
    }
    int const n = int(x.size());
    int const order = 12;
    if (n < 4 * order)
        return false;
    // Lifted highs (as the voice's own fall-off is undone), and a window that fades at both ends.
    for (int i = n - 1; i > 0; --i)
        x[i] -= 0.97 * x[i - 1];
    for (int i = 0; i < n; ++i)
        x[i] *= 0.54 - 0.46 * std::cos(2.0 * 3.14159265358979323846 * i / (n - 1));
    double r[order + 1] = {};
    for (int lag = 0; lag <= order; ++lag)
        for (int i = lag; i < n; ++i)
            r[lag] += x[i] * x[i - lag];
    if (r[0] <= 1e-12)
        return false;
    // Linear prediction (Levinson-Durbin): the filter whose response is the envelope of the spectrum.
    double a[order + 1] = {1.0}, before[order + 1];
    double error = r[0] * (1.0 + 1e-9);
    for (int m = 1; m <= order; ++m) {
        double acc = r[m];
        for (int k = 1; k < m; ++k)
            acc += a[k] * r[m - k];
        double const reflect = -acc / error;
        std::copy(a, a + order + 1, before);
        for (int k = 1; k < m; ++k)
            a[k] = before[k] + reflect * before[m - k];
        a[m] = reflect;
        error *= 1.0 - reflect * reflect;
        if (error <= 0.0)
            return false;
    }
    // The envelope from 150 Hz to 3.8 kHz, and its first two peaks.
    QVector<double> freq, level;
    for (double f = 150.0; f <= std::min(3800.0, fine / 2.0 - 50.0); f += 10.0) {
        double const w = 2.0 * 3.14159265358979323846 * f / fine;
        double re = 0.0, im = 0.0;
        for (int k = 0; k <= order; ++k) {
            re += a[k] * std::cos(w * k);
            im -= a[k] * std::sin(w * k);
        }
        freq.push_back(f);
        level.push_back(-std::log(re * re + im * im + 1e-30));
    }
    QVector<double> peaks;
    for (int i = 1; i + 1 < level.size() && peaks.size() < 2; ++i)
        if (level[i] > level[i - 1] && level[i] >= level[i + 1] && freq[i] >= 220.0)
            peaks.push_back(freq[i]);
    if (peaks.size() < 2)
        return false;
    first = peaks[0];
    second = peaks[1];
    return true;
}

QChar vowelShape(double first, double second)
{
    if (first >= 650.0)
        return QLatin1Char('D');        // a: the jaw well down
    if (second >= 2050.0 && first < 450.0)
        return QLatin1Char('B');        // i, ee: the teeth nearly together
    if (second >= 1450.0)
        return QLatin1Char('C');        // e: open, spread
    if (first < 400.0)
        return QLatin1Char('F');        // u, oo: small and pushed out
    return QLatin1Char('E');            // o: round
}

QString wordShapes(QString const& word)
{
    QString w;
    for (QChar const c : word.toLower())
        if (c.isLetter())
            w += c;
    // A silent e at the end ("make", "tone") says nothing.
    if (w.size() > 2 && w.endsWith(QLatin1Char('e')) && QStringLiteral("aeiouy").indexOf(w.at(w.size() - 2)) < 0)
        w.chop(1);
    QString shapes;
    auto say = [&shapes](char shape) {
        if (shapes.isEmpty() || shapes.back() != QLatin1Char(shape))
            shapes += QLatin1Char(shape);
    };
    for (qsizetype i = 0; i < w.size(); ++i) {
        QString const two = w.mid(i, 2);
        // Two letters that are one sound.
        struct Pair { char const* letters; char shape; };
        static Pair const kPairs[] = {{"th", 'B'}, {"sh", 'B'}, {"ch", 'B'}, {"ph", 'G'}, {"wh", 'F'}, {"ng", 'B'}, {"ck", 'B'},
                                      {"oo", 'F'}, {"ou", 'E'}, {"ow", 'E'}, {"oa", 'E'}, {"ee", 'B'}, {"ea", 'B'}, {"ai", 'C'},
                                      {"ay", 'C'}, {"au", 'E'}, {"aw", 'E'}, {"oi", 'E'}, {"oy", 'E'}, {"ew", 'F'}, {"qu", 'F'}};
        bool paired = false;
        for (Pair const& p : kPairs)
            if (two == QLatin1String(p.letters)) {
                say(p.shape);
                ++i;
                paired = true;
                break;
            }
        if (paired)
            continue;
        switch (w.at(i).unicode()) {
        case 'a': say('D'); break;
        case 'e': say('C'); break;
        case 'i': say('C'); break;
        case 'o': say('E'); break;
        case 'u': say('F'); break;
        case 'y': say(i == 0 ? 'B' : 'C'); break;
        case 'w': say('F'); break;
        case 'm': case 'b': case 'p': say('A'); break;
        case 'f': case 'v': say('G'); break;
        case 'h': break;        // a breath: the mouth as the sound after it has it
        default: say('B'); break;
        }
    }
    return shapes;
}

QString fromText(QString const& text, QString const& detected)
{
    QStringList words;
    for (QString const& word : text.split(QRegularExpression(QStringLiteral("[^\\w']+")), Qt::SkipEmptyParts))
        if (!wordShapes(word).isEmpty())
            words << word;
    int const frames = int(detected.size());
    QString out(frames, QLatin1Char('X'));
    if (words.isEmpty() || frames == 0)
        return out;
    // The stretches of speech: frames not at rest, a single frame at rest within one not breaking it.
    QVector<QPair<int, int>> stretches;     // first frame, frame after the last
    for (int f = 0; f < frames;) {
        if (detected[f] == QLatin1Char('X')) {
            ++f;
            continue;
        }
        int end = f + 1;
        while (end < frames && (detected[end] != QLatin1Char('X') || (end + 1 < frames && detected[end + 1] != QLatin1Char('X'))))
            ++end;
        stretches.push_back({f, end});
        f = end;
    }
    if (stretches.isEmpty())
        stretches.push_back({0, frames});
    // Each word's sounds; a word belongs to the stretch its middle falls in, by sounds against frames of speech.
    QVector<QString> said;
    int sounds = 0;
    for (QString const& word : std::as_const(words)) {
        said.push_back(wordShapes(word));
        sounds += int(said.last().size());
    }
    int speech = 0;
    for (auto const& s : std::as_const(stretches))
        speech += s.second - s.first;
    QVector<QVector<int>> wordsOf(stretches.size());
    int before = 0;
    for (int w = 0; w < said.size(); ++w) {
        double const middle = (before + said[w].size() / 2.0) / sounds * speech;
        before += int(said[w].size());
        int at = 0, covered = 0;
        while (at + 1 < stretches.size() && covered + (stretches[at].second - stretches[at].first) <= middle) {
            covered += stretches[at].second - stretches[at].first;
            ++at;
        }
        wordsOf[at].push_back(w);
    }
    for (int s = 0; s < stretches.size(); ++s) {
        QString line;
        for (int w : std::as_const(wordsOf[s]))
            line += said[w];
        if (line.isEmpty())
            continue;       // a stretch with no word: left at rest
        int const first = stretches[s].first, length = stretches[s].second - first;
        for (int f = 0; f < length; ++f)
            out[first + f] = line.at(std::min(int(line.size()) - 1, int(qint64(f) * line.size() / length)));
    }
    return out;
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
        double burst = 0.0;     // the hiss of its first few milliseconds: the burst of a b or a p as the lips part
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
        {
            // Its first five milliseconds: a short noisy burst there, louder than what it starts from, is a b or a p.
            qint64 const end = std::min(last, first + std::max<qint64>(8, rate / 200));
            int bursts = 0;
            double burstSum = 0.0, prior = 0.0;
            for (qint64 i = first; i < end; ++i) {
                double const v = (double(stereo[i * 2]) + double(stereo[i * 2 + 1])) / 65536.0;
                burstSum += v * v;
                if (i > first && ((v >= 0.0) != (prior >= 0.0)))
                    ++bursts;
                prior = v;
            }
            double const burstLoud = std::sqrt(burstSum / double(end - first));
            heard[f].burst = burstLoud > 0.004 && end - first > 1 ? double(bursts) / double(end - first - 1) : 0.0;
        }
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
        else {
            // A voiced sound: which vowel, by the voice's resonances; failing that, by how loud and how low it is.
            qint64 const from = qint64(f * perFrame), to = std::min(total, qint64((f + 1) * perFrame));
            QVector<double> voice;
            voice.reserve(to - from);
            for (qint64 i = from; i < to; ++i)
                voice.push_back((double(stereo[i * 2]) + double(stereo[i * 2 + 1])) / 65536.0);
            double first = 0.0, second = 0.0;
            if (formants(voice, rate, first, second))
                shape = vowelShape(first, second);
            else if (h.low > 0.7)
                shape = QLatin1Char(level > 0.55 ? 'E' : 'F');      // a hum low down: the rounded vowels
            else if (level > 0.65)
                shape = QLatin1Char('D');
            else if (level > 0.4)
                shape = QLatin1Char('C');
            else
                shape = QLatin1Char('B');
        }
        shapes[f] = shape;
    }
    // A shape held for a single frame between two frames of another is a flicker: it goes - except a frame much quieter
    // than the voice either side of it that is not quite silent (the hum through the nose of an m, the breath held for a
    // b or a p), which is the lips closing between two sounds: closed.  (A frame of dead silence is a gap in the sound.)
    QString const raw = shapes;
    auto voiced = [](QChar c) { return c != QLatin1Char('X') && c != QLatin1Char('A') && c != QLatin1Char('G') && c != QLatin1Char('B'); };
    for (int f = 1; f + 1 < frames; ++f) {
        bool const dip = voiced(raw[f - 1]) && voiced(raw[f + 1]) && heard[f].loud >= 0.004
                         && heard[f].loud < 0.5 * std::min(heard[f - 1].loud, heard[f + 1].loud);
        if (dip)
            shapes[f] = QLatin1Char('A');
        else if (raw[f - 1] == raw[f + 1] && raw[f] != raw[f - 1])
            shapes[f] = raw[f - 1];
    }
    // A voiced sound that starts with a burst, after a frame of quiet: a b or a p - the lips were closed on the frame before.
    for (int f = 1; f < frames; ++f) {
        bool const quietBefore = heard[f - 1].loud < 0.25 * heard[f].loud;
        if (voiced(shapes[f]) && heard[f].burst > 0.3 && quietBefore && (shapes[f - 1] == QLatin1Char('X') || shapes[f - 1] == QLatin1Char('A')))
            shapes[f - 1] = QLatin1Char('A');
    }
    return shapes;
}

QString lead(QString const& shapes, int frames)
{
    frames = std::clamp(frames, 0, int(shapes.size()));
    return shapes.mid(frames) + QString(frames, QLatin1Char('X'));
}

}
