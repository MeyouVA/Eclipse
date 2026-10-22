#include "ui/io/MovieExport.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>

#include "audio/sound/Sound.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"

namespace MovieExport {

QStringList encoderArguments(QString const& suffix, Options const& options, bool sound)
{
    QStringList args;
    QString const kind = suffix.toLower();
    int const quality = std::clamp(options.quality, 0, 51);
    QString const audio = QString::number(std::clamp(options.audioKbps, 32, 512)) + QLatin1Char('k');
    static char const* const kSpeeds[3] = {"veryfast", "medium", "slow"};
    QString const speed = QLatin1String(kSpeeds[std::clamp(options.speed, 0, 2)]);
    if (kind == QLatin1String("gif")) {
        args << QStringLiteral("-vf") << QStringLiteral("split[a][b];[a]palettegen[p];[b][p]paletteuse");
        return args;
    }
    if (kind == QLatin1String("webm")) {
        // VP9's scale of quality runs higher than H.264's: about twelve steps on for the same look.
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9") << QStringLiteral("-crf") << QString::number(std::clamp(quality + 12, 0, 63))
             << QStringLiteral("-b:v") << QStringLiteral("0") << QStringLiteral("-deadline") << (options.speed == 0 ? QStringLiteral("realtime") : QStringLiteral("good"))
             << QStringLiteral("-cpu-used") << QString::number(options.speed == 2 ? 1 : options.speed == 0 ? 5 : 2);
        if (sound)
            args << QStringLiteral("-c:a") << QStringLiteral("libopus") << QStringLiteral("-b:a") << audio;
        return args;
    }
    // The others want even sizes.
    args << QStringLiteral("-vf") << QStringLiteral("pad=ceil(iw/2)*2:ceil(ih/2)*2");
    if (options.codec == 2 && kind == QLatin1String("mov")) {
        // ProRes 422 HQ: for editing further; it keeps everything, and has no quality to choose.
        args << QStringLiteral("-c:v") << QStringLiteral("prores_ks") << QStringLiteral("-profile:v") << QStringLiteral("3")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv422p10le");
        if (sound)
            args << QStringLiteral("-c:a") << QStringLiteral("pcm_s16le");
        return args;
    }
    if (options.codec == 1) {
        // H.265: the same picture in about half the file; tagged so that players on every system take it.
        args << QStringLiteral("-c:v") << QStringLiteral("libx265") << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-crf") << QString::number(std::clamp(quality + 4, 0, 51)) << QStringLiteral("-preset") << speed
             << QStringLiteral("-tag:v") << QStringLiteral("hvc1");
    } else {
        // H.264 in this pixel layout plays everywhere.
        args << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-crf") << QString::number(quality) << QStringLiteral("-preset") << speed;
    }
    if (sound)
        args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a") << audio;
    return args;
}

bool write(Scene const& scene, QString const& path, std::function<bool(int frame)> const& progress, QString* error, Options const& options)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    QString const ffmpeg = Sound::findFfmpeg();
    if (ffmpeg.isEmpty())
        return fail(QStringLiteral("Movies are written by ffmpeg, which is not installed on this computer. "
                                   "Install it (ffmpeg.org) or export the frames as images instead."));

    // The sound goes to ffmpeg as a file beside the frames, which go down a pipe.
    QTemporaryDir scratch;
    QString const suffix = QFileInfo(path).suffix().toLower();
    bool const gif = suffix == QLatin1String("gif");
    int const first = std::clamp(options.first, 0, scene.frameCount() - 1);
    int const last = options.last < 0 ? scene.frameCount() - 1 : std::clamp(options.last, first, scene.frameCount() - 1);
    double const scale = std::clamp(options.scale, 0.05, 8.0);
    int const wide = std::max(2, int(std::lround(scene.width * scale))), high = std::max(2, int(std::lround(scene.height * scale)));
    QVector<qint16> const sound = gif ? QVector<qint16>() : Sound::mix(scene);
    QString const wav = scratch.filePath(QStringLiteral("sound.wav"));
    if (!sound.isEmpty() && (!scratch.isValid() || !Sound::writeWav(wav, sound, error)))
        return false;

    QStringList args{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
                     QStringLiteral("-s"), QStringLiteral("%1x%2").arg(wide).arg(high),
                     QStringLiteral("-r"), QString::number(scene.fps, 'f', 3), QStringLiteral("-i"), QStringLiteral("-")};
    // Of the sound, the part those frames last.
    if (!sound.isEmpty())
        args << QStringLiteral("-ss") << QString::number(first / scene.fps, 'f', 4) << QStringLiteral("-t")
             << QString::number((last - first + 1) / scene.fps, 'f', 4) << QStringLiteral("-i") << wav;
    args << encoderArguments(suffix, options, !sound.isEmpty());
    args << QDir::toNativeSeparators(path);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, args);
    if (!process.waitForStarted(10000))
        return fail(QStringLiteral("ffmpeg could not be started."));

    auto abandon = [&](QString const& why) {
        process.kill();
        process.waitForFinished(5000);
        QFile::remove(path);
        return fail(why);
    };
    QImage image(wide, high, QImage::Format_RGBA8888);
    for (int f = first; f <= last; ++f) {
        if (progress && !progress(f))
            return abandon(QStringLiteral("Stopped."));
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.scale(double(wide) / scene.width, double(high) / scene.height);
            Canvas::paintFrame(painter, scene, f);
        }
        // The lines of an image may be padded: send them one by one.
        for (int y = 0; y < image.height(); ++y)
            process.write(reinterpret_cast<char const*>(image.constScanLine(y)), qint64(image.width()) * 4);
        while (process.bytesToWrite() > 0) {
            if (process.state() != QProcess::Running || !process.waitForBytesWritten(30000))
                return abandon(QStringLiteral("ffmpeg stopped: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
        }
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(600000) || process.exitCode() != 0)
        return abandon(QStringLiteral("ffmpeg failed: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
    return true;
}

}
