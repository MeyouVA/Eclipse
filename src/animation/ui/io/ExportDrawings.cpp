// Eclipse - exporting: the drawings of layers, each written to a file of its own (a PNG picture or SVG lines).
#include "animation/ui/window/MainWindow.h"

#include <QDir>
#include <QColorSpace>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QStatusBar>
#include <QSvgGenerator>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"
#include "animation/ui/timeline/Timeline.h"

int MainWindow::exportDrawings(QString const& folder, QVector<int> const& layers, QString const& format, QString* error)
{
    Scene const& scene = m_document->scene();
    bool const svg = format.compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0;
    ImageFiles::Format const* const imageFormat=ImageFiles::find(format);
    if(!imageFormat || !imageFormat->writable) { if(error) *error=tr("Unsupported drawing output format: %1").arg(format); return 0; }
    if (!QDir().mkpath(folder)) {
        if (error)
            *error = tr("The folder %1 could not be made.").arg(QDir::toNativeSeparators(folder));
        return 0;
    }
    int written = 0;
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        Layer const& layer = scene.layers[l];
        for (int d = 0; d < layer.drawings.size(); ++d) {
            // Names that cannot be file names lose the characters that stop them.
            QString name = layer.name + QLatin1Char('-') + layer.drawingLabel(d);
            name.replace(QRegularExpression(QStringLiteral(R"([\/:*?"<>|])")), QStringLiteral("_"));
            QString const path = QDir(folder).filePath(name + QLatin1Char('.') + imageFormat->suffix);
            if (svg) {
                QSvgGenerator generator;
                generator.setFileName(path);
                generator.setSize(QSize(scene.width, scene.height));
                generator.setViewBox(QRect(0, 0, scene.width, scene.height));
                generator.setTitle(name);
                QPainter painter;
                if (!painter.begin(&generator)) {
                    if (error)
                        *error = tr("%1 could not be written.").arg(QDir::toNativeSeparators(path));
                    return written;
                }
                painter.setRenderHint(QPainter::Antialiasing, true);
                Canvas::paintDrawing(painter, scene, layer.drawings[d], 1.0);
                painter.end();
            } else {
                QImage::Format pixels=QImage::Format_ARGB32_Premultiplied;
                QColorSpace profile;
                for(Picture const& picture : layer.drawings[d].pictures) {
                    if(picture.image.pixelFormat().typeInterpretation()==QPixelFormat::FloatingPoint) pixels=QImage::Format_RGBA32FPx4_Premultiplied;
                    else if(picture.image.depth()>32 && pixels!=QImage::Format_RGBA32FPx4_Premultiplied) pixels=QImage::Format_RGBA64_Premultiplied;
                    if(picture.image.colorSpace().isValid()) profile=picture.image.colorSpace();
                }
                QImage image(scene.width, scene.height, pixels);
                if(profile.isValid()) image.setColorSpace(profile);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing, true);
                Canvas::paintDrawing(painter, scene, layer.drawings[d], 1.0);
                painter.end();
                if (!ImageFiles::save(image,path,imageFormat->suffix,error)) {
                    if (error)
                        *error = tr("%1 could not be written.").arg(QDir::toNativeSeparators(path));
                    return written;
                }
            }
            ++written;
        }
    }
    return written;
}

void MainWindow::onActionExportDrawings()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty() && layer() >= 0)
        layers = {layer()};
    Scene const& scene = m_document->scene();
    layers.removeIf([&](int l) { return l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings(); });
    if (layers.isEmpty()) {
        QMessageBox::information(this, tr("Export Drawings"), tr("No drawings available in current selection."));
        return;
    }
    bool ok = false;
    QStringList names, formats;
    for(ImageFiles::Format const& output : ImageFiles::formats())
        if(output.writable) { names << (output.suffix==QLatin1String("svg") ? tr("SVG lines") : output.name); formats << output.suffix; }
    QString const kind = QInputDialog::getItem(this, tr("Export Drawings"), tr("Drawings as:"), names, 0, false, &ok);
    if (!ok)
        return;
    QString const start = m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).path() + QStringLiteral("/frames");
    QString const folder = QFileDialog::getExistingDirectory(this, tr("Export Drawings"), start);
    if (folder.isEmpty())
        return;
    QString error;
    int const written = exportDrawings(folder, layers, formats.value(names.indexOf(kind),QStringLiteral("png")), &error);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Export Drawings"), error);
    else
        statusBar()->showMessage(tr("%n drawing(s) written to %1", nullptr, written).arg(QDir::toNativeSeparators(folder)), 6000);
}
