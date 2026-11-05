// Eclipse - the main window: scanning drawings into the cells of a layer, and the scan settings in the menus.
#include "actions/theme/AppSettings.h"
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"
#include "ui/io/Scanning.h"
#include "ui/io/Scanner.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

int MainWindow::scanPages(QStringList const& pages, ScanSettings const& settings, QString const& name, QString* error)
{
    Scene const& scene = m_document->scene();
    int const target = layer();
    if (target < 0 || target >= scene.layers.size() || !scene.layers[target].holdsDrawings()) {
        if (error)
            *error = tr("Select a cell of a drawing layer to scan into.");
        return 0;
    }
    int at = std::max(0, frame());
    int const swatch = m_palette->currentSwatch();
    QColor const colour = scene.resolve(swatch, QColor(Qt::black));
    QString line = settings.vectorizeLine.isEmpty() ? vectorizeOptionsLine() : settings.vectorizeLine;
    if (settings.type == ScanSettings::VectorColour && !line.contains(QLatin1String("-2pass")))
        line += QLatin1String(" -2pass");
    if (line.trimmed().isEmpty())
        line = QStringLiteral("-thresh 0.5");
    QStringList notes;
    int made = 0;
    m_document->beginMacro(tr("Scan Drawing"));
    for (int k = 0; k < pages.size(); ++k) {
        QImage const raw = ImageFiles::load(pages[k]);
        if (raw.isNull()) {
            notes << tr("Cannot load file %1, unsupported type.").arg(QFileInfo(pages[k]).fileName());
            continue;
        }
        QString note;
        QImage const page = settings.process(raw, &note);
        if (!note.isEmpty())
            notes << note;
        // The page as so many fields across (12 fields: the frame's width) - or, Page Panning, at its own size by its dpi.
        double const scale = settings.pagePanning ? scene.width / 12.0 / std::max(1, settings.dpi)
                                                  : settings.fields / 12.0 * scene.width / std::max(1, page.width());
        Picture picture;
        picture.image = page;
        picture.size = page.size();
        picture.file = QFileInfo(pages[k]).absoluteFilePath();
        picture.placement = QTransform::fromScale(scale, scale)
                            * QTransform::fromTranslate((scene.width - page.width() * scale) / 2.0, (scene.height - page.height() * scale) / 2.0);
        Drawing drawing;
        drawing.pictures = {picture};
        if (settings.type != ScanSettings::Bitmap && !vectorizeDrawing(drawing, line, colour, swatch, &notes)) {
            notes << tr("Could not vectorize scanned images while creating layer %1.").arg(scene.layers[target].name);
            continue;
        }
        drawing.scanInfo = settings.info(raw, pages[k]);
        drawing.name = name.isEmpty() ? QString() : pages.size() > 1 ? QStringLiteral("%1-%2").arg(name).arg(k + 1) : name;
        Layer const& now = m_document->scene().layers[target];
        int const there = at < now.cells.size() ? now.cells[at] : Layer::kNoDrawing;
        if (settings.overwrite && there >= 0 && there < now.drawings.size()) {
            // Overwrite Drawings: what was scanned goes into the drawing on the cell (it keeps its name unless given one).
            m_document->editLayers({target}, tr("Scan Drawing"), [there, drawing](Layer& l) {
                Drawing replaced = drawing;
                if (replaced.name.isEmpty())
                    replaced.name = l.drawings[there].name;
                replaced.set = l.drawings[there].set;
                l.drawings[there] = replaced;
            });
        } else if (m_document->importDrawings(target, QString(), at, {drawing}, std::max(1, settings.hold)) < 0) {
            notes << tr("Error saving scan.");
            continue;
        }
        ++made;
        if (settings.scanAndAdvance)
            at += std::max(1, settings.hold);
    }
    m_document->endMacro();
    if (settings.scanAndAdvance && made > 0)
        m_timeline->setCurrentCell(target, std::min(at, m_document->scene().frameCount() - 1));
    if (error)
        *error = notes.join(QLatin1Char('\n'));
    return made;
}

void MainWindow::onActionScanDrawing()
{
    // Scan New Drawing...: the Scan Drawing window, each scan into the current cell.
    ScanDrawingDialog dialog(this, this);
    dialog.exec();
}

void MainWindow::onActionScanOption(QString const& which)
{
    // Scan and Advance / Overwrite Drawings / Flip Drawings / Optical Registration, kept for the next scans.
    ScanSettings s = ScanSettings::load();
    bool* const flag = which == QLatin1String("advance") ? &s.scanAndAdvance : which == QLatin1String("overwrite") ? &s.overwrite
                       : which == QLatin1String("flip") ? &s.flip : which == QLatin1String("registration") ? &s.opticalRegistration : nullptr;
    if (!flag)
        return;
    *flag = !*flag;
    s.save();
    statusBar()->showMessage(*flag ? tr("Scanning: %1 on.").arg(which) : tr("Scanning: %1 off.").arg(which), 3000);
}

void MainWindow::onShowScanningMenu(QMenu*)
{
    ScanSettings const s = ScanSettings::load();
    m_actions->setChecked(QStringLiteral("ScanAndAdvance"), s.scanAndAdvance);
    m_actions->setChecked(QStringLiteral("ScanOverwrite"), s.overwrite);
    m_actions->setChecked(QStringLiteral("ScanFlip"), s.flip);
    m_actions->setChecked(QStringLiteral("ScanRegistration"), s.opticalRegistration);
}

void MainWindow::onActionScanningParameters()
{
    ScanConfigDialog dialog(ScanSettings::load(), this);
    if (dialog.exec() == QDialog::Accepted)
        dialog.settings().save();
}

void MainWindow::onActionShowScanInfo(bool shown)
{
    m_canvas->setScanInfoShown(shown);
}

void MainWindow::onActionShowParticlesAsDots(bool dots)
{
    m_canvas->setParticlesAsDots(dots);
    settings().setValue(QStringLiteral("prefs/particlesAsDots"), dots);
}

void MainWindow::onActionAcquireTwain()
{
    // Acquire from TWAIN: the scanner's own window (the system's, which drives TWAIN scanners too), the pages then imported.
    onActionImportScanner();
}
