#include "board/ui/BoardWindow.h"
#include "board/ui/BoardDockTitle.h"
#include "board/ui/BoardLayers.h"
#include "board/ui/BoardLibrary.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardThumbnails.h"
#include "board/ui/BoardTimeline.h"
#include "board/ui/BoardCaptions.h"
#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QDockWidget>
#include <QListView>
#include <QMenu>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

namespace {
void focusInside(QWidget* target)
{
    if (target->focusPolicy() != Qt::NoFocus) {
        target->setFocus(Qt::OtherFocusReason);
        return;
    }
    for (auto* child : target->findChildren<QWidget*>())
        if (child->isVisible() && child->isEnabled() && (child->focusPolicy() & Qt::TabFocus)) {
            child->setFocus(Qt::OtherFocusReason);
            return;
        }
}

struct ViewDock {
    char const* view;
    char const* action;
};
constexpr ViewDock viewDocks[]{{"thumbnails", "BoardViewThumbnails"}, {"layers", "BoardViewLayers"},
                               {"inspector", "BoardViewInspector"}, {"timeline", "BoardViewTimeline"}};
}

QDockWidget* BoardWindow::dockFor(QString const& view) const
{
    if (view == QLatin1String("layers"))
        return m_layerDock;
    if (view == QLatin1String("inspector"))
        return m_captionDock;
    if (view == QLatin1String("timeline"))
        return m_timelineDock;
    return m_thumbnailDock;
}

void BoardWindow::buildViewMenus()
{
    auto addActions = [this](QMenu* menu, std::initializer_list<char const*> ids) {
        for (auto const* id : ids) {
            if (!id)
                menu->addSeparator();
            else if (auto* action = m_actions->action(QLatin1String(id)))
                menu->addAction(action);
        }
    };
    m_thumbnailDock->setTitleBarWidget(new BoardDockTitle(m_thumbnailDock, [addActions](QMenu* menu) {
        addActions(menu, {"BoardNewPanel", "BoardNewScene", "BoardDuplicatePanel", "BoardRemovePanel", nullptr, "BoardRefreshThumbnails"});
    }));
    m_layerDock->setTitleBarWidget(new BoardDockTitle(m_layerDock, [this](QMenu* menu) { m_layers->populateMenu(menu); }));
    m_captionDock->setTitleBarWidget(new BoardDockTitle(m_captionDock, [this, addActions](QMenu* menu) {
        for (int i = 0; i < m_inspectorTabs->count(); ++i) {
            QAction* tab = menu->addAction(m_inspectorTabs->tabText(i));
            tab->setCheckable(true);
            tab->setChecked(i == m_inspectorTabs->currentIndex());
            connect(tab, &QAction::triggered, m_inspectorTabs, [tabs = m_inspectorTabs, i] { tabs->setCurrentIndex(i); });
        }
        menu->addSeparator();
        addActions(menu, {"BoardCaptionAdd", "BoardCaptionRemove", "BoardFindCaption"});
    }));
    m_timelineDock->setTitleBarWidget(new BoardDockTitle(m_timelineDock, [addActions](QMenu* menu) {
        addActions(menu, {"BoardTimelineZoomIn", "BoardTimelineZoomOut", nullptr, "BoardPanelDuration", "BoardSoundImport"});
    }));
    for (auto const& entry : viewDocks) {
        QDockWidget* dock = dockFor(QLatin1String(entry.view));
        dock->addActions(actions());
        QString const id = QLatin1String(entry.action);
        m_actions->setChecked(id, !dock->isHidden());
        connect(dock->toggleViewAction(), &QAction::toggled, this, [this, id, dock](bool on) {
            m_actions->setChecked(id, on);
            // Opening a view of a collapsed region by hand ends the collapse.
            if (on && m_collapsedSide.contains(dock)) {
                m_collapsedSide.clear();
                m_actions->setChecked(QStringLiteral("BoardCollapseSide"), false);
            }
            if (on && m_collapsedBottom.contains(dock)) {
                m_collapsedBottom.clear();
                m_actions->setChecked(QStringLiteral("BoardCollapseBottom"), false);
            }
        });
    }
    auto action = [this](char const* id) { return m_actions->action(QLatin1String(id)); };
    m_stage->setViewActions({action("BoardFitStage"), action("BoardCameraGuide"), action("BoardCameraPreview"),
        action("BoardStageZoomOut"), action("BoardStageZoomIn"), action("BoardStageRotateCCW"), action("BoardStageRotateCW"),
        action("BoardStageMirror")});
    m_stage->setViewMenuActions({action("BoardFitStage"), action("BoardStageZoomIn"), action("BoardStageZoomOut"),
        action("BoardStageResetZoom"), action("BoardStageFrameSelection"), nullptr, action("BoardStageRotateCW"),
        action("BoardStageRotateCCW"), action("BoardStageResetRotation"), action("BoardStageMirror"), action("BoardStageResetPan"),
        action("BoardStageResetView"), nullptr, action("BoardCollapseSide"), action("BoardCollapseBottom"), nullptr,
        action("BoardCameraGuide"), action("BoardCameraPreview")});
    connect(m_stage, &BoardStage::viewChanged, this, [this] { m_actions->setChecked(QStringLiteral("BoardStageMirror"), m_stage->mirrored()); });
}

