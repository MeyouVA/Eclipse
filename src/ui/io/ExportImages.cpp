// Eclipse - writing the scene out: frames as pictures, an animated GIF, the sound.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <atomic>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QHeaderView>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "canvas/FrameRender.h"
#include "render/images/ImageFiles.h"
#include "ui/panels/ColourSpaceBox.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"

int MainWindow::exportImages(ImageExport const& options, QString* error)
{
    Scene const scene = m_document->scene();
    int const first = std::clamp(options.first, 0, scene.frameCount() - 1);
    int const last = options.last < 0 ? scene.frameCount() - 1 : std::clamp(options.last, first, scene.frameCount() - 1);
    QSize const size = FrameRender::sizeAt(scene, std::clamp(options.percent, 1, 1000) / 100.0);
    ImageFiles::Format const* const format = ImageFiles::find(options.format);
    if (!format) {
        if (error)
            *error = tr("Unknown image format: %1").arg(options.format);
        return 0;
    }
    if (!QDir().mkpath(options.folder)) {
        if (error)
            *error = tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(options.folder));
        return 0;
    }
    // Each layer apart as well (a layered format): the scene with only that layer of drawings shown, on nothing.
    bool const layered = options.layers && format->layers;
    QVector<Scene> alone(layered ? scene.layers.size() : 0);
    if (layered)
        for (int l = 0; l < scene.layers.size(); ++l) {
            Layer const& layer = scene.layers[l];
            if (!layer.holdsDrawings() && layer.kind != Layer::ColourCard)
                continue;
            Scene& only = alone[l];
            only = scene;
            only.background = Qt::transparent;
            for (int other = 0; other < only.layers.size(); ++other)
                if (other != l && (only.layers[other].holdsDrawings() || only.layers[other].kind == Layer::ColourCard))
                    only.layers[other].visible = false;
        }
    auto fileOf = [&](int f) {
        return QDir(options.folder).filePath(QStringLiteral("%1_%2.%3").arg(options.name).arg(f + 1, 4, 10, QLatin1Char('0')).arg(format->suffix));
    };
    // Colour management: from the working space to the files' own (EXR keeps the light in floats).
    QString const space = scene.outputSpace(options.colourSpace, format->suffix);
    bool const floats = !space.isEmpty() && format->suffix == QLatin1String("exr");
    QString spaceProblem;
    if (!space.isEmpty())
        scene.outputTransform(space, &spaceProblem);
    if (!spaceProblem.isEmpty()) {
        if (error)
            *error = spaceProblem;
        return 0;
    }
    QVector<int> frames;
    for (int f = first; f <= last; ++f)
        frames.push_back(f);
    std::atomic<int> written{0};
    auto keep = [&](int f, QImage const& image) -> QString {
        QString why;
        bool ok = false;
        if (layered) {
            QVector<ImageFiles::Layer> parts;
            for (int l : scene.drawOrder(f))
                if (l < alone.size() && !alone[l].layers.isEmpty() && scene.enabled(l))
                    parts.push_back({scene.layers[l].name, FrameRender::inSpace(scene, FrameRender::frame(alone[l], f, size, options.preview), space, false)});
            ok = ImageFiles::saveLayered(parts, FrameRender::inSpace(scene, image, space, false), fileOf(f), &why, space);
        } else {
            ok = ImageFiles::save(FrameRender::inSpace(scene, image, space, floats), fileOf(f), format->suffix, &why, 92, space);
        }
        if (!ok)
            return why.isEmpty() ? tr("Could not write %1.").arg(QDir::toNativeSeparators(fileOf(f))) : why;
        ++written;
        return QString();
    };
    auto progress = [&](int done) {
        if (options.progress) {
            options.progress->setValue(done);
            QApplication::processEvents();
            if (options.progress->wasCanceled())
                return false;
        } else {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        statusBar()->showMessage(tr("Rendered frame %1").arg(first + done), 1000);
        return true;
    };
    QString why;
    if (!FrameRender::render(scene, frames, size, options.preview, keep, progress, &why) && error)
        *error = why.isEmpty() ? tr("Stopped.") : why;
    return written;
}

