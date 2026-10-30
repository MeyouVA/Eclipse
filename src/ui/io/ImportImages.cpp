// Eclipse - importing: Flash movies and documents, 3D models, from a scanner.
#include "ui/window/MainWindow.h"
#include <QColorSpace>
#include <QStatusBar>
#include "colour/images/ColourTransform.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
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
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/io/Scanner.h"
#include "model/geometry/Model3D.h"
#include "model/geometry/Vectorize.h"
#include "canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"
#include "formats/flash/SwfImport.h"
#include "formats/flash/FlaImport.h"
#include "actions/theme/AppSettings.h"

class ImportProgress {
public:
    ImportProgress(QWidget* parent, QString const& label) : m_dialog(label, QCoreApplication::translate("MainWindow", "Cancel"), 0, 1000, parent)
    {
        m_dialog.setWindowModality(Qt::WindowModal);
        m_dialog.setMinimumDuration(600);
        m_dialog.setAutoClose(false);
        m_dialog.setAutoReset(false);
        m_clock.start();
    }
    SwfImport::Progress callback()
    {
        return [this](qint64 done, qint64 all) {
            // (Not more often than a few times a second: the reading is what matters.)
            if (m_clock.elapsed() - m_last >= 50) {
                m_last = m_clock.elapsed();
                m_dialog.setValue(all > 0 ? int(std::clamp<qint64>(done * 1000 / all, 0, 999)) : 0);
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                QCoreApplication::processEvents();
            }
            return !m_dialog.wasCanceled();
        };
    }

private:
    QProgressDialog m_dialog;
    QElapsedTimer m_clock;
    qint64 m_last = -1000;
};

static QString importedMediaFolder(QString const& scenePath, QString const& source)
{
    QString base;
    if (!scenePath.isEmpty()) {
        QFileInfo const scene(scenePath);
        base = scene.dir().filePath(scene.completeBaseName() + QStringLiteral("-media"));
    } else {
        base = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Imported Media"));
    }
    return QDir(base).filePath(QFileInfo(source).completeBaseName());
}

static QString saveImportedMedia(QString const& scenePath, QString const& source, QString const& name, QByteArray const& bytes)
{
    QDir folder(importedMediaFolder(scenePath, source));
    if (!folder.mkpath(QStringLiteral(".")))
        return QString();
    QFile file(folder.filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return QString();
    return file.fileName();
}

QString MainWindow::importSwfToLibrary(QString const& path, QString* error, bool groups)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return QString();
    }
    SwfImport::Movie movie;
    {
        ImportProgress shown(this, tr("Reading %1...").arg(QFileInfo(path).fileName()));
        if (!SwfImport::read(file.readAll(), movie, error, importedMediaFolder(m_path, path), shown.callback(), SwfImport::Layout::Tracks))
            return QString();
    }
    Scene const& scene = m_document->scene();
    QString const name = QFileInfo(path).completeBaseName();
    Scene const piece = SwfImport::rig(movie, name, QSize(scene.width, scene.height), groups);
    QDir const folder(m_library ? m_library->currentFolder() : LibraryView::defaultFolder());
    QString target = folder.filePath(name + QLatin1Char('.') + LibraryView::kSuffix);
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = folder.filePath(QStringLiteral("%1 %2.%3").arg(name).arg(n).arg(LibraryView::kSuffix));
    if (!QDir().mkpath(folder.absolutePath()) || !piece.save(target, error))
        return QString();
    if (m_library) {
        m_library->refresh();
        m_library->setCurrentTemplate(target);
    }
    return target;
}

