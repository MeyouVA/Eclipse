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

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
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
    if (m_document->undoStack()->isClean())
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
    createScene(form.path(), size.width, size.height, size.fps);
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
    m_canvas->fitToWindow();
    updateTitle();
    onActionShowWelcome();
}

bool MainWindow::createScene(QString const& path, int width, int height, double fps)
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
    m_path.clear();
    m_document->reset(scene);
    m_timeline->setCurrentCell(0, 0);
    m_canvas->fitToWindow();
    updateTitle();
    return saveTo(path);
}

void MainWindow::onActionShowWelcome()
{
    // At startup the welcome window is all there is: the main window comes up once a
    // scene has been made or chosen.  Closed without choosing, it stays hidden, and the
    // program ends.
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
        createScene(welcome.path(), size.width, size.height, size.fps);
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
    stop();
    m_path = path;
    m_document->reset(scene);
    m_timeline->setCurrentCell(int(scene.layers.size()) - 1, 0);
    m_canvas->setCurrentCell(layer(), frame());
    m_canvas->fitToWindow();
    updateTitle();
    addRecent(path);
    return true;
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
    if (!m_document->scene().save(path, &error)) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    m_path = path;
    m_document->undoStack()->setClean();
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
    return saveTo(path);
}

void MainWindow::onActionExportMovie()
{
    QString start = m_path.isEmpty() ? QStringLiteral("scene.mp4") : QFileInfo(m_path).path() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + QStringLiteral(".mp4");
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
    quality->setEnabled(QFileInfo(path).suffix().toLower() != QLatin1String("gif") && QFileInfo(path).suffix().toLower() != QLatin1String("webm"));
    form->addRow(tr("From frame"), first);
    form->addRow(tr("To frame"), last);
    form->addRow(tr("Size"), size);
    form->addRow(tr("Custom size"), custom);
    form->addRow(tr("Quality"), quality);
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
    }
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
    QSettings s = settings();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/workspace"), m_workspace);
    saveCurrentWorkspace();
    event->accept();
}
