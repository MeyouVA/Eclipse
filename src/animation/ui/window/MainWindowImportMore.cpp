// Eclipse - the main window: Import > Sprite Sheets... and Import > PDF / Illustrator...
#include "animation/ui/window/MainWindow.h"

#include "formats/pdf/PdfReader.h"
#include "model/document/Commands.h"
#include "render/images/ImageFiles.h"
#include "animation/ui/timeline/Timeline.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>

namespace {

// The runs of rows (or columns) of a picture that hold something: where each starts and how long it is.
QVector<QPair<int, int>> runsOf(QImage const& image, bool rows, QRgb background)
{
    int const length = rows ? image.height() : image.width(), across = rows ? image.width() : image.height();
    QVector<QPair<int, int>> runs;
    int start = -1;
    for (int i = 0; i <= length; ++i) {
        bool full = false;
        for (int j = 0; i < length && j < across && !full; ++j) {
            QRgb const c = rows ? image.pixel(j, i) : image.pixel(i, j);
            full = qAlpha(c) > 8 && c != background;
        }
        if (full && start < 0)
            start = i;
        if (!full && start >= 0) {
            runs.push_back({start, i - start});
            start = -1;
        }
    }
    return runs;
}

}  // namespace

int MainWindow::importSpriteSheet(QString const& file, int across, int down, int count, QString* error)
{
    QImage const sheet = ImageFiles::load(file).convertToFormat(QImage::Format_ARGB32);
    if (sheet.isNull()) {
        if (error)
            *error = tr("The imported file does not have a supported image format.");
        return -1;
    }
    // Where the sprites are: as the sheet's own description says (one written with it), as many across and down as asked,
    // or found from the empty rows and columns between them.
    QVector<QRect> cells;
    QFile description(QFileInfo(file).absoluteDir().filePath(QFileInfo(file).completeBaseName() + QStringLiteral(".json")));
    if (across <= 0 && down <= 0 && description.open(QIODevice::ReadOnly)) {
        for (QJsonValue const& f : QJsonDocument::fromJson(description.readAll()).object()[QStringLiteral("frames")].toArray()) {
            QJsonObject const o = f.toObject();
            cells.push_back(QRect(o[QStringLiteral("x")].toInt(), o[QStringLiteral("y")].toInt(), o[QStringLiteral("w")].toInt(), o[QStringLiteral("h")].toInt()));
        }
    }
    if (cells.isEmpty() && across > 0 && down > 0) {
        int const w = sheet.width() / across, h = sheet.height() / down;
        for (int y = 0; y < down; ++y)
            for (int x = 0; x < across; ++x)
                cells.push_back(QRect(x * w, y * h, w, h));
    }
    if (cells.isEmpty()) {
        // The gaps: see-through, when the sheet has any; else the colour of its corner.
        bool clear = false;
        for (int y = 0; y < sheet.height() && !clear; ++y)
            for (int x = 0; x < sheet.width() && !clear; ++x)
                clear = qAlpha(sheet.pixel(x, y)) <= 8;
        QRgb const background = clear ? qRgba(0, 0, 0, 0) : sheet.pixel(0, 0);
        for (QPair<int, int> const& row : runsOf(sheet, true, background)) {
            QImage const band = sheet.copy(0, row.first, sheet.width(), row.second);
            for (QPair<int, int> const& column : runsOf(band, false, background))
                cells.push_back(QRect(column.first, row.first, column.second, row.second));
        }
    }
    if (count > 0 && cells.size() > count)
        cells.resize(count);
    if (cells.isEmpty()) {
        if (error)
            *error = tr("No sprites were found in %1.").arg(QFileInfo(file).fileName());
        return -1;
    }
    // Every sprite the size of the largest, placed in the middle of the frame the same way, so that they line up.
    QSize most;
    for (QRect const& c : cells)
        most = most.expandedTo(c.size());
    Scene const& scene = m_document->scene();
    double const scale = std::min(1.0, std::min(double(scene.width) / most.width(), double(scene.height) / most.height()));
    QVector<Drawing> drawings;
    for (QRect const& c : cells) {
        Picture picture;
        picture.image = sheet.copy(c);
        picture.size = picture.image.size();
        picture.placement = QTransform::fromScale(scale, scale)
                            * QTransform::fromTranslate((scene.width - most.width() * scale) / 2.0 + (most.width() - c.width()) * scale / 2.0,
                                                        (scene.height - most.height() * scale) / 2.0 + (most.height() - c.height()) * scale);
        Drawing d;
        d.pictures = {picture};
        drawings.push_back(d);
    }
    m_document->beginMacro(tr("Import Sprite Sheets"));
    int const at = m_document->importDrawings(-1, QFileInfo(file).completeBaseName(), 0, drawings, 1);
    m_document->endMacro();
    if (at < 0 && error)
        *error = tr("Failed to import images for layer %1.").arg(QFileInfo(file).completeBaseName());
    return at;
}

