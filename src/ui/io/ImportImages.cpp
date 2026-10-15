// Eclipse - bringing in pictures: as drawings, traced or as they are.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "model/geometry/Vectorize.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/io/SwfImport.h"
#include "ui/io/FlaImport.h"

int MainWindow::importSwf(QString const& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return -1;
    }
    SwfImport::Movie movie;
    if (!SwfImport::read(file.readAll(), movie, error))
        return -1;
    // Made to fit the frame of the scene, in the middle of it.
    Scene const& scene = m_document->scene();
    double const scale = std::min(double(scene.width) / movie.size.width(), double(scene.height) / movie.size.height());
    QTransform const place = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - movie.size.width() * scale) / 2.0, (scene.height - movie.size.height() * scale) / 2.0);
    QVector<Drawing> drawings = movie.frames;
    for (Drawing& d : drawings) {
        for (Fill& fill : d.fills)
            fill.transform(place);
        for (Stroke& stroke : d.strokes)
            stroke.transform(place);        // (which scales the line's thickness with it)
    }
    int const at = m_document->importDrawings(-1, QFileInfo(path).completeBaseName(), 0, drawings, 1);
    if (at < 0 && error)
        *error = tr("The movie's frames could not be put on a layer.");
    if (at >= 0)
        m_timeline->setCurrentCell(at, 0);
    return at;
}

int MainWindow::importFla(QString const& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return -1;
    }
    FlaImport::Document fla;
    if (!FlaImport::read(file.readAll(), fla, error))
        return -1;
    Scene const& scene = m_document->scene();
    double const scale = std::min(double(scene.width) / std::max(1, fla.size.width()), double(scene.height) / std::max(1, fla.size.height()));
    QTransform const place = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - fla.size.width() * scale) / 2.0, (scene.height - fla.size.height() * scale) / 2.0);
    m_document->beginMacro(tr("Import FLA"));
    if (scene.frameCount() < fla.frames)
        m_document->setFrameCount(fla.frames);
    // Each layer, from the back up; then the masks are given to the layers they cut.
    QVector<int> ids;
    int above = int(m_document->scene().layers.size()) - 1;
    for (FlaImport::Layer const& in : std::as_const(fla.layers)) {
        int const at = m_document->addLayer(above);
        if (at < 0) {
            ids.push_back(0);
            continue;
        }
        above = at;
        QVector<Drawing> drawings = in.drawings;
        for (Drawing& d : drawings) {
            for (Fill& fill : d.fills)
                fill.transform(place);
            for (Stroke& stroke : d.strokes)
                stroke.transform(place);
            for (Picture& picture : d.pictures)
                picture.transform(place);
        }
        int const frames = m_document->scene().frameCount();
        QString const name = in.name;
        bool const shown = in.visible && !in.mask;
        QVector<int> cells = in.cells;
        m_document->editLayers({at}, tr("Import FLA"), [&](Layer& layer) {
            layer.name = name;
            layer.drawings = drawings;
            layer.cells.fill(Layer::kNoDrawing, frames);
            for (int f = 0; f < std::min(int(cells.size()), frames); ++f)
                layer.cells[f] = cells[f];
            layer.visible = shown;
            for (Drawing& d : layer.drawings)
                d.pinned = true;
        });
        ids.push_back(m_document->scene().layers[at].id);
    }
    for (int i = 0; i < fla.layers.size(); ++i) {
        int const by = fla.layers[i].maskedBy;
        int const at = m_document->scene().layerIndex(ids[i]);
        if (by < 0 || at < 0)
            continue;
        Effect cut = Effect::made(Effect::Cutter);
        cut.matte = ids[by];
        m_document->editLayers({at}, tr("Import FLA"), [&](Layer& layer) { layer.effects.push_back(cut); });
    }
    m_document->endMacro();
    int const top = m_document->scene().layerIndex(ids.isEmpty() ? 0 : ids.last());
    if (top >= 0)
        m_timeline->setCurrentCell(top, 0);
    if (error && (fla.skippedText > 0 || fla.skippedPictures > 0))
        *error = tr("Left out: text and bitmaps (not read).");
    return int(fla.layers.size());
}