void MainWindow::askExportImages(bool preview)
{
    Scene const& scene = m_document->scene();
    QDialog dialog(this);
    dialog.setWindowTitle(preview ? tr("Export OpenGL Frames") : tr("Export Images"));
    QLineEdit* const folder = new QLineEdit(m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).path() + QStringLiteral("/frames"), &dialog);
    folder->setMinimumWidth(320);
    QPushButton* const browse = new QPushButton(tr("Browse..."), &dialog);
    connect(browse, &QPushButton::clicked, &dialog, [&] {
        QString const chosen = QFileDialog::getExistingDirectory(&dialog, tr("Export To"), folder->text());
        if (!chosen.isEmpty())
            folder->setText(chosen);
    });
    QHBoxLayout* const folderRow = new QHBoxLayout;
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    QLineEdit* const name = new QLineEdit(m_path.isEmpty() ? QStringLiteral("frame") : QFileInfo(m_path).completeBaseName(), &dialog);
    QComboBox* const format = new QComboBox(&dialog);
    format->setObjectName(QStringLiteral("ExportFormat"));
    for (ImageFiles::Format const& f : ImageFiles::formats())
        format->addItem(f.alpha ? tr("%1 (with transparency)").arg(f.name) : f.name, f.suffix);
    // A layered format can keep each layer of drawings apart, as well as the frame as a whole.
    QCheckBox* const layers = new QCheckBox(tr("Each layer apart too (Photoshop layers)"), &dialog);
    layers->setObjectName(QStringLiteral("ExportLayers"));
    layers->setToolTip(tr("A multi-layer write: every layer of drawings in the frame as a layer of its own, the frame as a whole as well."));
    auto showLayers = [format, layers] {
        ImageFiles::Format const* const f = ImageFiles::find(format->currentData().toString());
        layers->setEnabled(f && f->layers);
    };
    connect(format, &QComboBox::currentIndexChanged, &dialog, showLayers);
    showLayers();
    // Colour management on: the colour space the files are written in.
    QComboBox* const space = ColourSpaceBox::make(&dialog, QStringLiteral("ExportColourSpace"), {}, tr("Scene Working Colour Space"));
    space->setToolTip(tr("The colour space the pictures are written in (an EXR is written in the scene's linear space unless another is chosen)."));
    space->setEnabled(scene.colourManaged);
    if (!scene.colourManaged)
        space->setItemText(0, tr("No Colour Space Management"));
    QSpinBox* const from = new QSpinBox(&dialog);
    from->setRange(1, scene.frameCount());
    from->setValue(playStart() + 1);
    QSpinBox* const to = new QSpinBox(&dialog);
    to->setRange(1, scene.frameCount());
    to->setValue(playStop() + 1);
    from->setObjectName(QStringLiteral("ExportFrom"));
    to->setObjectName(QStringLiteral("ExportTo"));
    // The whole scene, or only the frame one is on, at a press.
    QPushButton* const allFrames = new QPushButton(tr("All Frames"), &dialog);
    allFrames->setObjectName(QStringLiteral("ExportAllFrames"));
    QPushButton* const thisFrame = new QPushButton(tr("Current Frame"), &dialog);
    thisFrame->setObjectName(QStringLiteral("ExportCurrentFrame"));
    int const frames = scene.frameCount(), here = frame() + 1;
    connect(allFrames, &QPushButton::clicked, &dialog, [from, to, frames] {
        from->setValue(1);
        to->setValue(frames);
    });
    connect(thisFrame, &QPushButton::clicked, &dialog, [from, to, here] {
        from->setValue(here);
        to->setValue(here);
    });
    QHBoxLayout* const rangeRow = new QHBoxLayout;
    rangeRow->addWidget(allFrames);
    rangeRow->addWidget(thisFrame);
    rangeRow->addStretch(1);
    QComboBox* const resolution = new QComboBox(&dialog);
    for (int percent : {100, 75, 50, 25, 200})
        resolution->addItem(tr("%1%  (%2 x %3)").arg(percent).arg(qRound(scene.width * percent / 100.0)).arg(qRound(scene.height * percent / 100.0)), percent);
    resolution->setCurrentIndex(preview ? 2 : 0);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(tr("Folder"), folderRow);
    form->addRow(tr("File name"), name);
    form->addRow(tr("Format"), format);
    form->addRow(QString(), layers);
    form->addRow(tr("Colour Space"), space);
    form->addRow(tr("From frame"), from);
    form->addRow(tr("To frame"), to);
    form->addRow(QString(), rangeRow);
    form->addRow(tr("Resolution"), resolution);
    if (preview)
        form->addRow(new QLabel(tr("Quick frames for checking the animation: edges are not smoothed."), &dialog));
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ImageExport options;
    options.folder = folder->text();
    options.name = name->text().trimmed().isEmpty() ? QStringLiteral("frame") : name->text().trimmed();
    options.format = format->currentData().toString();
    options.first = std::min(from->value(), to->value()) - 1;
    options.last = std::max(from->value(), to->value()) - 1;
    options.percent = resolution->currentData().toInt();
    options.preview = preview;
    options.layers = layers->isEnabled() && layers->isChecked();
    options.colourSpace = ColourSpaceBox::chosen(space);
    stop();
    int const wanted = options.last - options.first + 1;
    QProgressDialog progress(tr("Rendering scene..."), tr("Cancel"), 0, wanted, this);
    progress.setObjectName(QStringLiteral("RenderProgress"));
    progress.setWindowTitle(preview ? tr("Export OpenGL Frames") : tr("Export Images"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    options.progress = &progress;
    QString error;
    int const written = exportImages(options, &error);
    progress.reset();
    if (written == wanted)
        statusBar()->showMessage(tr("Exported %1 frames to %2").arg(written).arg(QDir::toNativeSeparators(options.folder)), 6000);
    else
        QMessageBox::critical(this, tr("Eclipse"), tr("Export stopped after %1 of %2 frames.\n%3").arg(written).arg(wanted).arg(error));
}

void MainWindow::onActionExportFrames()
{
    askExportImages(false);
}

void MainWindow::onActionExportGif()
{
    QString const start = m_path.isEmpty() ? QStringLiteral("scene.gif") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".gif");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Animated GIF"), start, tr("Animated GIF (*.gif)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().toLower() != QLatin1String("gif"))
        path += QStringLiteral(".gif");
    exportMovieTo(path);
}

