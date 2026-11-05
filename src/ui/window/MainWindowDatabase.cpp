// Eclipse - the main window and the scene database: scenes opened from it and saved back to it as new versions, the
// rights to modify the scene, its drawings and its palettes, the Database Manager, and Batch Rendering.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"
#include "model/scene/SceneDatabase.h"
#include "ui/io/DatabaseManager.h"
#include "ui/io/RenderQueue.h"
#include "ui/timeline/Timeline.h"

bool MainWindow::openFromDatabase(QString const& environment, QString const& job, QString const& scene, int version, QString* error)
{
    SceneDatabase db;
    QString const file = db.versionFile(environment, job, scene, version);
    if (file.isEmpty()) {
        if (error)
            *error = tr("Cannot find scene %1 in scene database.").arg(scene);
        return false;
    }
    leaveDatabaseScene();
    m_openingFromDatabase = true;
    bool const opened = openFile(file);
    m_openingFromDatabase = false;
    if (!opened) {
        if (error)
            *error = tr("Cannot open the scene because of database error.");
        return false;
    }
    m_dbEnvironment = environment;
    m_dbJob = job;
    m_dbScene = scene;
    m_dbVersion = version > 0 ? version : db.currentVersion(environment, job, scene);
    // The rights to modify the scene: got, unless someone else has them (then it is open to look at).
    QString why;
    m_dbReadOnly = !db.getRights(environment, job, scene, QStringLiteral("scene"), &why);
    if (!m_dbReadOnly) {
        if (settings().value(QStringLiteral("prefs/autoRightsDrawings"), false).toBool())
            drawingRights(false, false, true);
        if (settings().value(QStringLiteral("prefs/autoRightsPalettes"), false).toBool())
            paletteRights(false, false);
    }
    updateTitle();
    statusBar()->showMessage(m_dbReadOnly ? tr("%1 is open to look at: %2").arg(scene, why)
                                          : tr("%1 (version %2) opened from the database; the rights to modify it are yours.").arg(scene).arg(m_dbVersion),
                             8000);
    return true;
}

void MainWindow::leaveDatabaseScene()
{
    if (m_dbScene.isEmpty())
        return;
    SceneDatabase db;
    if (!m_dbReadOnly)
        for (QString const& what : db.locks(m_dbEnvironment, m_dbJob, m_dbScene))
            if (!db.lockedByOther(m_dbEnvironment, m_dbJob, m_dbScene, what))
                db.releaseRights(m_dbEnvironment, m_dbJob, m_dbScene, what, false);
    m_dbEnvironment.clear();
    m_dbJob.clear();
    m_dbScene.clear();
    m_dbVersion = 0;
    m_dbReadOnly = false;
}

int MainWindow::saveToDatabase(QString const& comment, QString* error)
{
    if (m_dbScene.isEmpty()) {
        if (error)
            *error = tr("The scene was not opened from the database.");
        return 0;
    }
    if (m_dbReadOnly) {
        if (error)
            *error = tr("The rights to modify %1 are someone else's: it cannot be saved over.").arg(m_dbScene);
        return 0;
    }
    Scene saved = m_document->wholeScene();
    saved.uiState = projectUiState();
    SceneDatabase db;
    int const number = db.saveVersion(m_dbEnvironment, m_dbJob, m_dbScene, saved, comment, error);
    if (number > 0) {
        m_dbVersion = number;
        m_path = db.versionFile(m_dbEnvironment, m_dbJob, m_dbScene, number);
        m_document->setSaved();
        updateTitle();
        statusBar()->showMessage(tr("%1: version %2 saved to the database.").arg(m_dbScene).arg(number), 6000);
    }
    return number;
}

bool MainWindow::addToDatabase(QString const& environment, QString const& job, QString const& name, QString* error)
{
    Scene saved = m_document->wholeScene();
    saved.uiState = projectUiState();
    SceneDatabase db;
    if (db.scenes(environment, job).contains(name)) {
        // Continuing will overwrite: a new version of that scene.
        if (db.saveVersion(environment, job, name, saved, tr("Updated from %1").arg(m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName()), error) <= 0)
            return false;
    } else if (!db.createScene(environment, job, name, saved, error)) {
        return false;
    }
    return openFromDatabase(environment, job, name, 0, error);
}

int MainWindow::drawingRights(bool release, bool force, bool all)
{
    if (m_dbScene.isEmpty())
        return -1;
    SceneDatabase db;
    Scene const& scene = m_document->scene();
    QVector<int> layers = all ? QVector<int>() : m_timeline->selectedLayers();
    if (all)
        for (int l = 0; l < scene.layers.size(); ++l)
            layers << l;
    if (layers.isEmpty())
        layers << layer();
    int done = 0;
    QStringList refused;
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        QVector<int> drawings;
        if (all) {
            for (int d = 0; d < scene.layers[l].drawings.size(); ++d)
                drawings << d;
        } else if (int const d = scene.layers[l].drawingAt(frame()); d >= 0) {
            drawings << d;
        }
        for (int d : drawings) {
            QString const what = QStringLiteral("drawing:%1:%2").arg(scene.layers[l].name, scene.layers[l].drawingLabel(d));
            QString why;
            bool const ok = release ? db.releaseRights(m_dbEnvironment, m_dbJob, m_dbScene, what, force, &why)
                                    : db.getRights(m_dbEnvironment, m_dbJob, m_dbScene, what, &why);
            if (ok)
                ++done;
            else
                refused << why;
        }
    }
    if (!refused.isEmpty())
        statusBar()->showMessage(refused.first(), 8000);
    return done;
}