int MainWindow::importSwf(QString const& path, QString* error, bool asLayers, int* layers, bool groups)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return -1;
    }
    SwfImport::Movie movie;
    {
        ImportProgress shown(this, tr("Reading %1...").arg(QFileInfo(path).fileName()));
        if (!SwfImport::read(file.readAll(), movie, error, importedMediaFolder(m_path, path), shown.callback(),
                             asLayers ? SwfImport::Layout::Tracks : SwfImport::Layout::Frames))
            return -1;
    }
    if (layers)
        *layers = int(movie.tracks.size());
    // Made to fit the frame of the scene, in the middle of it.
    Scene const& scene = m_document->scene();
    double const scale = std::min(double(scene.width) / movie.size.width(), double(scene.height) / movie.size.height());
    QTransform const place = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - movie.size.width() * scale) / 2.0, (scene.height - movie.size.height() * scale) / 2.0);
    m_document->beginMacro(tr("Import SWF"));
    int at = -1;
    if (asLayers) {
        // A template made of it: a peg over it all, a drawing layer on a peg for each depth; the movie's colour behind everything.
        Scene const piece = SwfImport::rig(movie, QFileInfo(path).completeBaseName(), QSize(scene.width, scene.height), groups);
        int const first = m_document->insertTemplate(piece, 0);
        if (first >= 0) {
            at = int(m_document->scene().layers.size()) - 1;
            if (m_document->scene().layers[first].kind == Layer::ColourCard && first > 0)
                m_document->moveLayerBeside(first, 0, true);
        }
    } else {
        // Its gradients as swatches of the palette.
        QVector<int> swatches;
        for (Swatch const& gradient : std::as_const(movie.gradients)) {
            int const id = m_document->addSwatch(gradient.name, gradient.color);
            m_document->setSwatchGradient(id, gradient.kind, gradient.stops);
            swatches.push_back(id);
        }
        QVector<Drawing> drawings = movie.frames;
        for (Drawing& d : drawings) {
            for (Fill& fill : d.fills) {
                fill.transform(place);
                fill.swatch = fill.swatch >= 0 && fill.swatch < swatches.size() ? swatches[fill.swatch] : -1;
            }
            for (Stroke& stroke : d.strokes)
                stroke.transform(place);        // (which scales the line's thickness with it)
            for (Picture& picture : d.pictures)
                picture.transform(place);
        }
        at = m_document->importDrawings(-1, QFileInfo(path).completeBaseName(), 0, drawings, 1);
    }
    int const id = at < 0 ? 0 : m_document->scene().layers[at].id;
    if (at >= 0) {
        // Its frame labels as markers of the scene, its sounds on sound layers of their own.
        for (SwfImport::Label const& label : std::as_const(movie.labels))
            if (m_document->scene().markerAt(label.frame) < 0)
                m_document->addMarker(label.frame, 1, label.name, QColor(60, 190, 205));
        int lost = movie.skippedSounds;
        for (int i = 0; i < movie.sounds.size(); ++i) {
            SwfImport::Sound const& sound = movie.sounds[i];
            QString const saved = saveImportedMedia(m_path, path, QStringLiteral("sound %1.%2").arg(i + 1).arg(sound.suffix), sound.bytes);
            if (saved.isEmpty())
                ++lost;
            else
                m_document->addSound(saved, sound.frame);
        }
        QStringList left;
        if (lost > 0)
            left << tr("%n sound(s)", nullptr, lost);
        if (movie.skippedVideo > 0)
            left << tr("%n video(s) (%1)", nullptr, movie.skippedVideo).arg(movie.videoProblem);
        if (movie.scripts > 0)
            left << tr("%n piece(s) of ActionScript (not run)", nullptr, movie.scripts);
        if (error && !left.isEmpty())
            *error = tr("Left out: %1.").arg(left.join(QStringLiteral(", ")));
    }
    m_document->endMacro();
    int const layerAt = at < 0 ? -1 : m_document->scene().layerIndex(id);
    if (at < 0 && error)
        *error = tr("The movie's frames could not be put on a layer.");
    if (layerAt >= 0)
        m_timeline->setCurrentCell(layerAt, 0);
    return layerAt;
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
    {
        ImportProgress shown(this, tr("Reading %1...").arg(QFileInfo(path).fileName()));
        if (!FlaImport::read(file.readAll(), fla, error, importedMediaFolder(m_path, path), shown.callback()))
            return -1;
    }
    Scene const& scene = m_document->scene();
    double const scale = std::min(double(scene.width) / std::max(1, fla.size.width()), double(scene.height) / std::max(1, fla.size.height()));
    QTransform const place = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - fla.size.width() * scale) / 2.0, (scene.height - fla.size.height() * scale) / 2.0);
    m_document->beginMacro(tr("Import FLA"));
    if (scene.frameCount() < fla.frames)
        m_document->setFrameCount(fla.frames);
    // Its gradients as swatches of the palette.
    QVector<int> swatches;
    for (Swatch const& gradient : std::as_const(fla.gradients)) {
        int const id = m_document->addSwatch(gradient.name, gradient.color);
        m_document->setSwatchGradient(id, gradient.kind, gradient.stops);
        swatches.push_back(id);
    }
    auto swatchOf = [&](int index) { return index >= 0 && index < swatches.size() ? swatches[index] : -1; };
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
            for (Fill& fill : d.fills) {
                fill.transform(place);
                fill.swatch = swatchOf(fill.swatch);
            }
            for (Stroke& stroke : d.strokes) {
                stroke.transform(place);
                stroke.swatch = swatchOf(stroke.swatch);
            }
            for (Picture& picture : d.pictures)
                picture.transform(place);
        }
        int const frames = m_document->scene().frameCount();
        QString const name = in.name;
        bool const shown = in.visible && !in.mask;
        bool const locked = in.locked;
        QColor const colour = in.colour;
        QString const blend = in.blend;
        QVector<FlaImport::Filter> const filters = in.filters;
        QVector<int> cells = in.cells;
        m_document->editLayers({at}, tr("Import FLA"), [&](Layer& layer) {
            layer.name = name;
            layer.drawings = drawings;
            layer.cells.fill(Layer::kNoDrawing, frames);
            for (int f = 0; f < std::min(int(cells.size()), frames); ++f)
                layer.cells[f] = cells[f];
            layer.visible = shown;
            layer.locked = locked;
            Flash::applyLook(layer, blend, filters, scale);
            if (colour.isValid())
                layer.trackColor = colour;
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
    for (FlaImport::Label const& label : std::as_const(fla.labels)) {
        if (m_document->scene().markerAt(label.frame) >= 0)
            continue;
        int const marker = m_document->addMarker(label.frame, label.length, label.name, label.comment ? QColor(150, 150, 150) : QColor(60, 190, 205));
        if (marker >= 0 && label.comment)
            m_document->setMarkerNote(marker, label.note.isEmpty() ? label.name : label.note);
    }
    // Its swatches as a palette of their own (the palette starts with a colour: that becomes the first of them).
    if (!fla.swatches.isEmpty()) {
        int const palette = m_document->addPalette(tr("%1 (Flash swatches)").arg(QFileInfo(path).completeBaseName()));
        if (palette >= 0) {
            int first = -1;
            for (Swatch const& s : m_document->scene().palette)
                if (s.palette == palette && first < 0)
                    first = s.id;
            for (int i = 0; i < fla.swatches.size(); ++i) {
                QString const name = fla.swatches[i].name(fla.swatches[i].alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
                if (i == 0 && first >= 0)
                    m_document->editSwatch(first, name, fla.swatches[i]);
                else
                    m_document->addSwatch(name, fla.swatches[i], palette);
            }
            for (Swatch const& gradient : std::as_const(fla.swatchGradients)) {
                int const id = m_document->addSwatch(gradient.name, gradient.color, palette);
                m_document->setSwatchGradient(id, gradient.kind, gradient.stops);
            }
        }
    }
    QHash<QString, QString> written;
    int lost = fla.skippedSounds;
    for (FlaImport::Sound const& sound : std::as_const(fla.sounds)) {
        QString saved = written.value(sound.name);
        if (saved.isEmpty()) {
            QString name = sound.name;
            name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
            if (!name.endsWith(QLatin1Char('.') + sound.suffix, Qt::CaseInsensitive))
                name += QLatin1Char('.') + sound.suffix;
            saved = saveImportedMedia(m_path, path, name, sound.bytes);
            written.insert(sound.name, saved);
        }
        if (saved.isEmpty())
            ++lost;
        else
            m_document->addSound(saved, sound.frame);
    }
    m_document->endMacro();
    int const top = m_document->scene().layerIndex(ids.isEmpty() ? 0 : ids.last());
    if (top >= 0)
        m_timeline->setCurrentCell(top, 0);
    QStringList left;
    if (fla.skippedVideo > 0)
        left << tr("%n video(s) (%1)", nullptr, fla.skippedVideo).arg(fla.videoProblem);
    if (lost > 0)
        left << tr("%n sound(s)", nullptr, lost);
    if (fla.skippedPictures > 0)
        left << tr("%n bitmap(s) that could not be read", nullptr, fla.skippedPictures);
    if (error && !left.isEmpty())
        *error = tr("Left out: %1.").arg(left.join(QStringLiteral(", ")));
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
    // How to lay it out: a layer on a peg for each depth (to animate further), or the frames as they look.
    QDialog ask(this);
    ask.setWindowTitle(tr("Import SWF"));
    auto* column = new QVBoxLayout(&ask);
    auto* rigged = new QRadioButton(tr("A layer for each depth of the movie, moved by a peg of its own"), &ask);
    auto* flat = new QRadioButton(tr("One layer, a drawing for each frame"), &ask);
    auto* grouped = new QCheckBox(tr("Each layer and its peg in a group"), &ask);
    auto* library = new QCheckBox(tr("Into the Library as a template, not into the scene"), &ask);
    rigged->setObjectName(QStringLiteral("SwfAsLayers"));
    flat->setObjectName(QStringLiteral("SwfAsFrames"));
    bool const wasLayers = settings().value(QStringLiteral("import/swfAsLayers"), true).toBool();
    (wasLayers ? rigged : flat)->setChecked(true);
    grouped->setChecked(settings().value(QStringLiteral("import/swfGroups"), false).toBool());
    library->setChecked(settings().value(QStringLiteral("import/swfToLibrary"), false).toBool());
    auto* rigOptions = new QWidget(&ask);
    auto* rigColumn = new QVBoxLayout(rigOptions);
    rigColumn->setContentsMargins(20, 0, 0, 0);
    rigColumn->addWidget(grouped);
    rigColumn->addWidget(library);
    rigOptions->setEnabled(wasLayers);
    connect(rigged, &QRadioButton::toggled, rigOptions, &QWidget::setEnabled);
    column->addWidget(rigged);
    column->addWidget(rigOptions);
    column->addWidget(flat);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    column->addWidget(buttons);
    if (ask.exec() != QDialog::Accepted)
        return;
    bool const asLayers = rigged->isChecked();
    settings().setValue(QStringLiteral("import/swfAsLayers"), asLayers);
    settings().setValue(QStringLiteral("import/swfGroups"), grouped->isChecked());
    settings().setValue(QStringLiteral("import/swfToLibrary"), library->isChecked());
    QString error;
    if (asLayers && library->isChecked()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString const saved = importSwfToLibrary(path, &error, grouped->isChecked());
        QApplication::restoreOverrideCursor();
        if (saved.isEmpty())
            QMessageBox::warning(this, tr("Import SWF"), error);
        else
            statusBar()->showMessage(tr("%1: kept in the Library as %2.").arg(QFileInfo(path).fileName(), QFileInfo(saved).completeBaseName()), 10000);
        return;
    }
    int tracks = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const at = importSwf(path, &error, asLayers, &tracks, grouped->isChecked());
    QApplication::restoreOverrideCursor();
    if (at < 0) {
        QMessageBox::warning(this, tr("Import SWF"), error);
        return;
    }
    QString const done = asLayers ? tr("%1: %n layer(s), each on a peg of its own, under the peg %2.", nullptr, tracks)
                                  : tr("%1: a drawing for each frame of the movie, on the layer %2.");
    statusBar()->showMessage(done.arg(QFileInfo(path).fileName(), m_document->scene().layers[at].name)
                                 + (error.isEmpty() ? QString() : QLatin1Char(' ') + error),
                             10000);
}

// A picture's colours as its file stores them under its see-through parts (Preferences > Node View), made straight.
// 1: multiplied by the opacity (over black); 2: mixed with white by the opacity; 3: as 1, a colour above its opacity held down to it.
static QImage readTransparency(QImage const& read, int kind)
{
    if (kind <= 0 || kind > 3 || !read.hasAlphaChannel())
        return read;
    QImage image = read.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(line[x]);
            if (a == 0 || a == 255)
                continue;
            int rgb[3] = {qRed(line[x]), qGreen(line[x]), qBlue(line[x])};
            for (int& c : rgb)
                c = std::clamp(kind == 2 ? (c - (255 - a)) * 255 / a : std::min(c, kind == 3 ? a : 255) * 255 / a, 0, 255);
            line[x] = qRgba(rgb[0], rgb[1], rgb[2], a);
        }
    }
    return image;
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
    // Convert Image Colour Space on Import: the space the pictures are read in (the file's own profile is taken to sRGB first).
    QString const fromSpace = options.convertColour ? (options.sourceSpace.isEmpty() ? QStringLiteral("sRGB") : options.sourceSpace) : QString();
    QString const layerSpace = target >= 0 ? (scene.layers[target].colourSpace.isEmpty() ? scene.workingSpace : scene.layers[target].colourSpace) : QString();
    bool unconverted = false;
    if (!fromSpace.isEmpty() && !Cms::config()->space(fromSpace))
        unconverted = true;
    for (QString const& file : options.files) {
        QImageReader reader(file);
        reader.setAutoTransform(true);      // a photograph taken with the camera on its side
        QImage image = reader.read();
        if (image.isNull())
            return fail(tr("Could not read %1:\n%2").arg(QDir::toNativeSeparators(file), reader.errorString()));
        if (options.convertColour && !unconverted) {
            // The file's own profile: to sRGB now (a linked picture is read through it again each time its file is read).
            if (options.sourceSpace.isEmpty() && image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb))
                image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
            // Onto an existing layer: the pixels put in that layer's colour space now (a linked one keeps its own colour space instead).
            if (target >= 0 && !options.linked && !options.vectorize)
                Cms::between(fromSpace, layerSpace).apply(&image);
        }
        image = readTransparency(image, QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/importTransparency"), 0).toInt());
        double const across = double(scene.width) / image.width(), down = double(scene.height) / image.height();
        double const scale = !options.fit ? 1.0 : options.pan ? std::max(across, down) : std::min(across, down);
        QTransform placement = QTransform::fromScale(scale, scale);
        placement *= QTransform::fromTranslate((scene.width - image.width() * scale) / 2.0, (scene.height - image.height() * scale) / 2.0);
        Drawing drawing;
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
            if (options.linked && options.convertColour && !unconverted) {
                picture.ownProfile = options.sourceSpace.isEmpty();
                if (target >= 0 && fromSpace.compare(layerSpace, Qt::CaseInsensitive) != 0)
                    picture.colourSpace = fromSpace;
            }
            picture.size = image.size();
            picture.placement = placement;
            drawing.pictures.push_back(picture);
        }
        drawings.push_back(drawing);
    }
    QString const name = options.layerName.trimmed().isEmpty() ? QFileInfo(options.files.first()).completeBaseName() : options.layerName.trimmed();
    m_document->beginMacro(tr("Import Images"));
    int const at = m_document->importDrawings(target, name, options.newLayer ? 0 : frame(), drawings, options.hold);
    // A new layer reads its pictures in their source colour space from now on (Layer Properties: Colour Space).
    if (at >= 0 && target < 0 && !fromSpace.isEmpty() && !unconverted && !options.vectorize)
        m_document->editLayers({at}, tr("Colour Space"), [fromSpace](Layer& l) { l.colourSpace = fromSpace; });
    m_document->endMacro();
    if (at < 0)
        return fail(tr("The pictures could not be put on that layer."));
    if (unconverted)
        statusBar()->showMessage(tr("Unsupported colour space conversion for this import. The colour correction has not been applied."), 8000);
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, options.newLayer ? 0 : frame());
    return at;
}