bool MainWindow::exportAudio(QString const& path, QString* error)
{
    QVector<qint16> const sound = Sound::mix(m_document->scene());
    if (sound.isEmpty()) {
        if (error)
            *error = tr("The scene has no sound to export: add a sound layer first (File > Import > Sound), and check that it is enabled.");
        return false;
    }
    return Sound::writeWav(path, sound, error);
}

void MainWindow::onActionExportAudio()
{
    QString const start = m_path.isEmpty() ? QStringLiteral("scene.wav") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".wav");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Audio"), start, tr("WAV Sound (*.wav)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".wav");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    bool const ok = exportAudio(path, &error);
    QApplication::restoreOverrideCursor();
    if (ok)
        statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 6000);
    else
        QMessageBox::critical(this, tr("Eclipse"), tr("The sound could not be written.\n%1").arg(error));
}

QString MainWindow::writeFolder(QString const& path) const
{
    QDir const home = m_path.isEmpty() ? QDir::current() : QFileInfo(m_path).absoluteDir();
    return QDir::cleanPath(home.absoluteFilePath(path.trimmed()));
}

int MainWindow::renderWrites(QString* error, QProgressDialog* progress)
{
    Scene const scene = m_document->scene();
    int made = 0;
    QStringList problems;
    for (Scene::Write const& w : scene.writes) {
        if (!w.enabled || w.path.trimmed().isEmpty())
            continue;
        QString const where = writeFolder(w.path);
        QString why;
        bool ok = false;
        if (w.movie) {
            MovieExport::Options options;
            options.scale = std::clamp(w.percent, 1, 1000) / 100.0;
            options.colourSpace = scene.outputSpace(w.colourSpace, QString());
            QDir().mkpath(QFileInfo(where).absolutePath());
            ok = MovieExport::write(scene, where, nullptr, &why, options);
        } else {
            ImageExport images;
            images.folder = where;
            images.name = w.name.trimmed().isEmpty() ? QStringLiteral("frame") : w.name.trimmed();
            images.format = w.format;
            images.percent = std::clamp(w.percent, 1, 1000);
            images.layers = w.layers;
            images.colourSpace = w.colourSpace;
            images.progress = progress;
            if (progress)
                progress->setLabelText(tr("Rendering scene... (%1)").arg(w.name));
            QDir().mkpath(where);
            ok = exportImages(images, &why) == scene.frameCount();
        }
        if (ok)
            ++made;
        else
            problems << QStringLiteral("%1: %2").arg(w.name, why);
    }
    if (error)
        *error = problems.join(QLatin1Char('\n'));
    return made;
}

