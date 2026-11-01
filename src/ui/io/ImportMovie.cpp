// Eclipse - bringing in a movie: its frames as pictures, its sound as a sound layer.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QProgressDialog>
#include "ui/io/RawMediaImport.h"

#include "audio/sound/Sound.h"
#include "model/media/MediaFormats.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"

int MainWindow::importMovie(QString const& path, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return -1;
    };
    QString const ffmpeg = Sound::findFfmpeg();
    if (ffmpeg.isEmpty())
        return fail(tr("The bundled movie tools are missing. Run scripts/install_thirdparty.bat and try again."));
    if (m_path.isEmpty())
        return fail(tr("Save the scene first: the frames of the movie are kept in a folder beside it."));
    if (!QFileInfo::exists(path))
        return fail(tr("There is no file %1.").arg(QDir::toNativeSeparators(path)));
    bool const raw=RawVideo::needsSettings(path);
    RawVideo::Options rawOptions;
    double const targetRate=m_document->scene().fps;
    QStringList input{QStringLiteral("-i"),path};
    if(raw) {
        if(!RawMediaImport::video(this,path,targetRate,rawOptions,error)) return -1;
        input=RawVideo::inputArguments(path,rawOptions,error); if(input.isEmpty()) return -1;
    }
    auto const* pixels=raw?RawVideo::format(rawOptions.format):nullptr;
    QString const extension=pixels && pixels->floating?QStringLiteral("exr"):QStringLiteral("png");

    // Each import owns a new folder under the scene's media folder.
    QFileInfo const sceneFile(m_path), movie(path);
    QDir media(sceneFile.dir().filePath(sceneFile.completeBaseName() + QStringLiteral("-media")));
    if (!media.mkpath(QStringLiteral(".")))
        return fail(tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(media.path())));
    QTemporaryDir imported(media.filePath(movie.completeBaseName() + QStringLiteral("_XXXXXX")));
    if (!imported.isValid())
        return fail(tr("Could not create a folder for the imported movie."));
    QDir folder(imported.path());
    QProgressDialog progress(tr("Importing movie frames..."),tr("Cancel"),0,0,this); progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(500);
    bool cancelled=false;
    auto run = [&](QStringList const& args, QString* said) {
        QProcess process;
        process.start(ffmpeg, args);
        if (!process.waitForStarted(10000))
            return false;
        // A long movie takes a while; the window stays alive meanwhile.
        while (process.state()!=QProcess::NotRunning) {
            process.waitForFinished(100);
            QApplication::processEvents();
            if(progress.wasCanceled()) { cancelled=true; process.kill(); process.waitForFinished(1000); return false; }
        }
        if (said)
            *said = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QString said;
    QStringList decode{QStringLiteral("-y"),QStringLiteral("-v"),QStringLiteral("error")}; decode+=input;
    decode << QStringLiteral("-map") << QStringLiteral("0:v:0") << QStringLiteral("-r") << QString::number(targetRate,'g',12);
    if(pixels && pixels->floating) decode << QStringLiteral("-vf") << QStringLiteral("premultiply=inplace=1") << QStringLiteral("-c:v") << QStringLiteral("exr") << QStringLiteral("-pix_fmt") << QStringLiteral("gbrapf32le") << QStringLiteral("-format") << QStringLiteral("float");
    else if(pixels && pixels->bytes>1) decode << QStringLiteral("-pix_fmt") << QStringLiteral("rgba64be");
    decode << folder.filePath(QStringLiteral("frame_%05d.")+extension);
    if (!run(decode,&said)) {
        if(cancelled) { if(error) error->clear(); return -1; }
        return fail(tr("ffmpeg could not read the movie.\n%1").arg(said));
    }
    QStringList frames = folder.entryList({QStringLiteral("frame_*.")+extension}, QDir::Files, QDir::Name);
    if (frames.isEmpty())
        return fail(tr("The movie has no picture."));
    // Its sound, when it has any.
    QString const wav = folder.filePath(QStringLiteral("sound.wav"));
    bool const hasSound = !raw && run({QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-vn"),
                               QStringLiteral("-map"),QStringLiteral("0:a:0"),QStringLiteral("-c:a"),QStringLiteral("pcm_f64le"),wav},
                              nullptr)
                          && QFileInfo(wav).size() > 44;
    if(cancelled) { if(error) error->clear(); return -1; }
    progress.close();

    ImageImport options;
    for (QString const& frame : std::as_const(frames))
        options.files << folder.filePath(frame);
    options.layerName = movie.completeBaseName();
    options.linked = true;
    m_document->beginMacro(tr("Import Movie"));
    int const at = importImages(options, error);
    if (at >= 0 && hasSound)
        m_document->addSound(wav, 0);
    m_document->endMacro();
    if (at >= 0)
        imported.setAutoRemove(false);
    if (at >= 0)
        m_timeline->setCurrentCell(m_document->scene().layers.size() > at ? at : 0, 0);
    return at;
}

void MainWindow::onActionImportMovie()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import Movie"), m_path.isEmpty() ? QString() : QFileInfo(m_path).path(),
                                                      MediaFormats::videoFilter(), nullptr, QFileDialog::HideNameFilterDetails);
    if (path.isEmpty())
        return;
    stop();
    QString error;
    int const at = importMovie(path, &error);
    if (at < 0 && !error.isEmpty())
        QMessageBox::critical(this, tr("Eclipse"), error);
    else if(at>=0)
        statusBar()->showMessage(tr("Imported %1").arg(QDir::toNativeSeparators(path)), 6000);
}
