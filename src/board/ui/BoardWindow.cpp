#include "board/ui/BoardWindow.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QMessageBox>
#include <QStatusBar>
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
        m_actions->setChecked(QStringLiteral("BoardBrush"), true);
    } else {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), error);
    }

    m_defaultLayout = saveState();
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/workspace"));
    restoreGeometry(prefs.value(QStringLiteral("geometry")).toByteArray());
    restoreState(prefs.value(QStringLiteral("layout")).toByteArray());
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
    enabled("BoardSave", loaded);
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
        QString const title = project.title.isEmpty() ? project.name : project.title;
        setWindowTitle(tr("%1[*] — Eclipse Storyboard").arg(title));
        statusBar()->showMessage(tr("Scenes: %1  |  Panels: %2  |  %3 × %4  |  %5 fps")
            .arg(project.shots.size()).arg(panels).arg(project.width).arg(project.height).arg(project.fps));
    }
}

void BoardWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/workspace"));
    prefs.setValue(QStringLiteral("geometry"), saveGeometry());
    prefs.setValue(QStringLiteral("layout"), saveState());
    QMainWindow::closeEvent(event);
}

void BoardWindow::restoreDefaultLayout()
{
    restoreState(m_defaultLayout);
}

void BoardWindow::undo() { m_document.undoStack()->undo(); }
void BoardWindow::redo() { m_document.undoStack()->redo(); }