void BoardWindow::collapseViews(QString const& region)
{
    bool const side = region == QLatin1String("side");
    QString const id = side ? QStringLiteral("BoardCollapseSide") : QStringLiteral("BoardCollapseBottom");
    QList<QDockWidget*>& collapsed = side ? m_collapsedSide : m_collapsedBottom;
    QAction* action = m_actions->action(id);
    if (action && action->isChecked()) {
        collapsed.clear();
        for (auto* dock : {m_thumbnailDock, m_layerDock, m_captionDock, m_timelineDock}) {
            Qt::DockWidgetArea const area = dockWidgetArea(dock);
            bool const inRegion = side ? (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
                                       : (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea);
            if (inRegion && !dock->isFloating() && !dock->isHidden())
                collapsed << dock;
        }
        QList<QDockWidget*> const hiding = collapsed;
        for (auto* dock : hiding)
            dock->hide();
        collapsed = hiding;
        if (collapsed.isEmpty())
            m_actions->setChecked(id, false);
    } else {
        QList<QDockWidget*> const showing = collapsed;
        collapsed.clear();
        for (auto* dock : showing)
            dock->show();
        if (showing.contains(m_thumbnailDock))
            m_thumbnailDock->raise();
    }
}

void BoardWindow::clearCollapsed()
{
    m_collapsedSide.clear();
    m_collapsedBottom.clear();
    m_actions->setChecked(QStringLiteral("BoardCollapseSide"), false);
    m_actions->setChecked(QStringLiteral("BoardCollapseBottom"), false);
}

void BoardWindow::syncViewActions()
{
    for (auto const& entry : viewDocks)
        m_actions->setChecked(QLatin1String(entry.action), !dockFor(QLatin1String(entry.view))->isHidden());
}

void BoardWindow::showBoardView(QString const& view)
{
    revealView(view, true);
}

void BoardWindow::revealView(QString const& view, bool focus)
{
    auto reveal = [focus](QDockWidget* dock, QWidget* target) {
        dock->show();
        dock->raise();
        if (focus) {
            dock->window()->activateWindow();
            focusInside(target);
        }
    };
    if (view == QLatin1String("stage")) {
        m_stage->show();
        if (focus) {
            activateWindow();
            m_stage->focusCanvas();
        }
    } else if (view == QLatin1String("layers")) {
        reveal(m_layerDock, m_layers);
    } else if (view == QLatin1String("timeline")) {
        reveal(m_timelineDock, m_timeline);
    } else if (view == QLatin1String("thumbnails")) {
        reveal(m_thumbnailDock, m_thumbnails);
    } else {
        QWidget* page = m_captions;
        if (view == QLatin1String("library"))
            page = m_library;
        else if (view == QLatin1String("storyboard"))
            page = m_outline;
        else if (view == QLatin1String("toolproperties"))
            page = m_toolProperties;
        m_inspectorTabs->setCurrentWidget(page);
        reveal(m_captionDock, page);
    }
}

void BoardWindow::toggleBoardView(QString const& view)
{
    for (auto const& entry : viewDocks) {
        if (view != QLatin1String(entry.view))
            continue;
        QAction* action = m_actions->action(QLatin1String(entry.action));
        QDockWidget* dock = dockFor(view);
        if (action && action->isChecked())
            revealView(view == QLatin1String("inspector") ? QStringLiteral("captions") : view, false);
        else
            dock->close();
    }
}

void BoardWindow::populateToolbarMenu(QMenu* menu)
{
    menu->clear();
    QStringList customizable;
    for (QString const& id : m_actions->toolbarIds()) {
        if (auto* bar = findChild<QToolBar*>(id, Qt::FindDirectChildrenOnly)) {
            menu->addAction(bar->toggleViewAction());
            if (m_actions->toolbarCustomizable(id))
                customizable << id;
        }
    }
    if (customizable.isEmpty())
        return;
    menu->addSeparator();
    QMenu* customize = menu->addMenu(tr("Customize Toolbar"));
    for (QString const& id : customizable) {
        QAction* entry = customize->addAction(m_actions->toolbarText(id) + QStringLiteral("..."));
        entry->setObjectName(QStringLiteral("BoardCustomize.") + id);
        connect(entry, &QAction::triggered, this, [this, id] { m_actions->customizeToolbar(id, this); });
    }
}

void BoardWindow::stageView(QString const& command)
{
    m_stage->viewCommand(command);
}

void BoardWindow::mirrorStage(bool mirrored)
{
    m_stage->setMirrored(mirrored);
}

void BoardWindow::storeView()
{
    if (m_document.path().isEmpty())
        return;
    BoardView view = m_stage->view();
    view.shot = std::max(0, m_selected.first);
    view.panel = std::max(0, m_selected.second);
    m_document.setView(view);
}

void BoardWindow::restoreView()
{
    BoardView const view = m_document.project().view;
    selectPanel(view.shot, view.panel, false);
    m_actions->setChecked(QStringLiteral("BoardStageMirror"), view.mirror);
    QTimer::singleShot(0, this, [this, path = m_document.path(), view] {
        if (m_document.path() == path)
            m_stage->applyView(view);
    });
}
