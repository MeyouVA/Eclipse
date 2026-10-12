// Eclipse - printing a frame.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/io/MovieExport.h"
#include "ui/io/ImportExportPrivate.h"

bool MainWindow::printFrame(QPrinter& printer, int frame)
{
    Scene const scene = m_document->scene();
    frame = std::clamp(frame, 0, scene.frameCount() - 1);
    QPainter painter;
    if (!painter.begin(&printer))
        return false;
    // The frame as large as the page takes it, in the middle; a hair line round it.
    QRectF const page = printer.pageLayout().paintRectPixels(printer.resolution());
    QRectF const sheet(0, 0, page.width(), page.height());
    double const scale = std::min(sheet.width() / scene.width, sheet.height() / scene.height);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate((sheet.width() - scene.width * scale) / 2.0, (sheet.height() - scene.height * scale) / 2.0);
    painter.scale(scale, scale);
    painter.setClipRect(QRectF(0, 0, scene.width, scene.height));
    Canvas::paintFrame(painter, scene, frame);
    painter.setClipping(false);
    QPen edge(QColor(120, 120, 120));
    edge.setCosmetic(true);
    painter.setPen(edge);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(0, 0, scene.width, scene.height));
    return painter.end();
}

void MainWindow::onActionPrint()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(m_document->scene().width >= m_document->scene().height ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer.setDocName(m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName());
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print Frame %1").arg(frame() + 1));
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (printFrame(printer, frame()))
        statusBar()->showMessage(tr("Frame %1 sent to the printer.").arg(frame() + 1), 5000);
    else
        QMessageBox::warning(this, tr("Print"), tr("The frame could not be printed."));
}
