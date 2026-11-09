#include "board/ui/BoardWindow.h"
#include "board/model/BoardProjectJson.h"
#include "board/ui/BoardLibrary.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardTimeline.h"
#include "board/ui/export/BoardRender.h"
#include "render/images/ImageFiles.h"

#include <QColorSpace>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

void BoardWindow::importBoardImages(QString const& operation)
{
    auto const [shot, panel] = m_selected;
    bool const toLibrary = operation == QLatin1String("library");
    if (toLibrary ? m_document.path().isEmpty() : !m_document.panel(shot, panel))
        return;
    m_playback->stop();
    bool const asScenes = operation == QLatin1String("scenes");
    QString const title = toLibrary ? tr("Import Images into Library") : (asScenes ? tr("Import Images as Scenes") : tr("Import Images as Layers"));
    QStringList const files = QFileDialog::getOpenFileNames(this, title, QFileInfo(m_document.path()).absolutePath(),
        ImageFiles::fileFilter(), nullptr, QFileDialog::HideNameFilterDetails);
    if (files.isEmpty())
        return;

    QVector<QPair<QString, QImage>> images;
    images.reserve(files.size());
    qint64 bytes = 0;
    for (QString const& file : files) {
        QString error;
        QImage image = ImageFiles::load(file, &error);
        bytes += image.sizeInBytes();
        if (image.isNull() || image.sizeInBytes() > BoardProjectJson::maxEmbeddedImageBytes
            || bytes > BoardProjectJson::maxEmbeddedImagesBytes) {
            QMessageBox::warning(this, title, tr("Could not import “%1”.\n%2").arg(QFileInfo(file).fileName(),
                image.isNull() ? error : tr("The embedded images exceed the project size limit.")));
            return;
        }
        images.append({QFileInfo(file).completeBaseName(), std::move(image)});
    }

    int const frames = asScenes ? defaultPanelFrames(m_document.project().fps) : 0;
    bool const imported = toLibrary ? m_document.addLibraryImages(images) : m_document.importPictures(shot, panel, images, frames);
    if (!imported) {
        QMessageBox::warning(this, title, tr("These images could not be added within the current project’s limits."));
        return;
    }
    if (asScenes)
        selectPanel(shot + 1, 0);
    if (toLibrary) {
        showBoardView(QStringLiteral("library"));
        m_library->selectLastItem();
    }
    statusBar()->showMessage(tr("Imported %n image(s)", nullptr, int(images.size())), 5000);
}

void BoardWindow::exportBoardFrame()
{
    if (m_document.path().isEmpty())
        return;
    m_playback->stop();
    int const frame = m_timeline->frame();
    BoardProject const project = m_document.project();
    QString const title = tr("Export Current Image");
    QFileDialog dialog(this, title, QFileInfo(m_document.path()).absolutePath(), ImageFiles::fileFilter(true));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::HideNameFilterDetails);
    dialog.setDefaultSuffix(QStringLiteral("png"));
    dialog.selectFile(tr("%1 - %2.png").arg(project.name).arg(frame + 1));
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString const file = dialog.selectedFiles().value(0);
    auto const* format = ImageFiles::find(QFileInfo(file).suffix());
    if (!format || !format->writable) {
        QMessageBox::warning(this, title, tr("Choose a filename with a supported image extension, such as .png."));
        return;
    }
    QColor const background = format->alpha ? QColor(Qt::transparent) : QColor(Qt::white);
    BoardRender render(project);
    QImage image = render.frame(frame, QSize(project.width, project.height), background, format->channelBits > 8);
    image.setColorSpace(QColorSpace::SRgb);
    QString error;
    if (ImageFiles::save(image, file, format->suffix, &error))
        statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(file)), 6000);
    else
        QMessageBox::warning(this, title, error);
}