void MainWindow::onActionImport3DModel()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Import 3D Models"), QString(), tr("3D Models (*.obj)"));
    if (files.isEmpty())
        return;
    files.sort(Qt::CaseInsensitive);    // a numbered run of them, in its order
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const at = import3DModels(files, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0) {
        QMessageBox::warning(this, tr("Import 3D Models"), error);
        return;
    }
    m_timeline->setCurrentCell(at, frame());
    statusBar()->showMessage(tr("%1: a 3D model layer - turn it in space with the Rotate tool's X and Y angles (or the Timeline's).")
                                 .arg(QFileInfo(files.first()).fileName()),
                             10000);
}

int MainWindow::import3DModels(QStringList const& files, QString* error)
{
    QVector<Drawing> drawings;
    for (QString const& file : files) {
        QString why;
        if (!Model3D::load(file, &why)) {
            if (error)
                *error = tr("%1: %2").arg(QFileInfo(file).fileName(), why);
            return -1;
        }
        Drawing d;
        d.model = QFileInfo(file).absoluteFilePath();
        d.name = QFileInfo(file).completeBaseName();
        // (In the middle of the frame.)
        d.modelCentre = QPointF(m_document->scene().width / 2.0, m_document->scene().height / 2.0);
        // (Preferences: how big a model comes in.)
        d.modelScale = std::clamp(QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/modelScale"), 1.0).toDouble(), 0.01, 100.0);
        drawings.push_back(d);
    }
    // One model: shown the length of the scene.  Several: one a frame, from the start.
    int const hold = drawings.size() == 1 ? std::max(1, m_document->scene().frameCount()) : 1;
    return m_document->importDrawings(-1, QFileInfo(files.first()).completeBaseName(), 0, drawings, hold);
}

void MainWindow::onActionImportScanner()
{
    // The scans kept with the pictures, where they can be found again.
    QString const folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Eclipse Scans");
    QString error;
    QStringList const scanned = Scanner::acquire(this, folder, &error);
    if (scanned.isEmpty()) {
        if (!error.isEmpty())
            QMessageBox::information(this, tr("From Scanner"), error);
        return;
    }
    askImportImages(false, scanned);
}