void MainWindow::onActionImportSpriteSheet()
{
    QString const file = QFileDialog::getOpenFileName(this, tr("Import Sprite Sheets"), QString(), ImageFiles::fileFilter(), nullptr, QFileDialog::HideNameFilterDetails);
    if (file.isEmpty())
        return;
    QDialog ask(this);
    ask.setWindowTitle(tr("Import Sprite Sheets"));
    QFormLayout* const form = new QFormLayout(&ask);
    QSpinBox* const across = new QSpinBox(&ask);
    QSpinBox* const down = new QSpinBox(&ask);
    QSpinBox* const count = new QSpinBox(&ask);
    for (QSpinBox* const box : {across, down, count}) {
        box->setRange(0, 999);
        box->setSpecialValueText(tr("Find"));
    }
    count->setSpecialValueText(tr("All"));
    form->addRow(tr("Sprites across"), across);
    form->addRow(tr("Sprites down"), down);
    form->addRow(tr("How many"), count);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    form->addRow(buttons);
    if (ask.exec() != QDialog::Accepted)
        return;
    QString error;
    int const at = importSpriteSheet(file, across->value(), down->value(), count->value(), &error);
    if (at < 0)
        QMessageBox::warning(this, tr("Import Sprite Sheets"), error);
    else
        statusBar()->showMessage(tr("%n sprite(s) imported, a drawing each.", nullptr, int(m_document->scene().layers[at].drawings.size())), 6000);
}

int MainWindow::importPdf(QString const& file, bool separateLayers, QString* error)
{
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    Pdf::Options options;
    options.cmyk = s.value(QStringLiteral("prefs/pdfCmyk"), true).toBool();
    Pdf::Report report;
    QVector<Pdf::Page> pages;
    if (!Pdf::read(file, pages, error, options, &report))
        return -1;
    Scene const& scene = m_document->scene();
    // Each page fitted to the frame, in the middle; a drawing a frame, page by page.
    auto placed = [&](Pdf::Page const& page, Drawing d) {
        double const scale = std::min(scene.width / std::max(1.0, page.size.width()), scene.height / std::max(1.0, page.size.height()));
        QTransform const m = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - page.size.width() * scale) / 2.0, (scene.height - page.size.height() * scale) / 2.0);
        for (Fill& f : d.fills)
            f.transform(m);
        for (Stroke& line : d.strokes) {
            line.transform(m);
            line.width = float(line.width * scale);
        }
        return d;
    };
    QString const base = QFileInfo(file).completeBaseName();
    QStringList names;
    if (separateLayers)
        for (Pdf::Page const& page : pages)
            for (Pdf::ArtLayer const& l : page.layers)
                if (!names.contains(l.name))
                    names << l.name;
    if (names.isEmpty())
        names << QString();
    m_document->beginMacro(tr("Import PDF"));
    int first = -1;
    for (QString const& name : names) {
        QVector<Drawing> drawings;
        for (Pdf::Page const& page : pages) {
            Drawing d;
            for (Pdf::ArtLayer const& l : page.layers)
                if (!separateLayers || l.name == name) {
                    d.fills += l.drawing.fills;
                    d.strokes += l.drawing.strokes;
                }
            drawings.push_back(placed(page, d));
        }
        int const at = m_document->importDrawings(-1, name.isEmpty() ? base : name, 0, drawings, 1);
        if (first < 0)
            first = at;
    }
    m_document->endMacro();
    QStringList left;
    if (report.texts)
        left << tr("%n piece(s) of text", nullptr, report.texts);
    if (report.images)
        left << tr("%n picture(s)", nullptr, report.images);
    if (report.cmykLeftOut)
        left << tr("%n path(s) in CMYK (Support CMYK in PDF/Illustrator Import is off)", nullptr, report.cmykLeftOut);
    if (report.streamsUnread)
        left << tr("%n part(s) in a compression that is not read", nullptr, report.streamsUnread);
    if (error)
        *error = left.isEmpty() ? QString() : tr("Left out: %1.").arg(left.join(QStringLiteral(", ")));
    return first;
}

