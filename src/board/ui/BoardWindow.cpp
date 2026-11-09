#include "board/ui/BoardWindow.h"
#include "board/ui/BoardStage.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

BoardWindow::BoardWindow(QWidget* parent) : QMainWindow(parent), m_actions(new ActionManager(this))
{
    setObjectName(QStringLiteral("BoardWindow"));
    setWindowTitle(tr("Eclipse Storyboard"));
    resize(1200, 760);
    buildWorkspace();

    QString const resources = ActionManager::defaultDirectory();
    m_actions->setSettingsGroup(QStringLiteral("board"));
    m_actions->setIconDirectory(resources);
    m_actions->registerResponder(QStringLiteral("window"), this);
    QString error;
    if (m_actions->load(QDir(resources).filePath(QStringLiteral("board")), &error)) {
        m_actions->populateMenuBar(QStringLiteral("menu.main"), menuBar());
        for (QString const& id : m_actions->toolbarIds())
            if (auto* toolbar = m_actions->createToolbar(id, this))
                addToolBar(id == QLatin1String("BoardDrawingToolbar") ? Qt::LeftToolBarArea : Qt::TopToolBarArea, toolbar);
        m_actions->installShortcuts(this);
        m_stage->setViewActions({m_actions->action(QStringLiteral("BoardFitStage")),
            m_actions->action(QStringLiteral("BoardCameraGuide")), m_actions->action(QStringLiteral("BoardCameraPreview"))});
        m_actions->setChecked(QStringLiteral("BoardBrush"), true);
    } else {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), error);
    }

    loadWorkspaceSettings();
    applyPreferences();
    connect(&m_document, &BoardDocument::projectChanged, this, &BoardWindow::updateProjectState);
    connect(&m_document, &BoardDocument::projectChanged, this, &BoardWindow::refreshWorkspace);
    connect(m_document.undoStack(), &QUndoStack::cleanChanged, this, &BoardWindow::updateProjectState);
    updateProjectState();
}

void BoardWindow::updateProjectState()
{
    bool const loaded = !m_document.path().isEmpty();
    auto* history = m_document.undoStack();
    auto enabled = [this](char const* id, bool on) {
        if (auto* action = m_actions->action(QLatin1String(id)))
            action->setEnabled(on);
    };
    for (auto const* id : {"BoardSave", "BoardSaveAs", "BoardSaveVersion",
                           "BoardClose", "BoardRefreshThumbnails"})
        enabled(id, loaded);
    enabled("BoardSettings", loaded);
    enabled("BoardUndo", history->canUndo());
    enabled("BoardRedo", history->canRedo());
    updatePanelActions();
    setWindowModified(!history->isClean());
    if (loaded) {
        auto const& project = m_document.project();
        qsizetype panels = 0;
        for (auto const& shot : project.shots)
            panels += shot.panels.size();
        QString title = project.title.isEmpty() ? project.name : project.title;
        QString const base = QFileInfo(m_document.path()).completeBaseName();
        if (base != project.name)
            title = tr("%1 (%2)").arg(title, base);
        setWindowTitle(tr("%1[*] — Eclipse Storyboard").arg(title));
        statusBar()->showMessage(tr("Scenes: %1  |  Panels: %2  |  %3 × %4  |  %5 fps")
            .arg(project.shots.size()).arg(panels).arg(project.width).arg(project.height).arg(project.fps));
    } else {
        setWindowTitle(tr("Eclipse Storyboard"));
        statusBar()->clearMessage();
    }
}

void BoardWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    saveWorkspaceSettings();
    QMainWindow::closeEvent(event);
}

void BoardWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (m_sizeDefaultOnMaximize)
        QTimer::singleShot(0, this, [this] {
            if (m_sizeDefaultOnMaximize)
                sizeReferenceLayout();
        });
}

void BoardWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_sizeDefaultOnMaximize && isMaximized()) {
        m_sizeDefaultOnMaximize = false;
        QTimer::singleShot(0, this, [this] { sizeReferenceLayout(); });
    }
}

void BoardWindow::undo() { m_document.undoStack()->undo(); }
void BoardWindow::redo() { m_document.undoStack()->redo(); }
