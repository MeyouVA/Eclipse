#include "board/ui/BoardWindow.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardThumbnails.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"

#include <QDockWidget>
#include <QTabWidget>

namespace {
constexpr int layoutVersion = 1;
QStringList const workspaceIds{QStringLiteral("horizontal"), QStringLiteral("overview"), QStringLiteral("vertical"),
                               QStringLiteral("drawing"), QStringLiteral("timeline")};

QString workspaceKey(QString const& name) { return QStringLiteral("presets/") + name; }
}

void BoardWindow::loadWorkspaceSettings()
{
    m_defaultLayout = saveState(layoutVersion);
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/workspace"));
    restoreGeometry(prefs.value(QStringLiteral("geometry")).toByteArray());
    QString const last = prefs.value(QStringLiteral("active")).toString();
    if (workspaceIds.contains(last))
        m_workspace = last;
    loadWorkspace();
}

void BoardWindow::saveWorkspaceSettings()
{
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/workspace"));
    prefs.setValue(QStringLiteral("geometry"), saveGeometry());
    prefs.setValue(QStringLiteral("active"), m_workspace);
    QByteArray const state = saveState(layoutVersion);
    prefs.setValue(workspaceKey(m_workspace) + QStringLiteral("/layout"), state);
    prefs.setValue(workspaceKey(m_workspace) + QStringLiteral("/inspector"), m_inspectorTabs->currentIndex());
    if (m_workspace == QLatin1String("horizontal"))
        prefs.setValue(QStringLiteral("layout"), state);
}

void BoardWindow::switchWorkspace(QString const& name)
{
    if (m_workspace == name || !workspaceIds.contains(name))
        return;
    saveWorkspaceSettings();
    m_workspace = name;
    loadWorkspace();
    settings().setValue(QStringLiteral("board/workspace/active"), name);
}

void BoardWindow::loadWorkspace()
{
    applyWorkspacePreset();
    QSettings prefs = settings();
    prefs.beginGroup(QStringLiteral("board/workspace"));
    auto const fallback = m_workspace == QLatin1String("horizontal") ? prefs.value(QStringLiteral("layout")) : QVariant();
    auto const state = prefs.value(workspaceKey(m_workspace) + QStringLiteral("/layout"), fallback).toByteArray();
    if (restoreState(state, layoutVersion)) {
        m_sizeDefaultOnMaximize = false;
        int const tab = prefs.value(workspaceKey(m_workspace) + QStringLiteral("/inspector"), m_inspectorTabs->currentIndex()).toInt();
        m_inspectorTabs->setCurrentIndex(std::clamp(tab, 0, m_inspectorTabs->count() - 1));
    }
}

void BoardWindow::restoreDefaultLayout()
{
    if (m_workspace != QLatin1String("horizontal"))
        saveWorkspaceSettings();
    m_workspace = QStringLiteral("horizontal");
    applyWorkspacePreset();
    saveWorkspaceSettings();
}

void BoardWindow::applyWorkspacePreset()
{
    bool const overview = m_workspace == QLatin1String("overview");
    restoreState(m_defaultLayout, layoutVersion);
    m_thumbnails->setOverview(overview);
    m_stage->show();
    showBoardView(QStringLiteral("captions"));
    showBoardView(QStringLiteral("thumbnails"));
    if (overview) {
        m_stage->hide();
        m_layerDock->hide();
        removeDockWidget(m_captionDock);
        addDockWidget(Qt::RightDockWidgetArea, m_thumbnailDock);
        splitDockWidget(m_thumbnailDock, m_captionDock, Qt::Horizontal);
        m_captionDock->show();
        m_inspectorTabs->setCurrentIndex(1);
    } else if (m_workspace == QLatin1String("vertical")) {
        addDockWidget(Qt::LeftDockWidgetArea, m_thumbnailDock);
    } else if (m_workspace == QLatin1String("drawing")) {
        m_captionDock->hide();
    } else if (m_workspace == QLatin1String("timeline")) {
        showBoardView(QStringLiteral("timeline"));
    }
    sizeReferenceLayout();
    m_sizeDefaultOnMaximize = !isMaximized();
    for (auto const& id : workspaceIds)
        m_actions->setChecked(QStringLiteral("workspace.") + id, id == m_workspace);
}

void BoardWindow::sizeReferenceLayout()
{
    if (m_workspace == QLatin1String("overview")) {
        resizeDocks({m_thumbnailDock, m_captionDock}, {width() * 69 / 100, width() * 31 / 100}, Qt::Horizontal);
        return;
    }
    int const percent = m_workspace == QLatin1String("vertical") || m_workspace == QLatin1String("timeline") ? 25 : 31;
    resizeDocks({m_layerDock, m_captionDock}, {width() * 18 / 100, width() * percent / 100}, Qt::Horizontal);
    if (m_workspace == QLatin1String("vertical"))
        resizeDocks({m_thumbnailDock}, {width() * 13 / 100}, Qt::Horizontal);
    else if (m_workspace == QLatin1String("timeline"))
        resizeDocks({m_timelineDock}, {height() * 32 / 100}, Qt::Vertical);
    else
        resizeDocks({m_thumbnailDock}, {height() * 23 / 100}, Qt::Vertical);
}
