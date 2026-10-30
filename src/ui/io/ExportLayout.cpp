// Eclipse - exporting: the layout image, the frame as it is (every layer, or those selected) written to a picture file.
#include "ui/window/MainWindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"
#include "ui/timeline/Timeline.h"

bool MainWindow::exportLayoutImage(QString const& path, bool selectedOnly, QString* error)
{
    Scene scene = m_document->scene();
    if (selectedOnly) {
        // Only the drawing layers picked in the Timeline are shown (the pegs and groups they hang on still move them).
        QVector<int> picked = m_timeline->selectedLayers();
        if (picked.isEmpty() && layer() >= 0)
            picked = {layer()};
        for (int l = 0; l < scene.layers.size(); ++l)
            if (scene.layers[l].holdsDrawings() && !picked.contains(l))
                scene.layers[l].visible = false;
    }
    QImage const image = FrameRender::frame(scene, frame(), QSize(scene.width, scene.height), false, scene.background);
    return ImageFiles::save(image, path, QFileInfo(path).suffix().toLower(), error);
}

static QString layoutPath(QWidget* parent, QString const& scenePath, QString const& title)
{
    QString start = scenePath.isEmpty() ? QStringLiteral("layout.png") : QFileInfo(scenePath).path() + QStringLiteral("/frames/") + QFileInfo(scenePath).completeBaseName() + QStringLiteral("_layout.png");
    QString path = QFileDialog::getSaveFileName(parent, title, start, QObject::tr("Pictures (*.png *.jpg *.tga *.tif *.psd);;All Files (*)"));
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    return path;
}

void MainWindow::onActionExportLayoutImage()
{
    QString const path = layoutPath(this, m_path, tr("Export Layout Image"));
    QString error;
    if (path.isEmpty())
        return;
    if (!exportLayoutImage(path, false, &error))
        QMessageBox::warning(this, tr("Export Layout Image"), error);
    else
        statusBar()->showMessage(tr("Layout image written: %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void MainWindow::onActionExportLayoutImageFromSelection()
{
    QString const path = layoutPath(this, m_path, tr("Export Layout Image from Selection"));
    QString error;
    if (path.isEmpty())
        return;
    if (!exportLayoutImage(path, true, &error))
        QMessageBox::warning(this, tr("Export Layout Image from Selection"), error);
    else
        statusBar()->showMessage(tr("Layout image written: %1").arg(QDir::toNativeSeparators(path)), 6000);
}
