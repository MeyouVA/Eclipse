#include "board/ui/BoardWindow.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardThumbnails.h"
#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QDockWidget>
#include <QStatusBar>

void BoardWindow::buildWorkspace()
{
    m_stage = new BoardStage(&m_document, this);
    setCentralWidget(m_stage);
    connect(m_stage, &BoardStage::message, this, [this](QString const& text) {
        statusBar()->showMessage(text, 3000);
    });
    m_thumbnailDock = new QDockWidget(tr("Thumbnails"), this);
    m_thumbnailDock->setObjectName(QStringLiteral("BoardThumbnailDock"));
    m_thumbnailDock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_thumbnails = new BoardThumbnails(&m_document, m_thumbnailDock);
    m_thumbnailDock->setWidget(m_thumbnails);
    addDockWidget(Qt::BottomDockWidgetArea, m_thumbnailDock);
    resizeDocks({m_thumbnailDock}, {145}, Qt::Vertical);
    connect(m_thumbnails, &BoardThumbnails::panelSelected, this, [this](int shot, int panel) { selectPanel(shot, panel); });
    connect(m_thumbnails, &BoardThumbnails::panelDropped, this, &BoardWindow::dropPanel);
}

void BoardWindow::refreshWorkspace()
{
    if (m_stage->writingArtwork()) {
        m_thumbnails->refreshPanel(m_selected.first, m_selected.second);
        return;
    }
    m_thumbnails->rebuild();
    auto const& shots = m_document.project().shots;
    if (shots.isEmpty()) {
        selectPanel(-1, -1);
        return;
    }
    int const shot = std::clamp(m_selected.first, 0, int(shots.size()) - 1);
    int const panel = std::clamp(m_selected.second, 0, int(shots[shot].panels.size()) - 1);
    selectPanel(shot, panel);
}

void BoardWindow::selectPanel(int shot, int panel, bool fit)
{
    m_selected = {shot, panel};
    m_stage->showPanel(shot, panel, fit);
    m_thumbnails->selectPanel(shot, panel);
    updatePanelActions();
}

void BoardWindow::updatePanelActions()
{
    auto enabled = [this](char const* id, bool value) {
        if (auto* action = m_actions->action(QLatin1String(id)))
            action->setEnabled(value);
    };
    auto const& shots = m_document.project().shots;
    auto const [shot, panel] = m_selected;
    bool const selected = shot >= 0 && shot < shots.size() && panel >= 0 && panel < shots[shot].panels.size();
    enabled("BoardNewScene", selected);
    enabled("BoardDuplicateScene", selected);
    enabled("BoardRemoveScene", selected && shots.size() > 1);
    enabled("BoardSplitScene", selected && panel > 0);
    enabled("BoardNewPanel", selected);
    enabled("BoardDuplicatePanel", selected);
    enabled("BoardRemovePanel", selected && shots[shot].panels.size() > 1);
    enabled("BoardSplitPanel", selected && shots[shot].panels[panel].length > 1);
}

void BoardWindow::chooseStageTool(QString const& tool) { m_stage->chooseTool(tool); }
void BoardWindow::fitStage() { m_stage->fit(); }
void BoardWindow::showThumbnails() { m_thumbnailDock->show(); }
