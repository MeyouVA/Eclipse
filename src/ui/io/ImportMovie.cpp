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

#include "audio/sound/Sound.h"
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

    // Each import owns a new folder under the scene's media folder.
    QFileInfo const sceneFile(m_path), movie(path);
    QDir media(sceneFile.dir().filePath(sceneFile.completeBaseName() + QStringLiteral("-media")));
    if (!media.mkpath(QStringLiteral(".")))
        return fail(tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(media.path())));
    QTemporaryDir imported(media.filePath(movie.completeBaseName() + QStringLiteral("_XXXXXX")));
    if (!imported.isValid())
        return fail(tr("Could not create a folder for the imported movie."));
    QDir folder(imported.path());
    auto run = [&](QStringList const& args, QString* said) {
        QProcess process;
        process.start(ffmpeg, args);
        if (!process.waitForStarted(10000))
            return false;
        // A long movie takes a while; the window stays alive meanwhile.
        while (!process.waitForFinished(100))
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (said)
            *said = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QString said;
    if (!run({QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-r"),
              QString::number(m_document->scene().fps, 'f', 3), folder.filePath(QStringLiteral("frame_%05d.png"))},
             &said))
        return fail(tr("ffmpeg could not read the movie.\n%1").arg(said));
    QStringList frames = folder.entryList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name);
    if (frames.isEmpty())
        return fail(tr("The movie has no picture."));
    // Its sound, when it has any.
    QString const wav = folder.filePath(QStringLiteral("sound.wav"));
    bool const hasSound = run({QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path, QStringLiteral("-vn"),
                               QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-ar"), QString::number(SoundData::kRate), wav},
                              nullptr)
                          && QFileInfo(wav).size() > 44;

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
                                                      tr("Movies (*.mp4 *.mov *.m4v *.avi *.mkv *.webm *.wmv *.mpg *.mpeg *.gif);;All Files (*)"));
    if (path.isEmpty())
        return;
    stop();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    int const at = importMovie(path, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0)
        QMessageBox::critical(this, tr("Eclipse"), error);
    else
        statusBar()->showMessage(tr("Imported %1").arg(QDir::toNativeSeparators(path)), 6000);
}