void MainWindow::onActionImportPdf()
{
    QString const file = QFileDialog::getOpenFileName(this, tr("Import PDF / Illustrator"), QString(), tr("PDF and Illustrator files (*.pdf *.ai)"));
    if (file.isEmpty())
        return;
    bool const separate = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/pdfSeparateLayers"), true).toBool();
    QString note;
    int const at = importPdf(file, separate, &note);
    if (at < 0) {
        QMessageBox::warning(this, tr("Import PDF / Illustrator"), note.isEmpty() ? tr("Error in file import") : note);
        return;
    }
    statusBar()->showMessage(tr("%1 imported as vector drawings.").arg(QFileInfo(file).fileName()) + (note.isEmpty() ? QString() : QLatin1Char(' ') + note), 10000);
}

void MainWindow::onActionExportTransformations()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    QString const path = QFileDialog::getSaveFileName(this, tr("Select Animation Export File"), QString(), tr("Eclipse transformations (*.ecltransform)"));
    if (path.isEmpty())
        return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(m_document->exportTransformations(layers)) < 0) {
        QMessageBox::warning(this, tr("Export Transformations"), out.errorString());
        return;
    }
    statusBar()->showMessage(tr("The transformations of %n layer(s) written.", nullptr, int(layers.size())), 5000);
}

void MainWindow::onActionImportTransformations()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Select Animation Import File"), QString(), tr("Eclipse transformations (*.ecltransform)"));
    if (path.isEmpty())
        return;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Transformations"), tr("Unable to find the given import file."));
        return;
    }
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    QString error;
    int const done = m_document->importTransformations(in.readAll(), layers, frame(), &error);
    if (done == 0)
        QMessageBox::warning(this, tr("Import Transformations"), error);
    else
        statusBar()->showMessage(tr("Transformations imported onto %n layer(s).", nullptr, done), 5000);
}

QByteArray MainWindow::xsheetCsv(Scene const& scene)
{
    // Export CSV: the exposure sheet as a table - a row for each frame, a column for each drawing layer, the drawing
    // shown there (empty where none is).
    auto quoted = [](QString text) {
        if (text.contains(QLatin1Char(',')) || text.contains(QLatin1Char('"')) || text.contains(QLatin1Char('\n')))
            text = QLatin1Char('"') + text.replace(QStringLiteral("\""), QStringLiteral("\"\"")) + QLatin1Char('"');
        return text;
    };
    QVector<int> columns;
    QStringList header{QStringLiteral("Frame")};
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].holdsDrawings()) {
            columns.push_back(l);
            header << quoted(scene.layers[l].name);
        }
    QStringList lines{header.join(QLatin1Char(','))};
    for (int f = 0; f < scene.frameCount(); ++f) {
        QStringList row{QString::number(f + 1)};
        for (int l : columns) {
            int const d = scene.layers[l].drawingAt(f);
            row << (d == Layer::kNoDrawing ? QString() : quoted(scene.layers[l].drawingLabel(d)));
        }
        lines << row.join(QLatin1Char(','));
    }
    return (lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}

void MainWindow::onActionExportCsv()
{
    QString const path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QString(), tr("Comma-separated values (*.csv)"));
    if (path.isEmpty())
        return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(xsheetCsv(m_document->scene())) < 0) {
        QMessageBox::warning(this, tr("Export CSV"), tr("The export operation has failed because the file is not writable. Verify that the saving location is available and writable then try again."));
        return;
    }
    statusBar()->showMessage(tr("Export completed: %1").arg(QDir::toNativeSeparators(path)), 5000);
}