void MainWindow::onActionImportFla()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import FLA"), QString(), tr("Flash Document (*.fla)"));
    if (path.isEmpty())
        return;
    QString note;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const made = importFla(path, &note);
    QApplication::restoreOverrideCursor();
    if (made < 0) {
        QMessageBox::warning(this, tr("Import FLA"), note);
        return;
    }
    statusBar()->showMessage(tr("%1: %n layer(s), frame by frame.", nullptr, made).arg(QFileInfo(path).fileName()) + (note.isEmpty() ? QString() : QLatin1Char(' ') + note), 10000);
}

void MainWindow::onActionImportSwf()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import SWF"), QString(), tr("Flash Movie (*.swf)"));
    if (path.isEmpty())
        return;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const at = importSwf(path, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0) {
        QMessageBox::warning(this, tr("Import SWF"), error);
        return;
    }
    statusBar()->showMessage(tr("%1: a drawing for each frame of the movie, on the layer %2.").arg(QFileInfo(path).fileName(), m_document->scene().layers[at].name), 7000);
}

int MainWindow::importImages(ImageImport const& options, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return -1;
    };
    Scene const& scene = m_document->scene();
    if (options.files.isEmpty())
        return fail(tr("No file was chosen."));
    int const target = options.newLayer ? -1 : layer();
    if (target >= 0 && !scene.layers[target].holdsDrawings())
        return fail(tr("The current layer does not hold drawings. Choose a drawing layer, or import to a new layer."));

    QVector<Drawing> drawings;
    for (QString const& file : options.files) {
        QImageReader reader(file);
        reader.setAutoTransform(true);      // a photograph taken with the camera on its side
        QImage image = reader.read();
        if (image.isNull())
            return fail(tr("Could not read %1:\n%2").arg(QDir::toNativeSeparators(file), reader.errorString()));
        // In the middle of the frame: made to fit it, or at its own size.
        // (Pan: made to fill it, the longer way hanging over the edges for the camera to pan across.)
        double const across = double(scene.width) / image.width(), down = double(scene.height) / image.height();
        double const scale = !options.fit ? 1.0 : options.pan ? std::max(across, down) : std::min(across, down);
        QTransform placement = QTransform::fromScale(scale, scale);
        placement *= QTransform::fromTranslate((scene.width - image.width() * scale) / 2.0, (scene.height - image.height() * scale) / 2.0);
        Drawing drawing;
        // How far a traced shape's curves may stray from the edge of the picture (scene units): about a pixel of the picture,
        // so that the steps of its pixels are smoothed over rather than taken for corners.
        double const kTraceTolerance = std::max(0.75, 1.25 * scale);
        if (options.vectorize && options.colours > 0) {
            // In colour: a painted shape for the area of each colour, the largest underneath.
            for (Vectorize::ColourShape const& area : Vectorize::traceColours(image, options.colours, options.paper)) {
                Fill shape;
                shape.color = area.colour;
                for (QPolygonF const& outline : area.outlines)
                    shape.outlines.push_back(placement.map(outline));
                shape.giveContours(kTraceTolerance);        // shapes with points, as the brush makes
                drawing.fills.push_back(shape);
            }
        } else if (options.vectorize && options.centreLines) {
            // Lines as lines: each stroke of the picture a pencil line down its middle, as thick as the stroke was.
            for (Vectorize::CentreLine const& traced : Vectorize::centreLines(image, options.threshold)) {
                Stroke line;
                line.swatch = m_palette->currentSwatch();
                line.color = scene.resolve(line.swatch, QColor(Qt::black));
                line.width = float(std::max(1.0, traced.width * scale));
                line.closed = traced.closed;
                for (QPointF const& point : traced.points)
                    line.points.push_back({placement.map(point), 1.0f});
                if (line.points.size() >= 2)
                    drawing.strokes.push_back(line);
            }
        } else if (options.vectorize) {
            Fill shape;
            shape.swatch = m_palette->currentSwatch();
            shape.color = scene.resolve(shape.swatch, QColor(Qt::black));
            for (QPolygonF const& outline : Vectorize::trace(image, options.threshold))
                shape.outlines.push_back(placement.map(outline));
            shape.giveContours(kTraceTolerance);
            if (!shape.outlines.isEmpty())
                drawing.fills.push_back(shape);
        } else {
            Picture picture;
            picture.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            picture.file = QFileInfo(file).absoluteFilePath();
            picture.linked = options.linked;
            picture.size = image.size();
            picture.placement = placement;
            drawing.pictures.push_back(picture);
        }
        drawings.push_back(drawing);
    }
    QString const name = options.layerName.trimmed().isEmpty() ? QFileInfo(options.files.first()).completeBaseName() : options.layerName.trimmed();
    int const at = m_document->importDrawings(target, name, options.newLayer ? 0 : frame(), drawings, options.hold);
    if (at < 0)
        return fail(tr("The pictures could not be put on that layer."));
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, options.newLayer ? 0 : frame());
    return at;
}