int MainWindow::paletteRights(bool release, bool force)
{
    if (m_dbScene.isEmpty())
        return -1;
    SceneDatabase db;
    int done = 0;
    QStringList whats{QStringLiteral("palettes")};
    for (QString const& name : m_document->scene().paletteNames)
        whats << QStringLiteral("palette:%1").arg(name);
    for (QString const& what : whats) {
        QString why;
        bool const ok = release ? db.releaseRights(m_dbEnvironment, m_dbJob, m_dbScene, what, force, &why)
                                : db.getRights(m_dbEnvironment, m_dbJob, m_dbScene, what, &why);
        done += ok;
        if (!ok)
            statusBar()->showMessage(why, 8000);
    }
    return done;
}

void MainWindow::onActionDatabaseManager()
{
    DatabaseManager manager(this);
    if (!m_dbScene.isEmpty())
        manager.select(m_dbEnvironment, m_dbJob, m_dbScene);
    connect(&manager, &DatabaseManager::openRequested, this, [this](QString const& e, QString const& j, QString const& s, int v) {
        if (!confirmDiscard())
            return;
        QString error;
        if (!openFromDatabase(e, j, s, v, &error))
            QMessageBox::warning(this, tr("Database Manager"), error);
    });
    manager.exec();
}

void MainWindow::onActionSaveToDatabase()
{
    if (m_dbScene.isEmpty()) {
        onActionUpdateDatabaseScene();
        return;
    }
    bool ok = false;
    QString const comment = QInputDialog::getText(this, tr("Save to Database"), tr("What changed in this version:"), QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    QString error;
    if (saveToDatabase(comment, &error) <= 0)
        QMessageBox::warning(this, tr("Save to Database"), error);
}

void MainWindow::onActionUpdateDatabaseScene()
{
    SceneDatabase db;
    QString error;
    if (!db.open(false, &error)) {
        QMessageBox::information(this, tr("Update Database Scene"), error);
        return;
    }
    QStringList places;
    for (QString const& e : db.environments())
        for (QString const& j : db.jobs(e))
            places << e + QStringLiteral(" / ") + j;
    if (places.isEmpty()) {
        QMessageBox::information(this, tr("Update Database Scene"), tr("Make an environment and a job first (File > Database > Database Manager)."));
        return;
    }
    bool ok = false;
    QString const place = QInputDialog::getItem(this, tr("Update Database Scene"), tr("Job:"), places, 0, false, &ok);
    if (!ok)
        return;
    QString const name = QInputDialog::getText(this, tr("Update Database Scene"), tr("Scene:"), QLineEdit::Normal,
                                               m_path.isEmpty() ? tr("Scene") : QFileInfo(m_path).completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QString const environment = place.section(QStringLiteral(" / "), 0, 0), job = place.section(QStringLiteral(" / "), 1);
    if (db.scenes(environment, job).contains(name)
        && QMessageBox::question(this, tr("Update Database Scene"),
                                 tr("Continuing will overwite the following scene:\n\nScene: %1\nJob: %2\nEnvironment: %3\n\nProceed?").arg(name, job, environment))
               != QMessageBox::Yes)
        return;
    if (!addToDatabase(environment, job, name, &error))
        QMessageBox::warning(this, tr("Update Database Scene"), error);
}

void MainWindow::onActionDatabaseVersions()
{
    onActionDatabaseManager();
}

void MainWindow::onActionGetDrawingRights()
{
    int const got = drawingRights(false, false, false);
    if (got < 0)
        statusBar()->showMessage(tr("The rights to modify drawings are for scenes opened from the database."), 6000);
    else
        statusBar()->showMessage(tr("Rights to modify %n drawing(s) got.", nullptr, got), 6000);
}

void MainWindow::onActionReleaseDrawingRights()
{
    int const gone = drawingRights(true, false, false);
    if (gone >= 0)
        statusBar()->showMessage(tr("Rights to modify %n drawing(s) released.", nullptr, gone), 6000);
}

void MainWindow::onActionForceReleaseDrawingRights()
{
    int const gone = drawingRights(true, true, false);
    if (gone >= 0)
        statusBar()->showMessage(tr("Rights to modify %n drawing(s) released.", nullptr, gone), 6000);
}

void MainWindow::onActionGetPaletteRights()
{
    int const got = paletteRights(false, false);
    if (got < 0)
        statusBar()->showMessage(tr("The rights to modify palettes are for scenes opened from the database."), 6000);
}

void MainWindow::onActionReleasePaletteRights()
{
    paletteRights(true, false);
}

void MainWindow::onActionRenderQueue()
{
    RenderQueue queue(this);
    queue.exec();
}

void MainWindow::onActionAddToRenderQueue()
{
    if (m_path.isEmpty() || m_document->unsaved()) {
        QMessageBox::information(this, tr("Batch Rendering"), tr("Save the scene first: the queue renders scene files."));
        return;
    }
    RenderQueue::Job job;
    job.scene = m_path;
    job.folder = QFileInfo(m_path).absoluteDir().filePath(QStringLiteral("frames"));
    QVector<RenderQueue::Job> jobs = RenderQueue::load();
    jobs.push_back(job);
    RenderQueue::save(jobs);
    statusBar()->showMessage(tr("%1 added to the Batch Rendering queue (%n job(s)).", nullptr, int(jobs.size())).arg(QFileInfo(m_path).fileName()), 6000);
}
