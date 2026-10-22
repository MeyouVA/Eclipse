// Eclipse - the Flash movie writer: the scene's sound, streamed with the frames (MP3 when ffmpeg is there, else as it is).
#include "formats/flash/SwfExportPrivate.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

#include "audio/sound/Sound.h"

namespace swfout {

namespace {

// The scene's sound as MP3 frames (each 1152 samples a channel), by ffmpeg; empty when it cannot be.
QVector<QByteArray> mp3Frames(QVector<qint16> const& stereo)
{
    QString const ffmpeg = Sound::findFfmpeg();
    QTemporaryDir dir;
    if (ffmpeg.isEmpty() || !dir.isValid())
        return {};
    QString const wav = dir.filePath(QStringLiteral("in.wav")), mp3 = dir.filePath(QStringLiteral("out.mp3"));
    if (!Sound::writeWav(wav, stereo, nullptr))
        return {};
    QProcess run;
    run.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), wav, QStringLiteral("-ar"),
                       QStringLiteral("44100"), QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-b:a"), QStringLiteral("128k"),
                       QStringLiteral("-write_xing"), QStringLiteral("0"), QStringLiteral("-id3v2_version"), QStringLiteral("0"), mp3});
    if (!run.waitForFinished(600000) || run.exitCode() != 0)
        return {};
    QFile file(mp3);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray const bytes = file.readAll();
    // Frame by frame, by their headers (MPEG-1 layer III at 44100 Hz).
    static int const rates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    QVector<QByteArray> frames;
    int p = 0;
    while (p + 4 <= bytes.size()) {
        quint32 const head = (quint32(quint8(bytes[p])) << 24) | (quint32(quint8(bytes[p + 1])) << 16) | (quint32(quint8(bytes[p + 2])) << 8) | quint8(bytes[p + 3]);
        bool const sync = (head & 0xffe00000u) == 0xffe00000u && ((head >> 19) & 3) == 3 && ((head >> 17) & 3) == 1;
        int const rate = rates[(head >> 12) & 15];
        if (!sync || rate == 0 || ((head >> 10) & 3) != 0) {
            ++p;
            continue;
        }
        int const length = 144 * rate * 1000 / 44100 + int((head >> 9) & 1);
        if (p + length > bytes.size())
            break;
        frames.push_back(bytes.mid(p, length));
        p += length;
    }
    return frames;
}

}  // namespace

bool soundStream(Scene const& scene, int first, int last, double fps, SoundStream& out, QString* problem)
{
    out = SoundStream();
    QVector<qint16> const all = Sound::mix(scene);
    double const perFrame = SoundData::kRate / fps;
    qint64 const from = std::llround(first * perFrame), to = std::llround((last + 1) * perFrame);
    QVector<qint16> part;
    for (qint64 s = from; s < to; ++s) {
        part.push_back(2 * s + 1 < all.size() ? all[2 * s] : 0);
        part.push_back(2 * s + 1 < all.size() ? all[2 * s + 1] : 0);
    }
    if (std::all_of(part.cbegin(), part.cend(), [](qint16 v) { return v == 0; }))
        return false;
    int const frames = last - first + 1;
    QVector<QByteArray> const mp3 = mp3Frames(part);
    // The head: played at 44 kHz, 16 bits, in stereo; streamed as MP3 or as it is (little-endian), so many samples a frame.
    u8(out.head, 0x0f);
    u8(out.head, (mp3.isEmpty() ? 3 : 2) << 4 | 0x0f);
    u16(out.head, quint16(std::clamp(int(std::lround(perFrame)), 1, 65535)));
    if (!mp3.isEmpty())
        u16(out.head, 0);
    out.blocks.resize(frames);
    if (mp3.isEmpty()) {
        if (problem)
            *problem = QObject::tr("ffmpeg was not found: the sound is kept as it is, without being made smaller");
        for (int f = 0; f < frames; ++f) {
            qint64 const a = std::llround(f * perFrame), b = std::llround((f + 1) * perFrame);
            QByteArray block;
            for (qint64 s = a; s < b && 2 * s + 1 < part.size(); ++s) {
                u16(block, quint16(part[2 * s]));
                u16(block, quint16(part[2 * s + 1]));
            }
            out.blocks[f] = block;
        }
        return true;
    }
    // Each frame its MP3 frames: those that start before it ends; how many samples they hold, and those to skip.
    int next = 0;
    for (int f = 0; f < frames && next < mp3.size(); ++f) {
        qint64 const start = std::llround(f * perFrame), end = std::llround((f + 1) * perFrame);
        int const firstHere = next;
        while (next < mp3.size() && qint64(next) * 1152 < end)
            ++next;
        if (next == firstHere)
            continue;
        QByteArray block;
        u16(block, quint16(std::min(65535, (next - firstHere) * 1152)));
        u16(block, quint16(qint16(std::clamp<qint64>(start - qint64(firstHere) * 1152, -32768, 32767))));
        for (int i = firstHere; i < next; ++i)
            block += mp3[i];
        out.blocks[f] = block;
    }
    return true;
}

}  // namespace swfout
