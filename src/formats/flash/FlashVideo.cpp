// Eclipse - Flash video decoding.
#include "formats/flash/FlashVideo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>

#include "audio/sound/Sound.h"

namespace {

QString tr(char const* text)
{
    return QCoreApplication::translate("FlashVideo", text);
}

void put24(QByteArray& out, quint32 v)
{
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char(v & 0xff));
}

void put32(QByteArray& out, quint32 v)
{
    out.append(char(v >> 24));
    put24(out, v & 0xffffff);
}

quint32 u32(QByteArray const& b, qsizetype at)
{
    return at + 3 < b.size() ? quint32(quint8(b[at])) | (quint32(quint8(b[at + 1])) << 8) | (quint32(quint8(b[at + 2])) << 16) | (quint32(quint8(b[at + 3])) << 24)
                             : 0;
}

}  // namespace

namespace FlashVideo {

QByteArray flv(int codec, QVector<QByteArray> const& frames, double fps, QVector<bool> const& keyframes)
{
    QByteArray out("FLV\x01\x01", 5);      // version 1, video only
    put32(out, 9);
    put32(out, 0);
    double const step = 1000.0 / std::max(1.0, fps);
    for (int i = 0; i < frames.size(); ++i) {
        bool const key = keyframes.isEmpty() ? i == 0 : (i < keyframes.size() && keyframes[i]);
        QByteArray body;
        body.append(char(((key ? 1 : 2) << 4) | (codec & 0x0f)));
        // (VP6 in an FLV has a byte more: how much to crop the picture across and down - nothing here.)
        if (codec == VP6 || codec == VP6Alpha)
            body.append('\0');
        body += frames[i];
        quint32 const time = quint32(std::lround(i * step));
        out.append(char(9));
        put24(out, quint32(body.size()));
        put24(out, time & 0xffffff);
        out.append(char((time >> 24) & 0xff));
        put24(out, 0);
        out += body;
        put32(out, quint32(11 + body.size()));
    }
    return out;
}

QStringList decode(int codec, QVector<QByteArray> const& frames, double fps, QString const& folder, QString const& name, QVector<bool> const& keyframes,
                   QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return QStringList();
    };
    if (frames.isEmpty())
        return fail(tr("The video has no frames."));
    QTemporaryDir work;
    if (!work.isValid())
        return fail(tr("Could not make a folder to decode the video in."));
    QString const stream = work.filePath(QStringLiteral("video.flv"));
    {
        QFile file(stream);
        QByteArray const bytes = flv(codec, frames, fps, keyframes);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return fail(tr("Could not write the video out to decode it."));
    }
    QStringList made = decodeFile(stream, folder, name, codec == VP6Alpha, error);
    while (made.size() > frames.size())
        made.removeLast();
    return made;
}

QStringList decodeFile(QString const& input, QString const& folder, QString const& name, bool alpha, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return QStringList();
    };
    QString const ffmpeg = Sound::findFfmpeg();
    if (ffmpeg.isEmpty())
        return fail(tr("Video is decoded by ffmpeg, which is not installed on this computer. Install it (ffmpeg.org) to bring the video in."));
    QDir const out(folder);
    if (!out.mkpath(QStringLiteral(".")))
        return fail(tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(folder)));
    QTemporaryDir work;
    if (!work.isValid())
        return fail(tr("Could not make a folder to decode the video in."));
    QString const suffix = alpha ? QStringLiteral("png") : QStringLiteral("jpg");
    QStringList args{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-i"), input, QStringLiteral("-fps_mode"),
                     QStringLiteral("passthrough"), QStringLiteral("-start_number"), QStringLiteral("1")};
    if (alpha)
        args << QStringLiteral("-pix_fmt") << QStringLiteral("rgba");
    else
        args << QStringLiteral("-q:v") << QStringLiteral("2");
    args << work.filePath(QStringLiteral("frame %05d.") + suffix);
    QProcess process;
    process.start(ffmpeg, args);
    if (!process.waitForStarted(10000))
        return fail(tr("ffmpeg could not be started."));
    while (!process.waitForFinished(100))
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QStringList made;
    for (int i = 1;; ++i) {
        QString const from = work.filePath(QStringLiteral("frame %1.").arg(i, 5, 10, QLatin1Char('0')) + suffix);
        if (!QFileInfo::exists(from))
            break;
        QString const to = out.filePath(QStringLiteral("%1 %2.").arg(name).arg(i, 5, 10, QLatin1Char('0')) + suffix);
        QFile::remove(to);
        if (!QFile::rename(from, to) && !QFile::copy(from, to))
            break;
        made << to;
    }
    if (made.isEmpty())
        return fail(tr("ffmpeg could not decode the video: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
    return made;
}

QByteArray flaMovie(QByteArray const& data)
{
    // (The MP4 starts with its 'ftyp' box: four bytes of length, then the name.)
    if (data.size() < 16 || quint8(data[0]) != 0x03 || quint8(data[1]) != 0x08)
        return QByteArray();
    qsizetype const at = data.indexOf("ftyp");
    return at >= 4 && at < 64 ? data.mid(at - 4) : QByteArray();
}

bool flaFrames(QByteArray const& data, int& codec, QVector<QByteArray>& frames, QVector<bool>& keyframes)
{
    // 03, the codec; ... ; from byte 76, 17 bytes a frame: where it starts (from the first), how long it is, 1, 5 for a
    // frame that stands on its own or 4 for one that does not, and 1 (0 on the last).  Four bytes on, the frames (a byte
    // later for VP6).
    frames.clear();
    keyframes.clear();
    if (data.size() < 100 || quint8(data[0]) != 0x03)
        return false;
    codec = quint8(data[1]);
    if (codec != SorensonH263 && codec != VP6 && codec != VP6Alpha && codec != ScreenVideo && codec != ScreenVideo2)
        return false;
    struct Entry {
        quint32 offset, size;
        bool key;
    };
    QVector<Entry> entries;
    qsizetype p = 76;
    while (p + 17 <= data.size()) {
        quint32 const offset = u32(data, p), size = u32(data, p + 4), one = u32(data, p + 8), kind = u32(data, p + 12);
        int const more = quint8(data[p + 16]);
        if (one != 1 || (kind != 4 && kind != 5) || more > 1)
            break;
        entries.push_back({offset, size, kind == 5});
        p += 17;
        if (more == 0)
            break;
    }
    if (entries.isEmpty())
        return false;
    qsizetype const start = p + 4 + ((codec == VP6 || codec == VP6Alpha) ? 1 : 0);
    for (Entry const& e : std::as_const(entries)) {
        if (start + qsizetype(e.offset) + qsizetype(e.size) > data.size())
            break;
        frames.push_back(data.mid(start + e.offset, e.size));
        keyframes.push_back(e.key);
    }
    return !frames.isEmpty();
}

}
