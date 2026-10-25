#include "audio/sound/Sound.h"

#include <QDataStream>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>
#include <functional>

#include "model/scene/Scene.h"

float SoundData::peak(qint64 first, qint64 count) const
{
    qint64 const begin = std::max<qint64>(0, first) * 2;
    qint64 const end = std::min<qint64>(samples.size(), (first + count) * 2);
    int loudest = 0;
    for (qint64 i = begin; i < end; ++i)
        loudest = std::max(loudest, std::abs(int(samples[i])));
    return float(loudest) / 32768.0f;
}

namespace Sound {

QString findFfmpeg()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

// ---- reading ------------------------------------------------------------------------------

static bool decodeWithFfmpeg(QString const& ffmpeg, QString const& path, QVector<qint16>& out, QString* error)
{
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-vn"),
                           QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-acodec"), QStringLiteral("pcm_s16le"),
                           QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-ar"), QString::number(SoundData::kRate),
                           QStringLiteral("-")});
    if (!process.waitForStarted(10000) || !process.waitForFinished(300000) || process.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error && error->isEmpty())
            *error = QStringLiteral("ffmpeg could not read the file.");
        process.kill();
        return false;
    }
    QByteArray const bytes = process.readAllStandardOutput();
    out.resize(bytes.size() / 2 / 2 * 2);  // whole stereo frames only
    memcpy(out.data(), bytes.constData(), size_t(out.size()) * 2);
    return true;
}

