// Eclipse - the main window: files - new, open, recent, save, export a movie, closing.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSpinBox>
#include <QCheckBox>
#include <QJsonDocument>
#include "model/scene/AtomicFile.h"

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/ColourSpaceBox.h"
#include "ui/views/NodeView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

bool MainWindow::confirmDiscard()
{
    if (!m_document->unsaved())
        return true;
    QMessageBox::StandardButton const answer =
        QMessageBox::warning(this, tr("Eclipse"), tr("The scene has unsaved changes. Save them?"),
                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return onActionSave();
    return answer == QMessageBox::Discard;
}

void MainWindow::onActionNewScene()
{
    // The form of the welcome window, without the scenes to open.
    WelcomeDialog form(ActionManager::defaultDirectory(), this, true);
    if (form.exec() != QDialog::Accepted || form.choice() != WelcomeDialog::CreateScene || !confirmDiscard())
        return;
    Resolution const size = form.resolution();
    createScene(form.path(), size.width, size.height, size.fps, size.fov);
}

void MainWindow::onActionCloseScene()
{
    // The scene goes and the welcome window comes back, as when the program starts.
    if (!confirmDiscard())
        return;
    stop();
    m_path.clear();
    m_document->reset(Scene());
    m_timeline->setCurrentCell(0, 0);
    placeCameraView();
    updateTitle();
    onActionShowWelcome();
}

bool MainWindow::createScene(QString const& path, int width, int height, double fps, double fov)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
        return false;
    }
    stop();
    Scene scene;
    scene.width = width;
    scene.height = height;
    scene.fps = fps;
    scene.fov = std::clamp(fov > 0.0 ? fov : Layer::kDefaultFov, m_document->minimumFov(), m_document->maximumFov());
    m_path.clear();
    m_document->reset(scene);
    m_timeline->setCurrentCell(0, 0);
    placeCameraView();
    updateTitle();
    return saveTo(path);
}

void MainWindow::onActionShowWelcome()
{
    WelcomeDialog welcome(ActionManager::defaultDirectory(), isVisible() ? this : nullptr);
    bool const accepted = welcome.exec() == QDialog::Accepted;
    if (accepted && !isVisible()) {
        show();
        QApplication::processEvents();  // the views get their sizes before a scene is fitted to them
    }
    if (!accepted || !confirmDiscard())
        return;
    if (welcome.choice() == WelcomeDialog::OpenScene) {
        openFile(welcome.path());
    } else if (welcome.choice() == WelcomeDialog::CreateScene) {
        Resolution const size = welcome.resolution();
        createScene(welcome.path(), size.width, size.height, size.fps, size.fov);
    }
}

bool MainWindow::openFile(QString const& path)
{
    Scene scene;
    QString error;
    if (!Scene::load(path, scene, &error)) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    bool const oldFile = !Scene::lastLoadUsedFolder();
    QStringList const missing = Scene::lastMissingFiles();
    stop();
    m_path = path;
    m_document->reset(scene);
    m_timeline->setCurrentCell(int(scene.layers.size()) - 1, 0);
    m_canvas->setCurrentCell(layer(), frame());
    placeCameraView();
    updateTitle();
    addRecent(path);
    QJsonObject const ui = scene.uiState;
    QString const opened = m_path;
    QTimer::singleShot(0, this, [this, opened, ui] {
        if (m_path == opened)
            restoreProjectUi(ui);
    });
    if (!missing.isEmpty())
        QMessageBox::warning(this, tr("Eclipse"), tr("%n file(s) of the scene's folder could not be read; what they held is left empty:\n%1", nullptr, int(missing.size()))
                                                      .arg(missing.mid(0, 12).join(QLatin1Char('\n'))));
    if (oldFile)
        offerSceneFolder();
    return true;
}

QString MainWindow::sceneFolderPath(QString const& path)
{
    // A scene keeps a folder of its own, named as it is: one that is not in it yet goes into one, beside where it was.
    QFileInfo const info(path);
    QString const name = info.completeBaseName();
    if (info.absoluteDir().dirName().compare(name, Qt::CaseInsensitive) == 0)
        return info.absoluteFilePath();
    return info.absoluteDir().filePath(name + QLatin1Char('/') + info.fileName());
}

