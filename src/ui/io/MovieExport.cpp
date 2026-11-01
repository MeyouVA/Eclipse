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
#include "model/media/MediaFormats.h"
#include "model/scene/Scene.h"
#include "model/scene/AtomicFile.h"
#include "canvas/Canvas.h"
#include "canvas/FrameRender.h"

namespace MovieExport {

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
                                   "Install the bundled media tools using scripts/install_thirdparty.bat or export the frames as images instead."));

    // The sound goes to ffmpeg as a file beside the frames, which go down a pipe.
    QTemporaryDir scratch;
    QString const suffix = QFileInfo(path).suffix().toLower();
    auto const* format=MediaFormats::video(suffix);
    if(!format) return fail(QStringLiteral("Unsupported movie container: %1").arg(suffix));
    int const codecId=MediaFormats::resolvedCodec(*format,options.codec,options.transparentBackground);
    auto const* codec=MediaFormats::videoCodec(codecId);
    if(!codec) return fail(QStringLiteral("The selected codec or transparency is not supported by this container."));
    bool const alpha=format->alphaCodecs.contains(codecId);
    Scene rendered = scene;
    if (options.transparentBackground)
        rendered.background = Qt::transparent;
    else if (!alpha && suffix != QLatin1String("gif"))
        rendered.background.setAlpha(255);
    QString const generated = scratch.filePath(QStringLiteral("output.") + suffix);
    if (!scratch.isValid())
        return fail(QStringLiteral("No temporary folder for the export."));
    bool const gif = suffix == QLatin1String("gif");
    int const first = std::clamp(options.first, 0, scene.frameCount() - 1);
    int const last = options.last < 0 ? scene.frameCount() - 1 : std::clamp(options.last, first, scene.frameCount() - 1);
    double const scale = std::clamp(options.scale, 0.05, 8.0);
    int const wide = std::max(2, int(std::lround(scene.width * scale))), high = std::max(2, int(std::lround(scene.height * scale)));
    if(codecId==MediaFormats::DnxHD && (wide!=1920 || high!=1080)) return fail(QStringLiteral("DNxHD requires 1920 by 1080 frames. Use DNxHR for other dimensions."));
    QVector<qint16> const sound = gif ? QVector<qint16>() : Sound::mix(scene);
    QString const wav = scratch.filePath(QStringLiteral("sound.wav"));
    if (!sound.isEmpty() && (!scratch.isValid() || !Sound::writeWav(wav, sound, error)))
        return false;

    QStringList args{QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), codec->deep?QStringLiteral("rgba64le"):QStringLiteral("rgba"),
                     QStringLiteral("-s"), QStringLiteral("%1x%2").arg(wide).arg(high),
                     QStringLiteral("-r"), QString::number(scene.fps, 'f', 3), QStringLiteral("-i"), QStringLiteral("-")};
    // Of the sound, the part those frames last.
    if (!sound.isEmpty())
        args << QStringLiteral("-ss") << QString::number(first / scene.fps, 'f', 4) << QStringLiteral("-t")
             << QString::number((last - first + 1) / scene.fps, 'f', 4) << QStringLiteral("-i") << wav;
    args << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if(!sound.isEmpty()) args << QStringLiteral("-map") << QStringLiteral("1:a:0");
    MovieExport::Options encoding=options; encoding.frameRate=scene.fps;
    args << encoderArguments(suffix, encoding, !sound.isEmpty());
    args << QStringLiteral("-t") << QString::number((last-first+1)/scene.fps,'f',6);
    args << QDir::toNativeSeparators(generated);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(ffmpeg, args);
    if (!process.waitForStarted(10000))
        return fail(QStringLiteral("ffmpeg could not be started."));

    auto abandon = [&](QString const& why) {
        process.kill();
        process.waitForFinished(5000);
        return fail(why);
    };
    // A few frames at a time rendered on the cores, then sent down the pipe in order.
    int const chunk = std::max(1, FrameRender::threadsUsed() * 2);
    for (int at = first; at <= last; at += chunk) {
        QVector<int> frames;
        for (int f = at; f <= std::min(last, at + chunk - 1); ++f)
            frames.push_back(f);
        QVector<QImage> batch(frames.size());
        QImage* const held = batch.data();
        QString const space = options.colourSpace;
        FrameRender::render(rendered, frames, QSize(wide, high), false, [held, at, &rendered, space, codec](int f, QImage const& picture) {
            held[f - at] = FrameRender::inSpace(rendered, picture, space, codec->deep);
            return QString();
        }, nullptr);
        for (int i = 0; i < batch.size(); ++i) {
            // (Told of each frame as it goes down the pipe, in order.)
            if (progress && !progress(at + i))
                return abandon(QStringLiteral("Stopped."));
            if (options.timecode) {
                // Hours, minutes, seconds and frames, on a dark band at the bottom left.
                int const frameNow = frames[i], perSecond = std::max(1, int(std::lround(scene.fps)));
                QString const code = QStringLiteral("%1:%2:%3:%4").arg(frameNow / perSecond / 3600, 2, 10, QLatin1Char('0')).arg(frameNow / perSecond / 60 % 60, 2, 10, QLatin1Char('0'))
                                         .arg(frameNow / perSecond % 60, 2, 10, QLatin1Char('0')).arg(frameNow % perSecond, 2, 10, QLatin1Char('0'));
                QPainter p(&batch[i]);
                QFont font(options.timecodeFont.isEmpty() ? QStringLiteral("Arial") : options.timecodeFont);
                font.setPixelSize(std::max(8, batch[i].height() * std::clamp(options.timecodeHeight, 1, 50) / 100));
                p.setFont(font);
                QRect band = p.fontMetrics().boundingRect(code).adjusted(-6, -3, 6, 3);
                int const place = std::clamp(options.timecodePosition, 0, 5), margin = 12;
                int const left = place % 3 == 0 ? margin : place % 3 == 1 ? (batch[i].width() - band.width()) / 2 : batch[i].width() - band.width() - margin;
                int const top = place < 3 ? margin : batch[i].height() - band.height() - margin;
                band.moveTopLeft(QPoint(left, top));
                p.fillRect(band, options.timecodeBackground);
                p.setPen(options.timecodeColour);
                p.drawText(band, Qt::AlignCenter, code);
            }
            QImage const image = batch[i].convertToFormat(codec->deep?QImage::Format_RGBA64:QImage::Format_RGBA8888);
            // The lines of an image may be padded: send them one by one.
            for (int y = 0; y < image.height(); ++y)
                if(process.write(reinterpret_cast<char const*>(image.constScanLine(y)),qint64(image.width())*(codec->deep?8:4))<0) return abandon(QStringLiteral("Cannot send a movie frame to the encoder."));
            while (process.bytesToWrite() > 0) {
                if (process.state() != QProcess::Running || !process.waitForBytesWritten(30000))
                    return abandon(QStringLiteral("ffmpeg stopped: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
            }
        }
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(600000) || process.exitCode() != 0)
        return abandon(QStringLiteral("ffmpeg failed: %1").arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed()));
    return AtomicFile::replaceFrom(generated, path, error);
}

}
