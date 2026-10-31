// Eclipse - exporting: the drawings of layers, each written to a file of its own (a PNG picture or SVG lines).
#include "ui/window/MainWindow.h"

#include <QDir>
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
#include "ui/timeline/Timeline.h"

int MainWindow::exportDrawings(QString const& folder, QVector<int> const& layers, QString const& format, QString* error)
{
    Scene const& scene = m_document->scene();
    bool const svg = format.compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0;
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
            QString const path = QDir(folder).filePath(name + (svg ? QStringLiteral(".svg") : QStringLiteral(".png")));
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
                QImage image(scene.width, scene.height, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing, true);
                Canvas::paintDrawing(painter, scene, layer.drawings[d], 1.0);
                painter.end();
                if (!image.save(path, "PNG")) {
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
    QString const kind = QInputDialog::getItem(this, tr("Export Drawings"), tr("Drawings as:"), {tr("PNG pictures"), tr("SVG lines")}, 0, false, &ok);
    if (!ok)
        return;
    QString const start = m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).path() + QStringLiteral("/frames");
    QString const folder = QFileDialog::getExistingDirectory(this, tr("Export Drawings"), start);
    if (folder.isEmpty())
        return;
    QString error;
    int const written = exportDrawings(folder, layers, kind == tr("SVG lines") ? QStringLiteral("svg") : QStringLiteral("png"), &error);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Export Drawings"), error);
    else
        statusBar()->showMessage(tr("%n drawing(s) written to %1", nullptr, written).arg(QDir::toNativeSeparators(folder)), 6000);
}
