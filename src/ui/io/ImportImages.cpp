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
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/io/SwfImport.h"
#include "ui/io/FlaImport.h"

// A sound (or other media) that came inside an imported file, written out as a file of its own: into the folder kept
// beside the scene (<scene>-media/<what it came from>/), or, for a scene not saved yet, into Eclipse's own folder of
// imported media.  Empty when it could not be written.
// While a long import is read: how far it has come, and a button to stop it.  (Shown only once the reading has taken a
// moment, and kept alive meanwhile, so that the window never seems to hang.)
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

int MainWindow::importSwf(QString const& path, QString* error)
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
        if (!SwfImport::read(file.readAll(), movie, error, importedMediaFolder(m_path, path), shown.callback()))
            return -1;
    }
    // Made to fit the frame of the scene, in the middle of it.
    Scene const& scene = m_document->scene();
    double const scale = std::min(double(scene.width) / movie.size.width(), double(scene.height) / movie.size.height());
    QTransform const place = QTransform::fromScale(scale, scale)
                             * QTransform::fromTranslate((scene.width - movie.size.width() * scale) / 2.0, (scene.height - movie.size.height() * scale) / 2.0);
    m_document->beginMacro(tr("Import SWF"));
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
    int const at = m_document->importDrawings(-1, QFileInfo(path).completeBaseName(), 0, drawings, 1);
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
            // What had a blend mode or filters in Flash: the layer mixed and softened as they say (a blend mode Eclipse
            // has; a glow, a blur or a drop shadow, at the size the import scales to).
            static QHash<QString, int> const blends{{QStringLiteral("add"), Layer::BlendAdd},
                                                    {QStringLiteral("multiply"), Layer::BlendMultiply},
                                                    {QStringLiteral("screen"), Layer::BlendScreen},
                                                    {QStringLiteral("overlay"), Layer::BlendOverlay},
                                                    {QStringLiteral("darken"), Layer::BlendDarken},
                                                    {QStringLiteral("lighten"), Layer::BlendLighten},
                                                    {QStringLiteral("hardlight"), Layer::BlendHardLight},
                                                    {QStringLiteral("difference"), Layer::BlendDifference}};
            if (!blend.isEmpty())
                layer.blend = blends.value(blend, Layer::BlendNormal);
            for (FlaImport::Filter const& f : filters) {
                double const soft = (f.blurX + f.blurY) / 4.0 * scale;
                if (f.kind == QLatin1String("BlurFilter")) {
                    Effect blur = Effect::made(Effect::Blur);
                    blur.amount = soft;
                    layer.effects.push_back(blur);
                } else if (f.kind == QLatin1String("GlowFilter") && !f.inner) {
                    Effect glow = Effect::made(Effect::Glow);
                    glow.amount = soft;
                    glow.color = f.colour;
                    layer.effects.push_back(glow);
                } else if (f.kind == QLatin1String("DropShadowFilter") && !f.inner) {
                    Effect shadow = Effect::made(Effect::Shadow);
                    shadow.amount = soft;
                    shadow.color = f.colour;
                    double const a = f.angle * 3.14159265358979323846 / 180.0;
                    shadow.offset = QPointF(std::cos(a), std::sin(a)) * f.distance * scale;
                    layer.effects.push_back(shadow);
                }
            }
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
    // Its frame labels as markers of the scene (a comment as a marker's note); its sounds on sound layers of their own,
    // each sound written out once.
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
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const at = importSwf(path, &error);
    QApplication::restoreOverrideCursor();
    if (at < 0) {
        QMessageBox::warning(this, tr("Import SWF"), error);
        return;
    }
    statusBar()->showMessage(tr("%1: a drawing for each frame of the movie, on the layer %2.").arg(QFileInfo(path).fileName(), m_document->scene().layers[at].name)
                                 + (error.isEmpty() ? QString() : QLatin1Char(' ') + error),
                             10000);
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

void MainWindow::askImportImages(bool linked, QStringList const& given)
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
    if (given.isEmpty()) {
        choose();   // straight to the files, as the first thing asked
    } else {
        files->addItems(given);
        layerName->setText(tr("Scan"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
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