void MainWindow::askImportImages(bool linked)
{
    QDialog dialog(this);
    dialog.setWindowTitle(linked ? tr("Link to Images") : tr("Import Images"));
    dialog.setObjectName(QStringLiteral("ImportImagesDialog"));

    QGroupBox* const filesBox = new QGroupBox(tr("Files"), &dialog);
    QListWidget* const files = new QListWidget(filesBox);
    files->setMinimumSize(380, 110);
    QPushButton* const browse = new QPushButton(tr("Browse..."), filesBox);
    QVBoxLayout* const filesLayout = new QVBoxLayout(filesBox);
    filesLayout->addWidget(files);
    filesLayout->addWidget(browse, 0, Qt::AlignRight);

    QGroupBox* const layerBox = new QGroupBox(tr("Layer"), &dialog);
    QRadioButton* const newLayer = new QRadioButton(tr("Create Single Layer Named:"), layerBox);
    QLineEdit* const layerName = new QLineEdit(layerBox);
    layerName->setPlaceholderText(tr("the name of the first file"));
    QRadioButton* const currentLayer = new QRadioButton(tr("Add to the Current Layer, from the Current Frame"), layerBox);
    currentLayer->setEnabled(m_document->scene().layers[layer()].holdsDrawings());
    newLayer->setChecked(true);
    QSpinBox* const hold = new QSpinBox(layerBox);
    hold->setRange(1, 1000);
    QHBoxLayout* const nameRow = new QHBoxLayout;
    nameRow->addWidget(newLayer);
    nameRow->addWidget(layerName, 1);
    QFormLayout* const layerLayout = new QFormLayout(layerBox);
    layerLayout->addRow(nameRow);
    layerLayout->addRow(currentLayer);
    layerLayout->addRow(tr("Frames for each image"), hold);

    QGroupBox* const alignBox = new QGroupBox(tr("Alignment"), &dialog);
    QComboBox* const alignment = new QComboBox(alignBox);
    alignment->setObjectName(QStringLiteral("ImportAlignment"));
    alignment->addItems({tr("Fit"), tr("Pan"), tr("Project Resolution (Actual Size)")});
    alignment->setToolTip(tr("Fit: the whole picture inside the frame.\nPan: the picture fills the frame; what does not fit hangs over the edges, for the camera to pan across.\n"
                             "Project Resolution: a pixel of the picture is a pixel of the scene."));
    QFormLayout* const alignLayout = new QFormLayout(alignBox);
    alignLayout->addRow(tr("Rule"), alignment);

    QGroupBox* const asBox = new QGroupBox(tr("Import As"), &dialog);
    QRadioButton* const bitmap = new QRadioButton(tr("Keep As Original Bitmap"), asBox);
    QRadioButton* const vector = new QRadioButton(tr("Vector Drawing (Black and White)"), asBox);
    QRadioButton* const centre = new QRadioButton(tr("Vector Drawing (Centre Lines)"), asBox);
    centre->setObjectName(QStringLiteral("ImportCentreLines"));
    centre->setToolTip(tr("Each stroke of the picture becomes a pencil line down its middle, as thick as the stroke was - lines you can reshape,\n"
                          "not shapes round them. For clean line drawings."));
    QRadioButton* const colourVector = new QRadioButton(tr("Vector Drawing (Colour)"), asBox);
    QSpinBox* const colourCount = new QSpinBox(asBox);
    colourCount->setRange(2, 64);
    colourCount->setValue(8);
    colourCount->setEnabled(false);
    connect(colourVector, &QRadioButton::toggled, colourCount, &QSpinBox::setEnabled);
    bitmap->setChecked(true);
    vector->setEnabled(!linked);    // a traced picture is artwork of the scene, not a file to read again
    colourVector->setEnabled(!linked);
    centre->setEnabled(!linked);
    QSpinBox* const threshold = new QSpinBox(asBox);
    threshold->setRange(1, 255);
    threshold->setValue(128);
    threshold->setEnabled(false);
    auto thresholdUsed = [vector, centre, threshold] { threshold->setEnabled(vector->isChecked() || centre->isChecked()); };
    connect(vector, &QRadioButton::toggled, threshold, thresholdUsed);
    connect(centre, &QRadioButton::toggled, threshold, thresholdUsed);
    QFormLayout* const asLayout = new QFormLayout(asBox);
    asLayout->addRow(bitmap);
    asLayout->addRow(vector);
    asLayout->addRow(centre);
    asLayout->addRow(tr("Threshold (darker than this is line)"), threshold);
    asLayout->addRow(colourVector);
    asLayout->addRow(tr("Colours at most"), colourCount);
    // The colour of the paper: left out, so that what is traced is the drawing and not the sheet it is on.
    QCheckBox* const leavePaper = new QCheckBox(tr("Leave out the paper, of this colour:"), asBox);
    leavePaper->setObjectName(QStringLiteral("ImportLeavePaper"));
    leavePaper->setChecked(true);
    leavePaper->setEnabled(false);
    QPushButton* const paper = new QPushButton(asBox);
    paper->setObjectName(QStringLiteral("ImportPaper"));
    paper->setEnabled(false);
    auto showPaper = [paper](QColor const& colour) {
        paper->setProperty("colour", colour);
        paper->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
    };
    showPaper(QColor(255, 255, 255));
    connect(paper, &QPushButton::clicked, &dialog, [paper, showPaper, &dialog] {
        QColor const chosen = QColorDialog::getColor(paper->property("colour").value<QColor>(), &dialog, tr("Paper"));
        if (chosen.isValid())
            showPaper(chosen);
    });
    connect(colourVector, &QRadioButton::toggled, leavePaper, &QCheckBox::setEnabled);
    connect(colourVector, &QRadioButton::toggled, paper, &QPushButton::setEnabled);
    asLayout->addRow(leavePaper, paper);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QString const startFolder = m_path.isEmpty() ? QString() : QFileInfo(m_path).path();
    auto choose = [&] {
        QStringList chosen = QFileDialog::getOpenFileNames(&dialog, dialog.windowTitle(), startFolder, kImageFilter);
        if (chosen.isEmpty())
            return;
        chosen.sort(Qt::CaseInsensitive);   // a numbered run of files in its order
        files->clear();
        files->addItems(chosen);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    };
    connect(browse, &QPushButton::clicked, &dialog, choose);

    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(filesBox);
    layout->addWidget(layerBox);
    layout->addWidget(alignBox);
    layout->addWidget(asBox);
    layout->addWidget(buttons);
    choose();       // straight to the files, as the first thing asked
    if (files->count() == 0 || dialog.exec() != QDialog::Accepted)
        return;

    ImageImport options;
    for (int i = 0; i < files->count(); ++i)
        options.files << files->item(i)->text();
    options.newLayer = newLayer->isChecked();
    options.layerName = layerName->text();
    options.fit = alignment->currentIndex() != 2;
    options.pan = alignment->currentIndex() == 1;
    options.paper = leavePaper->isChecked() ? paper->property("colour").value<QColor>() : QColor();
    options.vectorize = vector->isChecked() || colourVector->isChecked() || centre->isChecked();
    options.centreLines = centre->isChecked();
    options.colours = colourVector->isChecked() ? colourCount->value() : 0;
    options.threshold = threshold->value();
    options.linked = linked;
    options.hold = hold->value();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    int const at = importImages(options, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0)
        QMessageBox::critical(this, tr("Eclipse"), error);
    else
        statusBar()->showMessage(tr("%n image(s) imported.", nullptr, int(options.files.size())), 5000);
}
