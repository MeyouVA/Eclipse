#include "board/ui/export/BoardExport.h"
#include "board/ui/export/BoardExportFiles.h"
#include "board/ui/export/BoardRender.h"
#include "canvas/export/MovieExport.h"
#include "canvas/export/StoryboardPdf.h"
#include "model/scene/AtomicFile.h"
#include "model/scene/Scene.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QTemporaryDir>

namespace {
QString tr(char const* message) { return QCoreApplication::translate("BoardExport", message); }

Scene metadata(BoardProject const& project, int frames)
{
    Scene scene;
    scene.layers.clear();
    scene.width = project.width;
    scene.height = project.height;
    scene.fps = project.fps;
    scene.background = QColor(238, 238, 238);
    scene.setFrameCount(frames);
    return scene;
}
}

bool BoardExport::pdf(BoardProject const& project, QString const& projectFile, QString const& path,
                      StoryboardPdf::Profile const& profile, QString* error)
{
    BoardRender render(project);
    Scene scene = metadata(project, render.length());
    scene.storyboard = project.timeline();
    QTemporaryDir staging;
    if (!staging.isValid())
        return fail(error, tr("Could not create a temporary output folder."));
    QString const generated = staging.filePath(QStringLiteral("board.pdf"));
    Storyboard::Naming naming;
    naming.customPanelNames = true;
    bool imageFailed = false;
    bool const written = StoryboardPdf::writeWithFrames(scene, profile, generated, projectFile, naming,
        [&](int frame, QSize size) {
            QImage const picture = render.frame(frame, size, scene.background, false, false);
            imageFailed |= picture.isNull();
            return picture;
        }, error);
    if (!written)
        return false;
    if (imageFailed)
        return fail(error, tr("Could not render a panel for the PDF."));
    return AtomicFile::replaceFrom(generated, path, error);
}

bool BoardExport::movie(BoardProject const& project, QString const& projectFile, QString const& path,
                        bool transparent, Progress const& progress, QString* error)
{
    BoardRender render(project);
    Scene const scene = metadata(project, render.length());
    MovieExport::Options options;
    options.transparentBackground = transparent;
    MovieExport::Source source;
    source.picture = [&](int frame, QSize size, QColor background, bool deep) {
        return render.frame(frame, size, background, deep);
    };
    if (!project.sound.muted && project.sound.start < render.length()) {
        source.audioPath = project.audioPath(projectFile);
        source.audioStart = project.sound.start / project.fps;
        source.audioOffset = project.sound.sourceOffset / project.fps;
        source.audioLength = project.sound.length / project.fps;
        if (!source.audioPath.isEmpty() && !QFileInfo(source.audioPath).isFile())
            return fail(error, tr("The sound file is missing: %1").arg(project.sound.name));
    }
    return MovieExport::write(scene, path, progress, error, options, source);
}

bool BoardExport::images(BoardProject const& project, QString const& folder, bool transparent,
                         Progress const& progress, QString* error)
{
    BoardRender render(project);
    QSize const size(project.width, project.height);
    QColor const paper = transparent ? QColor(Qt::transparent) : QColor(238, 238, 238);
    int const digits = std::max(6, int(QString::number(render.length()).size()));
    return publishFolder(folder, [&](QDir const& dir) {
        for (int frame = 0; frame < render.length(); ++frame) {
            if (progress && !progress(frame))
                return fail(error, tr("Stopped."));
            auto const picture = render.frame(frame, size, paper, true);
            if (picture.isNull())
                return fail(error, tr("Could not render frame %1.").arg(frame + 1));
            QString const name = QStringLiteral("frame_%1.png").arg(frame + 1, digits, 10, QLatin1Char('0'));
            QImageWriter writer(dir.filePath(name), QByteArrayLiteral("png"));
            if (!writer.write(picture))
                return fail(error, tr("Could not save %1. %2").arg(name, writer.errorString()));
        }
        return true;
    }, error);
}
