// Eclipse - the main window: Vectorize Line Art in Selected Drawings - the pictures of the drawings selected made into
// vector art with the Vectorization Parameters.
#include "animation/ui/window/MainWindow.h"

#include "model/document/Commands.h"
#include "model/geometry/VectorizeOptions.h"
#include "animation/ui/io/VectorizeDialog.h"
#include "animation/ui/panels/PalettePanel.h"
#include "animation/ui/timeline/Timeline.h"

#include <QApplication>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>

QString MainWindow::vectorizeOptionsLine()
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QString const style = s.value(QStringLiteral("prefs/vectorizeStyle"), QStringLiteral("Default Style")).toString();
    return s.value(QStringLiteral("prefs/vectorizeOptions"), Vectorize::styleLine(style)).toString();
}

bool MainWindow::vectorizeDrawing(Drawing& drawing, QString const& line, QColor const& colour, int swatch, QStringList* notes)
{
    Vectorize::Options options;
    if (!Vectorize::parse(line, options))
        return false;
    bool any = false;
    QVector<Picture> kept;
    for (Picture const& picture : std::as_const(drawing.pictures)) {
        if (picture.image.isNull()) {
            kept.push_back(picture);
            continue;
        }
        // Over white paper, as a scan is.
        QImage flat(picture.image.size(), QImage::Format_ARGB32);
        flat.fill(Qt::white);
        {
            QPainter p(&flat);
            p.drawImage(0, 0, picture.image);
        }
        Vectorize::Result const made = Vectorize::run(flat, options, colour, swatch);
        if (notes)
            *notes << made.notes;
        if (!made.ok) {
            kept.push_back(picture);
            continue;
        }
        any = true;
        QTransform const placed = QTransform::fromScale(double(picture.size.width()) / std::max(1, picture.image.width()),
                                                        double(picture.size.height()) / std::max(1, picture.image.height()))
                                  * picture.placement;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            for (Fill f : made.drawing.fillsOf(sheet)) {
                f.transform(placed);
                f.giveContours(std::max(0.75, placed.m11()));
                drawing.fillsOf(sheet).push_back(f);
            }
            for (Stroke s : made.drawing.strokesOf(sheet)) {
                s.transform(placed);
                s.width = float(s.width * std::sqrt(std::abs(placed.determinant())));
                drawing.strokesOf(sheet).push_back(s);
            }
        }
    }
    drawing.pictures = kept;
    return any;
}

int MainWindow::vectorizeSelectedDrawings(QString const& line, QString* error)
{
    Scene const& scene = m_document->scene();
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    int first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    if (first < 0 || last < first)
        first = last = frame();
    Vectorize::Options check;
    if (!Vectorize::parse(line, check, error))
        return -1;
    int const swatch = m_palette->currentSwatch();
    QColor const colour = scene.resolve(swatch, QColor(Qt::black));
    int made = 0;
    QStringList notes;
    m_document->beginMacro(tr("Vectorize Drawings"));
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        // Each drawing once, however many of its cells are selected.
        QVector<int> done;
        for (int f = first; f <= last; ++f) {
            int const index = m_document->scene().layers[l].drawingAt(f);
            if (index < 0 || done.contains(index))
                continue;
            done.push_back(index);
            Drawing whole = m_document->scene().layers[l].drawings[index];
            if (!vectorizeDrawing(whole, line, colour, swatch, &notes))
                continue;
            m_document->editLayers({l}, tr("Vectorize Drawings"), [index, whole](Layer& into) {
                if (index < into.drawings.size())
                    into.drawings[index] = whole;
            });
            ++made;
        }
    }
    m_document->endMacro();
    if (error)
        *error = made == 0 ? tr("Unable to vectorize layers: the drawings selected hold no pictures.") : notes.join(QLatin1Char('\n'));
    return made;
}

void MainWindow::onActionVectorizeDrawings()
{
    // Vectorize Line Art in Selected Drawings: the settings asked for (the first picture to preview), then all of them.
    Scene const& scene = m_document->scene();
    QImage preview;
    if (layer() >= 0 && layer() < scene.layers.size())
        if (int const index = scene.layers[layer()].drawingAt(frame()); index >= 0 && index < scene.layers[layer()].drawings.size())
            for (Picture const& p : scene.layers[layer()].drawings[index].pictures)
                if (!p.image.isNull()) {
                    preview = p.image;
                    break;
                }
    VectorizeDialog dialog(scene, vectorizeOptionsLine(), preview, this);
    dialog.setWindowTitle(tr("Vectorize Line Art in Selected Drawings"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/vectorizeOptions"), dialog.line());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(tr("Vectorizing Drawings..."));
    QString note;
    int const made = vectorizeSelectedDrawings(dialog.line(), &note);
    QApplication::restoreOverrideCursor();
    if (made <= 0)
        QMessageBox::information(this, tr("Vectorize Drawings"), note.isEmpty() ? tr("Vectorization failed.") : note);
    else
        statusBar()->showMessage(tr("%n drawing(s) vectorized.", nullptr, made) + (note.isEmpty() ? QString() : QLatin1Char(' ') + note), 8000);
}

void MainWindow::onActionVectorizationParameters()
{
    // The settings kept for vectorising (imports, scans and Vectorize Line Art).
    VectorizeDialog dialog(m_document->scene(), vectorizeOptionsLine(), QImage(), this);
    if (dialog.exec() == QDialog::Accepted)
        QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/vectorizeOptions"), dialog.line());
}