void MainWindow::offerSceneFolder()
{
    if (!settings().value(QStringLiteral("prefs/askConvertScenes"), true).toBool())
        return;
    QString const target = sceneFolderPath(m_path);
    bool const moves = QDir::cleanPath(target) != QDir::cleanPath(QFileInfo(m_path).absoluteFilePath());
    QString const kept = QFileInfo(m_path).absoluteDir().filePath(QFileInfo(m_path).completeBaseName() + QStringLiteral("_single-file.") + QFileInfo(m_path).suffix());
    QString const question = tr("This scene is in the older format, kept all in one file.\n\n"
                                "Convert it to a scene folder? Each drawing gets a file of its own under elements/, the palettes go in palette-library/ "
                                "and renders in frames/.\n\n%1")
                                 .arg(moves ? tr("The scene folder will be made at:\n%1\n(the old file stays where it is).").arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()))
                                            : tr("The old file is kept as %1.").arg(QFileInfo(kept).fileName()));
    if (QMessageBox::question(this, tr("Convert Scene"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;
    QString error;
    if (moves) {
        if (QFileInfo::exists(target)) {
            QMessageBox::warning(this, tr("Convert Scene"), tr("%1 is there already; the scene was not converted.").arg(QDir::toNativeSeparators(target)));
            return;
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
    } else {
        QFile::remove(kept);
        if (!QFile::copy(m_path, kept)) {
            QMessageBox::warning(this, tr("Convert Scene"), tr("Could not keep the old file as %1; the scene was not converted.").arg(QDir::toNativeSeparators(kept)));
            return;
        }
    }
    bool const keepBackup = m_keepBackup;
    m_keepBackup = false;           // (the old file is kept already)
    bool const saved = saveTo(target);
    m_keepBackup = keepBackup;
    if (saved)
        statusBar()->showMessage(tr("Converted to a scene folder: %1").arg(QDir::toNativeSeparators(target)), 8000);
}

void MainWindow::onActionOpenScene()
{
    if (!confirmDiscard())
        return;
    QString const path = QFileDialog::getOpenFileName(this, tr("Open Scene"), QFileInfo(m_path).path(), kFilter);
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::addRecent(QString const& path)
{
    QSettings s = settings();
    QStringList recent = s.value(QStringLiteral("recentFiles")).toStringList();
    QString const clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    recent.removeAll(clean);
    recent.prepend(clean);
    while (recent.size() > 10)
        recent.removeLast();
    s.setValue(QStringLiteral("recentFiles"), recent);
}

void MainWindow::onShowRecent(QMenu* menu)
{
    menu->clear();
    QStringList const recent = settings().value(QStringLiteral("recentFiles")).toStringList();
    for (QString const& path : recent) {
        QAction* const a = menu->addAction(QDir::toNativeSeparators(path));
        connect(a, &QAction::triggered, this, [this, path] {
            if (confirmDiscard())
                openFile(path);
        });
    }
    if (!recent.isEmpty())
        menu->addSeparator();
    QAction* const clear = menu->addAction(tr("Clear"));
    clear->setEnabled(!recent.isEmpty());
    connect(clear, &QAction::triggered, this, [] { settings().remove(QStringLiteral("recentFiles")); });
}

bool MainWindow::saveTo(QString const& path)
{
    QString error;
    // The file as it was, kept beside it (with ~ after its name) before it is saved over.
    if (m_keepBackup && QFileInfo::exists(path)) {
        if (!AtomicFile::replaceFrom(path, path + QLatin1Char('~'), &error)) {
            QMessageBox::critical(this, tr("Eclipse"), tr("Could not save the backup:\n%1").arg(error));
            return false;
        }
    }
    Scene saved = m_document->wholeScene();
    saved.uiState = projectUiState();
    if (!saved.saveProject(path, &error)) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    m_path = path;
    m_document->setSaved();
    updateTitle();
    addRecent(path);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

bool MainWindow::onActionSave()
{
    return m_path.isEmpty() ? onActionSaveAs() : saveTo(m_path);
}

bool MainWindow::onActionSaveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Scene"), m_path.isEmpty() ? QStringLiteral("scene.ecl") : m_path, kFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".ecl");
    path = sceneFolderPath(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return saveTo(path);
}

void MainWindow::onActionExportMovie()
{
    QString start = m_path.isEmpty() ? QStringLiteral("scene.mp4") : QFileInfo(m_path).path() + QStringLiteral("/frames/") + QFileInfo(m_path).completeBaseName() + QStringLiteral(".mp4");
    QString path = QFileDialog::getSaveFileName(this, tr("Export Movie"), start,
                                                tr("H.264 Movie (*.mp4);;QuickTime Movie (*.mov);;WebM Movie (*.webm);;Animated GIF (*.gif)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".mp4");
    // Which frames, how big, how fine.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export Movie"));
    QFormLayout* const form = new QFormLayout(&dialog);
    int const frames = m_document->scene().frameCount();
    QSpinBox* const first = new QSpinBox(&dialog);
    first->setObjectName(QStringLiteral("MovieFirst"));
    first->setRange(1, frames);
    QSpinBox* const last = new QSpinBox(&dialog);
    last->setObjectName(QStringLiteral("MovieLast"));
    last->setRange(1, frames);
    last->setValue(frames);
    connect(first, &QSpinBox::valueChanged, last, [last](int v) { last->setMinimum(v); });
    QComboBox* const size = new QComboBox(&dialog);
    size->setObjectName(QStringLiteral("MovieSize"));
    for (int percent : {200, 100, 75, 50, 33, 25})
        size->addItem(tr("%1 %  (%2 x %3)").arg(percent).arg(m_document->scene().width * percent / 100).arg(m_document->scene().height * percent / 100), percent);
    size->setCurrentIndex(1);
    // ... or a size of one's own: so many pixels wide, the height following the shape of the scene.
    size->addItem(tr("Custom"), 0);
    QSpinBox* const custom = new QSpinBox(&dialog);
    custom->setObjectName(QStringLiteral("MovieCustomWidth"));
    custom->setRange(16, 16384);
    custom->setSuffix(tr(" pixels wide"));
    custom->setValue(m_document->scene().width);
    custom->setEnabled(false);
    connect(size, &QComboBox::currentIndexChanged, custom, [size, custom] { custom->setEnabled(size->currentData().toInt() == 0); });
    QComboBox* const quality = new QComboBox(&dialog);
    quality->setObjectName(QStringLiteral("MovieQuality"));
    quality->addItem(tr("Fine (largest file)"), 18);
    quality->addItem(tr("Medium"), 23);
    quality->addItem(tr("Small file"), 28);
    QString const movieKind = QFileInfo(path).suffix().toLower();
    quality->setEnabled(movieKind != QLatin1String("gif"));
    // The codec (a .mp4 or a .mov), how long the encoder takes, and the sound's bit rate.
    QComboBox* const codec = new QComboBox(&dialog);
    codec->setObjectName(QStringLiteral("MovieCodec"));
    codec->addItem(tr("H.264 (plays everywhere)"), 0);
    codec->addItem(tr("H.265 / HEVC (smaller file)"), 1);
    if (movieKind == QLatin1String("mov")) {
        codec->addItem(tr("ProRes 422 HQ (for editing)"), 2);
        codec->addItem(tr("ProRes 4444 (supports transparency)"), 3);
        codec->setCurrentIndex(codec->findData(3));
    }
    codec->setEnabled(movieKind == QLatin1String("mp4") || movieKind == QLatin1String("mov"));
    QCheckBox* const transparent = new QCheckBox(tr("Transparent background"), &dialog);
    transparent->setObjectName(QStringLiteral("MovieTransparentBackground"));
    transparent->setChecked(movieKind == QLatin1String("mov"));
    transparent->setVisible(movieKind == QLatin1String("mov"));
    transparent->setToolTip(tr("Keeps empty areas transparent and preserves layer translucency."));
    QComboBox* const speed = new QComboBox(&dialog);
    speed->setObjectName(QStringLiteral("MovieSpeed"));
    speed->addItem(tr("Fast"), 0);
    speed->addItem(tr("Medium"), 1);
    speed->addItem(tr("Slow (smaller file)"), 2);
    speed->setCurrentIndex(1);
    speed->setEnabled(movieKind != QLatin1String("gif"));
    QComboBox* const audio = new QComboBox(&dialog);
    audio->setObjectName(QStringLiteral("MovieAudio"));
    for (int kbps : {128, 192, 256, 320})
        audio->addItem(tr("%1 kb/s").arg(kbps), kbps);
    audio->setCurrentIndex(1);
    audio->setEnabled(movieKind != QLatin1String("gif"));
    auto codecChanged = [codec, quality, speed, audio, transparent, movieKind] {
        bool const prores = movieKind == QLatin1String("mov") && codec->currentData().toInt() >= 2;
        quality->setEnabled(!prores && movieKind != QLatin1String("gif"));
        speed->setEnabled(!prores && movieKind != QLatin1String("gif"));
        audio->setEnabled(!prores && movieKind != QLatin1String("gif"));
        transparent->setEnabled(movieKind == QLatin1String("mov") && codec->currentData().toInt() == 3);
    };
    connect(codec, &QComboBox::currentIndexChanged, &dialog, codecChanged);
    codecChanged();
    form->addRow(tr("From frame"), first);
    form->addRow(tr("To frame"), last);
    form->addRow(tr("Size"), size);
    form->addRow(tr("Custom size"), custom);
    form->addRow(tr("Quality"), quality);
    form->addRow(tr("Codec"), codec);
    form->addRow(transparent);
    form->addRow(tr("Encoding"), speed);
    form->addRow(tr("Sound"), audio);
    // Colour management on: the colour space the frames are put in for the movie.
    Scene const& managed = m_document->scene();
    QComboBox* const space = ColourSpaceBox::make(&dialog, QStringLiteral("MovieColourSpace"), {}, tr("Scene Working Colour Space"));
    space->setEnabled(managed.colourManaged);
    if (!managed.colourManaged)
        space->setItemText(0, tr("No Colour Space Management"));
    space->setToolTip(tr("The colour space the movie's frames are put in (most players take sRGB or Rec.709)."));
    form->addRow(tr("Colour Space"), space);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    MovieChoices choices;
    choices.first = first->value() - 1;
    choices.last = last->value() - 1;
    choices.percent = size->currentData().toInt();
    if (choices.percent == 0)
        choices.percent = std::clamp(int(std::lround(custom->value() * 100.0 / std::max(1, m_document->scene().width))), 1, 1000);
    choices.quality = quality->currentData().toInt();
    choices.codec = codec->currentData().toInt();
    choices.speed = speed->currentData().toInt();
    choices.audioKbps = audio->currentData().toInt();
    choices.transparentBackground = transparent->isEnabled() && transparent->isChecked();
    choices.colourSpace = ColourSpaceBox::chosen(space);
    exportMovieTo(path, &choices);
}

void MainWindow::exportMovieTo(QString const& path, MovieChoices const* choices)
{
    stop();
    Scene const scene = m_document->scene();
    MovieExport::Options options;
    if (choices) {
        options.first = choices->first;
        options.last = choices->last;
        options.scale = choices->percent / 100.0;
        options.quality = choices->quality;
        options.codec = choices->codec;
        options.speed = choices->speed;
        options.audioKbps = choices->audioKbps;
        options.transparentBackground = choices->transparentBackground;
    }
    options.colourSpace = scene.outputSpace(choices ? choices->colourSpace : QString(), QString());
    QProgressDialog progress(tr("Exporting movie..."), tr("Stop"), 0, scene.frameCount(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QString error;
    bool const ok = MovieExport::write(scene, path, [&progress](int f) {
        progress.setValue(f);
        QApplication::processEvents();
        return !progress.wasCanceled();
    }, &error, options);
    progress.setValue(scene.frameCount());
    if (ok)
        statusBar()->showMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)), 6000);
    else if (!progress.wasCanceled())
        QMessageBox::critical(this, tr("Eclipse"), tr("The movie could not be written.\n%1").arg(error));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    if (!m_autoSaveWorkspace && m_persistWorkspaces && workspaceChanged()) {
        QMessageBox::StandardButton const answer
            = QMessageBox::question(this, tr("Eclipse"), tr("Some workspace have been changed. Do you wish to save them?"),
                                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Yes)
            saveCurrentWorkspace(true);
    }
    QSettings s = settings();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/workspace"), m_workspace);
    saveCurrentWorkspace();
    event->accept();
}
