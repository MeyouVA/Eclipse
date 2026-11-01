#include "audio/sound/Sound.h"
#include "platform/ThirdParty.h"

#include <QDataStream>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
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

bool SoundData::hasSamples(qint64 first,qint64 end) const
{
    first=std::max<qint64>(0,first); end=std::min(end,frames());
    if(first>=end) return false;
    if(ranges.isEmpty()) return true;
    return std::any_of(ranges.cbegin(),ranges.cend(),[&](auto const& span) { return first<span.second && end>span.first; });
}

namespace Sound {

QString findFfmpeg()
{
    return ThirdParty::executable(QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg"));
}

// ---- reading ------------------------------------------------------------------------------

static bool decodeWithFfmpeg(QString const& ffmpeg, QString const& path, QVector<double>& out, int rate, QString* error)
{
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-vn"),
                           QStringLiteral("-f"), QStringLiteral("f64le"), QStringLiteral("-acodec"), QStringLiteral("pcm_f64le"),
                           QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-ar"), QString::number(rate),
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
    out.resize(bytes.size() / 8 / 2 * 2);  // whole stereo frames only
    memcpy(out.data(), bytes.constData(), size_t(out.size()) * sizeof(double));
    return true;
}

// Plain PCM WAV (8, 16, 24 or 32 bit integers), any rate, any number of channels.
static bool readWav(QString const& path, QVector<double>& out, int outputRate, QString* error)
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
        return fail("Not a WAV file. Install the bundled media tools using scripts/install_thirdparty.bat.");
    auto u16 = [&](qsizetype at) { return quint32(quint8(data[at])) | quint32(quint8(data[at + 1])) << 8; };
    auto u32 = [&](qsizetype at) { return u16(at) | u16(at + 2) << 16; };

    int channels = 0, rate = 0, bits = 0, format = 0;
    QByteArray pcm;
    for (qsizetype at = 12; at + 8 <= data.size();) {
        QByteArray const id = data.mid(at, 4);
        qsizetype const size = qsizetype(u32(at + 4));
        if (size > data.size()-at-8) return fail("Truncated WAV data.");
        if (id == "fmt " && size >= 16) {
            format = int(u16(at + 8));
            channels = int(u16(at + 10));
            rate = int(u32(at + 12));
            bits = int(u16(at + 22));
            if(format==0xFFFE && size>=40) format=int(u16(at+32));
        } else if (id == "data") {
            pcm = data.mid(at + 8, size);
        }
        at += 8 + size + (size & 1);
    }
    if ((format != 1 && format != 3) || channels < 1 || rate < 1000 || (format==3 ? bits!=32 && bits!=64 : bits!=8 && bits!=16 && bits!=24 && bits!=32) || pcm.isEmpty())
        return fail("This kind of WAV file needs the bundled media tools. Run scripts/install_thirdparty.bat.");

    int const bytesPer = bits / 8;
    qint64 const frames = pcm.size() / (bytesPer * channels);
    if(!frames) return fail("The WAV contains no complete audio frames.");
    auto sample = [&](qint64 frame, int channel) -> double {
        char const* const p = pcm.constData() + (frame * channels + std::min(channel, channels - 1)) * bytesPer;
        if(format==3) {
            double value=0;
            if(bits==32) { float sample; memcpy(&sample,p,4); value=sample; }
            else memcpy(&value,p,8);
            return std::isfinite(value)?value:0.0;
        }
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
    qint64 const outFrames = frames * outputRate / rate;
    out.resize(outFrames * 2);
    for (qint64 i = 0; i < outFrames; ++i) {
        double const at = double(i) * rate / outputRate;
        qint64 const a = std::min<qint64>(frames - 1, qint64(at));
        qint64 const b = std::min<qint64>(frames - 1, a + 1);
        double const mixAmount = at - double(a);
        for (int c = 0; c < 2; ++c) {
            double const v = sample(a, c) * (1.0 - mixAmount) + sample(b, c) * mixAmount;
            out[i * 2 + c] = v;
        }
    }
    return true;
}

std::shared_ptr<SoundData> loadPrecise(QString const& path, int rate, QString* error)
{
    if(rate<8000 || rate>192000) { if(error) *error=QStringLiteral("Invalid audio sample rate."); return nullptr; }
    struct Cached { QByteArray hash; std::shared_ptr<SoundData> data; };
    static QHash<QString, Cached> cache;
    static QMutex mutex;
    QMutexLocker locked(&mutex);
    QString const filePath = QFileInfo(path).absoluteFilePath();
    QString const key = filePath + QLatin1Char('|') + QString::number(rate);
    QFile source(filePath);
    QCryptographicHash content(QCryptographicHash::Sha256);
    bool const readable = source.open(QIODevice::ReadOnly) && content.addData(&source);
    QByteArray const hash = readable ? content.result() : QByteArray();
    source.close();
    if (auto const found = cache.constFind(key); readable && found != cache.constEnd() && found->hash == hash)
        return found->data;
    if (!QFileInfo::exists(filePath)) {
        if (error)
            *error = QStringLiteral("The sound file is missing: %1").arg(path);
        return nullptr;
    }
    auto data = std::make_shared<SoundData>();
    data->contentHash=hash;
    QString const ffmpeg = findFfmpeg();
    QString why;
    bool ok = !ffmpeg.isEmpty() && decodeWithFfmpeg(ffmpeg, filePath, data->precise, rate, &why);
    if (!ok)
        ok = readWav(filePath, data->precise, rate, ffmpeg.isEmpty() ? &why : nullptr);
    if (!ok || data->precise.isEmpty()) {
        if (error)
            *error = why.isEmpty() ? QStringLiteral("The file holds no sound.") : why;
        return nullptr;
    }
    data->rate = rate;
    data->samples.resize(data->precise.size());
    for (qsizetype i = 0; i < data->precise.size(); ++i)
        data->samples[i] = qint16(std::clamp(data->precise[i], -1.0, 32767.0 / 32768.0) * 32768.0);
    if (cache.size() > 32) cache.clear();
    cache.insert(key, Cached{hash, data});
    return data;
}

std::shared_ptr<SoundData> load(QString const& path, QString* error)
{
    return loadPrecise(path, SoundData::kRate, error);
}

// ---- mixing and writing -------------------------------------------------------------------

QVector<double> mixPrecise(Scene const& scene,int firstFrame,int lastFrame,int rate)
{
    if(rate<8000 || rate>192000 || !std::isfinite(scene.fps) || scene.fps<=0) return {};
    qint64 const total = qint64(std::ceil(scene.frameCount() / scene.fps * rate));
    QVector<double> sum(total * 2, 0);
    bool any = false;
    // Where in the sound a frame of the scene starts.
    auto at = [&scene,rate](double frame) { return qint64(std::llround(frame / scene.fps * rate)); };
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        if (layer.kind != Layer::Sound || !scene.enabled(l))
            continue;
        std::shared_ptr<SoundData> const data = trackPrecise(layer, scene.fps, rate);
        if (!data)
            continue;
        if (!layer.soundFrames.isEmpty()) {
            for (int f = 0; f < scene.frameCount(); ++f) {
                int const frame = layer.soundFrameAt(f);
                if (frame < 0)
                    continue;
                qint64 const to = at(f), length = at(f + 1) - to, from = at(frame);
                if(f>=firstFrame && (lastFrame<0 || f<=lastFrame) && data->hasSamples(from,from+length)) any=true;
                for (qint64 i = 0; i < length && to + i < total && from + i < data->frames(); ++i) {
                    sum[(to + i) * 2] += data->precise[(from + i) * 2];
                    sum[(to + i) * 2 + 1] += data->precise[(from + i) * 2 + 1];
                }
            }
            continue;
        }
        qint64 const offset = at(layer.soundStart);
        qint64 const first = std::max<qint64>(0, -offset);
        qint64 const count = std::min<qint64>(data->frames(), total - offset);
        qint64 const rangeFirst=at(std::max(0,firstFrame)),rangeEnd=at(lastFrame<0?scene.frameCount():std::min(scene.frameCount(),lastFrame+1));
        if(data->hasSamples(std::max(first,rangeFirst-offset),std::min(count,rangeEnd-offset))) any=true;
        for (qint64 i = first; i < count; ++i) {
            sum[(offset + i) * 2] += data->precise[i * 2];
            sum[(offset + i) * 2 + 1] += data->precise[i * 2 + 1];
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
                std::shared_ptr<SoundData> const data = trackPrecise(layer, scene.fps, rate);
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
                    if(f>=firstFrame && (lastFrame<0 || f<=lastFrame) && data->hasSamples(from,from+length)) any=true;
                    for (qint64 i = 0; i < length && to + i < total; ++i) {
                        qint64 const s = from + i;
                        if (s < 0 || s >= data->frames())
                            continue;
                        sum[(to + i) * 2] += data->precise[s * 2];
                        sum[(to + i) * 2 + 1] += data->precise[s * 2 + 1];
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
    QVector<double> out;
    if (!any)
        return out;
    out.resize(sum.size());
    for (qsizetype i = 0; i < sum.size(); ++i)
        out[i] = sum[i];
    qint64 const begin=std::clamp<qint64>(at(std::max(0,firstFrame))*2,0,out.size());
    qint64 const end=lastFrame<0?out.size():std::clamp<qint64>(at(std::min(scene.frameCount(),lastFrame+1))*2,begin,out.size());
    return out.mid(begin,end-begin);
}

QVector<qint16> mix(Scene const& scene,int first,int last)
{
    QVector<double> const source = mixPrecise(scene, first, last, SoundData::kRate);
    QVector<qint16> result(source.size());
    for (qsizetype i = 0; i < source.size(); ++i)
        result[i] = qint16(std::clamp(source[i], -1.0, 32767.0 / 32768.0) * 32768.0);
    return result;
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