// Plain PCM WAV (8, 16, 24 or 32 bit integers), any rate, any number of channels.
static bool readWav(QString const& path, QVector<qint16>& out, QString* error)
{
    auto fail = [&](char const* why) {
        if (error)
            *error = QString::fromLatin1(why);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail("The file cannot be opened.");
    QByteArray const data = file.readAll();
    if (data.size() < 12 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE")
        return fail("Not a WAV file. Install ffmpeg to use other kinds of sound file.");
    auto u16 = [&](qsizetype at) { return quint32(quint8(data[at])) | quint32(quint8(data[at + 1])) << 8; };
    auto u32 = [&](qsizetype at) { return u16(at) | u16(at + 2) << 16; };

    int channels = 0, rate = 0, bits = 0, format = 0;
    QByteArray pcm;
    for (qsizetype at = 12; at + 8 <= data.size();) {
        QByteArray const id = data.mid(at, 4);
        qsizetype const size = qsizetype(u32(at + 4));
        if (id == "fmt " && at + 8 + 16 <= data.size()) {
            format = int(u16(at + 8));
            channels = int(u16(at + 10));
            rate = int(u32(at + 12));
            bits = int(u16(at + 22));
        } else if (id == "data") {
            pcm = data.mid(at + 8, size);
        }
        at += 8 + size + (size & 1);
    }
    // 0xFFFE is the "extensible" header, which for our purposes holds the same integers.
    if ((format != 1 && format != 0xFFFE) || channels < 1 || rate < 1000 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) || pcm.isEmpty())
        return fail("This kind of WAV file is not supported. Install ffmpeg to use it.");

    int const bytesPer = bits / 8;
    qint64 const frames = pcm.size() / (bytesPer * channels);
    auto sample = [&](qint64 frame, int channel) -> double {
        char const* const p = pcm.constData() + (frame * channels + std::min(channel, channels - 1)) * bytesPer;
        switch (bytesPer) {
        case 1:
            return (int(quint8(p[0])) - 128) / 128.0;
        case 2:
            return qint16(quint8(p[0]) | quint8(p[1]) << 8) / 32768.0;
        case 3:
            return (qint32(quint8(p[0]) << 8 | quint8(p[1]) << 16 | quint32(quint8(p[2])) << 24) >> 8) / 8388608.0;
        default:
            return qint32(quint8(p[0]) | quint8(p[1]) << 8 | quint8(p[2]) << 16 | quint32(quint8(p[3])) << 24) / 2147483648.0;
        }
    };
    // To our rate, taking each new sample between the two old ones around it.
    qint64 const outFrames = frames * SoundData::kRate / rate;
    out.resize(outFrames * 2);
    for (qint64 i = 0; i < outFrames; ++i) {
        double const at = double(i) * rate / SoundData::kRate;
        qint64 const a = std::min<qint64>(frames - 1, qint64(at));
        qint64 const b = std::min<qint64>(frames - 1, a + 1);
        double const mixAmount = at - double(a);
        for (int c = 0; c < 2; ++c) {
            double const v = sample(a, c) * (1.0 - mixAmount) + sample(b, c) * mixAmount;
            out[i * 2 + c] = qint16(std::clamp(v, -1.0, 1.0) * 32767.0);
        }
    }
    return true;
}

std::shared_ptr<SoundData> load(QString const& path, QString* error)
{
    struct Cached { QByteArray hash; std::shared_ptr<SoundData> data; };
    static QHash<QString, Cached> cache;
    QString const key = QFileInfo(path).absoluteFilePath();
    QFile source(key);
    QCryptographicHash content(QCryptographicHash::Sha256);
    bool const readable = source.open(QIODevice::ReadOnly) && content.addData(&source);
    QByteArray const hash = readable ? content.result() : QByteArray();
    source.close();
    if (auto const found = cache.constFind(key); readable && found != cache.constEnd() && found->hash == hash)
        return found->data;
    if (!QFileInfo::exists(key)) {
        if (error)
            *error = QStringLiteral("The sound file is missing: %1").arg(path);
        return nullptr;
    }
    auto data = std::make_shared<SoundData>();
    QString const ffmpeg = findFfmpeg();
    QString why;
    bool ok = !ffmpeg.isEmpty() && decodeWithFfmpeg(ffmpeg, key, data->samples, &why);
    if (!ok)
        ok = readWav(key, data->samples, ffmpeg.isEmpty() ? &why : nullptr);
    if (!ok || data->samples.isEmpty()) {
        if (error)
            *error = why.isEmpty() ? QStringLiteral("The file holds no sound.") : why;
        return nullptr;
    }
    cache.insert(key, Cached{hash, data});
    return data;
}

// ---- mixing and writing -------------------------------------------------------------------

QVector<qint16> mix(Scene const& scene)
{
    qint64 const total = qint64(std::ceil(scene.frameCount() / scene.fps * SoundData::kRate));
    QVector<qint32> sum(total * 2, 0);
    bool any = false;
    // Where in the sound a frame of the scene starts.
    auto at = [&scene](double frame) { return qint64(std::llround(frame / scene.fps * SoundData::kRate)); };
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        if (layer.kind != Layer::Sound || !scene.enabled(l))
            continue;
        std::shared_ptr<SoundData> const data = load(layer.soundFile);
        if (!data)
            continue;
        if (!layer.soundFrames.isEmpty()) {
            for (int f = 0; f < scene.frameCount(); ++f) {
                int const frame = layer.soundFrameAt(f);
                if (frame < 0)
                    continue;
                qint64 const to = at(f), length = at(f + 1) - to, from = at(frame);
                for (qint64 i = 0; i < length && to + i < total && from + i < data->frames(); ++i) {
                    any = true;
                    sum[(to + i) * 2] += data->samples[(from + i) * 2];
                    sum[(to + i) * 2 + 1] += data->samples[(from + i) * 2 + 1];
                }
            }
            continue;
        }
        any = true;
        qint64 const offset = at(layer.soundStart);
        qint64 const first = std::max<qint64>(0, -offset);
        qint64 const count = std::min<qint64>(data->frames(), total - offset);
        for (qint64 i = first; i < count; ++i) {
            sum[(offset + i) * 2] += data->samples[i * 2];
            sum[(offset + i) * 2 + 1] += data->samples[i * 2 + 1];
        }
    }
    std::function<void(QVector<Layer> const&, QVector<int> const&, int)> inside = [&](QVector<Layer> const& layers, QVector<int> const& shown, int depth) {
        if (depth > Scene::kSymbolDepth)
            return;
        Scene nestedScene = scene;
        nestedScene.layers = layers;
        for (int l = 0; l < layers.size(); ++l) {
            Layer const& layer = layers[l];
            if (!nestedScene.enabled(l))
                continue;
            if (layer.kind == Layer::Sound && depth > 0) {
                std::shared_ptr<SoundData> const data = load(layer.soundFile);
                if (!data)
                    continue;
                for (int f = 0; f < shown.size(); ++f) {
                    if (shown[f] < 0)
                        continue;
                    qint64 const to = at(f), length = at(f + 1) - to;
                    int const soundFrame = layer.soundFrameAt(shown[f]);
                    if (soundFrame < 0)
                        continue;
                    qint64 const from = at(soundFrame);
                    for (qint64 i = 0; i < length && to + i < total; ++i) {
                        qint64 const s = from + i;
                        if (s < 0 || s >= data->frames())
                            continue;
                        any = true;
                        sum[(to + i) * 2] += data->samples[s * 2];
                        sum[(to + i) * 2 + 1] += data->samples[s * 2 + 1];
                    }
                }
            } else if (layer.showsSymbol()) {
                Scene::Symbol const* const symbol = scene.findSymbol(layer.symbolId);
                if (!symbol)
                    continue;
                QVector<int> inner(shown.size(), -1);
                for (int f = 0; f < shown.size(); ++f)
                    if (shown[f] >= 0) {
                        int const frame = layer.drawingAt(shown[f]);
                        inner[f] = frame == Layer::kNoDrawing || frame >= symbol->frameCount ? -1 : frame;
                    }
                inside(symbol->layers, inner, depth + 1);
            }
        }
    };
    {
        QVector<int> every(scene.frameCount());
        for (int f = 0; f < every.size(); ++f)
            every[f] = f;
        inside(scene.layers, every, 0);
    }
    QVector<qint16> out;
    if (!any)
        return out;
    out.resize(sum.size());
    for (qsizetype i = 0; i < sum.size(); ++i)
        out[i] = qint16(std::clamp(sum[i], -32768, 32767));
    return out;
}

bool writeWav(QString const& path, QVector<qint16> const& stereo, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    quint32 const dataBytes = quint32(stereo.size() * 2);
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("RIFF", 4);
    s << quint32(36 + dataBytes);
    s.writeRawData("WAVEfmt ", 8);
    s << quint32(16) << quint16(1) << quint16(2) << quint32(SoundData::kRate) << quint32(SoundData::kRate * 4) << quint16(4) << quint16(16);
    s.writeRawData("data", 4);
    s << dataBytes;
    s.writeRawData(reinterpret_cast<char const*>(stereo.constData()), int(dataBytes));
    if (s.status() != QDataStream::Ok) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

}