void MainWindow::onActionRenderWrites()
{
    Scene const& scene = m_document->scene();
    if (scene.writes.isEmpty()) {
        statusBar()->showMessage(tr("The scene has no write outputs: add them with Scene > Write Outputs."), 7000);
        onActionWriteOutputs();
        return;
    }
    if (std::none_of(scene.writes.cbegin(), scene.writes.cend(), [](Scene::Write const& w) { return w.enabled && !w.path.trimmed().isEmpty(); })) {
        QMessageBox::information(this, tr("Render Write Nodes"), tr("Render requested with no write module enabled."));
        return;
    }
    // Frames already there from an earlier render: made again only when asked.
    bool already = false;
    for (Scene::Write const& w : scene.writes)
        if (w.enabled && !w.movie && !w.path.trimmed().isEmpty() && !QDir(writeFolder(w.path)).entryList({QStringLiteral("*.") + w.format}, QDir::Files).isEmpty())
            already = true;
    if (already && QMessageBox::question(this, tr("Render Write Nodes"), tr("Some frames have already been rendered. Do you want to render these frames again?"))
                       != QMessageBox::Yes)
        return;
    stop();
    QProgressDialog progress(tr("Rendering scene..."), tr("Cancel"), 0, scene.frameCount(), this);
    progress.setObjectName(QStringLiteral("RenderProgress"));
    progress.setWindowTitle(tr("Render Write Nodes"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    QString error;
    int const made = renderWrites(&error, &progress);
    progress.reset();
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Render Write Nodes"), tr("%n output(s) made. These could not be:", nullptr, made) + QLatin1Char('\n') + error);
    else
        statusBar()->showMessage(tr("%n write output(s) made.", nullptr, made), 6000);
}

void MainWindow::onActionWriteOutputs()
{
    QVector<Scene::Write> writes = m_document->scene().writes;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("WriteOutputsDialog"));
    dialog.setWindowTitle(tr("Write Outputs"));
    QTableWidget* const table = new QTableWidget(0, 8, &dialog);
    table->setObjectName(QStringLiteral("WriteOutputsTable"));
    table->setHorizontalHeaderLabels({tr("On"), tr("Name"), tr("Kind"), tr("Folder or file"), tr("Images as"), tr("Size %"), tr("Layers"), tr("Colour Space")});
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    auto addRow = [table](Scene::Write const& w) {
        int const row = table->rowCount();
        table->insertRow(row);
        QTableWidgetItem* const on = new QTableWidgetItem;
        on->setCheckState(w.enabled ? Qt::Checked : Qt::Unchecked);
        table->setItem(row, 0, on);
        table->setItem(row, 1, new QTableWidgetItem(w.name));
        QComboBox* const kind = new QComboBox(table);
        kind->addItems({QObject::tr("Images"), QObject::tr("Movie")});
        kind->setCurrentIndex(w.movie ? 1 : 0);
        table->setCellWidget(row, 2, kind);
        table->setItem(row, 3, new QTableWidgetItem(w.path));
        QComboBox* const format = new QComboBox(table);
        for (ImageFiles::Format const& f : ImageFiles::formats())
            format->addItem(f.suffix);
        ImageFiles::Format const* const known = ImageFiles::find(w.format);
        format->setCurrentText(known ? known->suffix : QStringLiteral("png"));
        table->setCellWidget(row, 4, format);
        QSpinBox* const size = new QSpinBox(table);
        size->setRange(1, 1000);
        size->setValue(w.percent);
        table->setCellWidget(row, 5, size);
        QTableWidgetItem* const layers = new QTableWidgetItem;
        layers->setCheckState(w.layers ? Qt::Checked : Qt::Unchecked);
        layers->setToolTip(QObject::tr("Multi-layer write: in a layered format (psd), each layer of drawings apart too."));
        table->setItem(row, 6, layers);
        QComboBox* const space = ColourSpaceBox::make(table, QString(), w.colourSpace, QObject::tr("Scene Working Colour Space"));
        space->setToolTip(QObject::tr("The colour space the frames are written in, with colour space management on (Scene Settings: Colour)."));
        table->setCellWidget(row, 7, space);
    };
    for (Scene::Write const& w : std::as_const(writes))
        addRow(w);
    QPushButton* const add = new QPushButton(tr("Add"), &dialog);
    add->setObjectName(QStringLiteral("WriteOutputsAdd"));
    QPushButton* const remove = new QPushButton(tr("Remove"), &dialog);
    remove->setObjectName(QStringLiteral("WriteOutputsRemove"));
    connect(add, &QPushButton::clicked, &dialog, [table, addRow] {
        Scene::Write w;
        w.name = QObject::tr("Write%1").arg(table->rowCount() + 1);
        w.path = QStringLiteral("frames/") + w.name;
        addRow(w);
    });
    connect(remove, &QPushButton::clicked, &dialog, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Renders kept with the scene: File > Export > Render Write Nodes makes all of them that are on. "
                                    "A folder or file not written in full is taken from the scene file's folder."), &dialog));
    layout->addWidget(table, 1);
    layout->addLayout(row);
    layout->addWidget(buttons);
    dialog.resize(900, 320);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QVector<Scene::Write> made;
    for (int r = 0; r < table->rowCount(); ++r) {
        Scene::Write w;
        w.enabled = table->item(r, 0)->checkState() == Qt::Checked;
        w.name = table->item(r, 1)->text().trimmed();
        w.movie = static_cast<QComboBox*>(table->cellWidget(r, 2))->currentIndex() == 1;
        w.path = table->item(r, 3)->text().trimmed();
        w.format = static_cast<QComboBox*>(table->cellWidget(r, 4))->currentText();
        w.percent = static_cast<QSpinBox*>(table->cellWidget(r, 5))->value();
        w.layers = table->item(r, 6)->checkState() == Qt::Checked;
        w.colourSpace = ColourSpaceBox::chosen(static_cast<QComboBox*>(table->cellWidget(r, 7)));
        made.push_back(w);
    }
    m_document->setWrites(made);
}
