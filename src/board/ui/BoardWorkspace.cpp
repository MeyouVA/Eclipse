#include "board/ui/BoardWindow.h"
#include "board/ui/BoardCaptions.h"
#include "board/ui/BoardLayers.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardThumbnails.h"
#include "board/ui/BoardTimeline.h"
#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QDockWidget>
#include <QStatusBar>
#include <QTabWidget>

void BoardWindow::buildWorkspace()
{
    m_stage = new BoardStage(&m_document, this);
    setCentralWidget(m_stage);
    connect(m_stage, &BoardStage::message, this, [this](QString const& text) {
        statusBar()->showMessage(text, 3000);
    });
    m_thumbnailDock = new QDockWidget(tr("Thumbnails"), this);
    m_thumbnailDock->setObjectName(QStringLiteral("BoardThumbnailDock"));
    m_thumbnailDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_thumbnails = new BoardThumbnails(&m_document, m_thumbnailDock);
    m_thumbnailDock->setWidget(m_thumbnails);
    addDockWidget(Qt::BottomDockWidgetArea, m_thumbnailDock);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    connect(m_thumbnails, &BoardThumbnails::panelSelected, this, [this](int shot, int panel) { selectPanel(shot, panel); });
    connect(m_thumbnails, &BoardThumbnails::panelDropped, this, &BoardWindow::dropPanel);
    connect(m_thumbnailDock, &QDockWidget::dockLocationChanged, m_thumbnails, [this](Qt::DockWidgetArea area) {
        if (area != Qt::NoDockWidgetArea)
            m_thumbnails->setVertical(area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);
    });

    m_layerDock = new QDockWidget(tr("Panel Layers"), this);
    m_layerDock->setObjectName(QStringLiteral("BoardLayerDock"));
    m_layers = new BoardLayers(&m_document, m_layerDock);
    m_layerDock->setWidget(m_layers);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    m_captionDock = new QDockWidget(tr("Inspector"), this);
    m_captionDock->setObjectName(QStringLiteral("BoardCaptionDock"));
    m_inspectorTabs = new QTabWidget(m_captionDock);
    m_inspectorTabs->setObjectName(QStringLiteral("BoardInspectorTabs"));
    m_captions = new BoardCaptions(&m_document, m_inspectorTabs);
    m_inspectorTabs->addTab(m_captions, tr("Panel"));
    m_outline = new QListView(m_inspectorTabs);
    m_outline->setObjectName(QStringLiteral("BoardPanelOutline"));
    m_outline->setModel(m_thumbnails->model());
    m_outline->setSelectionModel(m_thumbnails->selectionModel());
    m_outline->setIconSize(QSize(46, 46));
    m_outline->setUniformItemSizes(true);
    m_inspectorTabs->addTab(m_outline, tr("Storyboard"));
    m_captionDock->setWidget(m_inspectorTabs);
    splitDockWidget(m_layerDock, m_captionDock, Qt::Horizontal);
    connect(m_layers, &BoardLayers::layerSelected, m_stage, &BoardStage::chooseLayer);
    buildTimeline();
}

void BoardWindow::refreshWorkspace()
{
    m_playback->stop();
    if (m_captions->writingCaption())
        return;
    if (m_stage->writingArtwork()) {
        m_thumbnails->refreshPanel(m_selected.first, m_selected.second);
        m_layers->refreshSelectedPreview();
        return;
    }
    int const previousStart = m_timeline->panelStart(m_selected.first, m_selected.second);
    int const offset = previousStart < 0 ? 0 : m_timeline->frame() - previousStart;
    m_stage->hidePreview();
    m_timeline->setProject(m_document.project());
    m_playback->setLength(m_timeline->length());
    m_thumbnails->rebuild();
    auto const& shots = m_document.project().shots;
    if (shots.isEmpty()) {
        selectPanel(-1, -1);
        return;
    }
    int const shot = std::clamp(m_selected.first, 0, int(shots.size()) - 1);
    int const panel = std::clamp(m_selected.second, 0, int(shots[shot].panels.size()) - 1);
    int const start = m_timeline->panelStart(shot, panel);
    int const within = std::clamp(offset, 0, shots[shot].panels[panel].length - 1);
    m_playback->seek(start + within, false);
    m_timeline->setFrame(start + within, true);
    selectPanel(shot, panel, false, false);
    if (m_cameraPreview)
        showPlaybackFrame(start + within);
}

void BoardWindow::selectPanel(int shot, int panel, bool fit, bool syncTiming)
{
    if (syncTiming) {
        int const frame = std::max(0, m_timeline->panelStart(shot, panel));
        m_playback->seek(frame, false);
        m_timeline->setFrame(frame, true);
        m_stage->hidePreview();
    }
    m_selected = {shot, panel};
    int const layer = m_layers->showPanel(shot, panel);
    m_stage->showPanel(shot, panel, layer, fit);
    int const start = m_timeline->panelStart(shot, panel);
    if (start >= 0)
        m_stage->setCameraFrame(std::max(0, m_timeline->frame() - start));
    m_captions->showPanel(shot, panel);
    m_thumbnails->selectPanel(shot, panel);
    m_outline->scrollTo(m_thumbnails->currentIndex());
    m_outline->viewport()->update();
    updatePanelActions();
    if (syncTiming && m_cameraPreview)
        showPlaybackFrame(m_timeline->frame());
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
    enabled("BoardSplitProject", selected && shots.size() > 1);
    enabled("BoardSceneDetails", selected);
    bool const namesLocked = selected && shots[shot].namesLocked;
    enabled("BoardLockSceneNames", selected);
    m_actions->setChecked(QStringLiteral("BoardLockSceneNames"), namesLocked);
    enabled("BoardRenamePanel", selected && m_customPanelNames && !namesLocked);
    enabled("BoardRenameSequence", selected && !shots[shot].sequence.trimmed().isEmpty());
    enabled("BoardRenameAct", selected && !shots[shot].act.trimmed().isEmpty());
    enabled("BoardNewPanel", selected);
    enabled("BoardDuplicatePanel", selected);
    enabled("BoardRemovePanel", selected && shots[shot].panels.size() > 1);
    enabled("BoardSplitPanel", selected && shots[shot].panels[panel].length > 1);
    enabled("BoardCaptionAdd", selected);
    enabled("BoardCaptionRemove", selected);
    enabled("BoardPanelDuration", selected);
    enabled("BoardDissolve", selected && shot > 0);
    for (auto id : {"BoardImportImageLayers", "BoardImportImageScenes", "BoardExportFrame",
                    "BoardExportPdf", "BoardExportCsv", "BoardExportMovie", "BoardExportImages", "BoardExportAnimation"})
        enabled(id, selected);
    updatePlaybackActions();
    updateCameraActions();
}

void BoardWindow::chooseStageTool(QString const& tool)
{
    m_playback->stop();
    m_cameraPreview = false;
    m_actions->setChecked(QStringLiteral("BoardCameraPreview"), false);
    m_stage->hidePreview();
    m_stage->chooseTool(tool);
}
void BoardWindow::fitStage() { m_stage->fit(); }

void BoardWindow::regenerateThumbnails()
{
    m_thumbnails->refreshAll();
    statusBar()->showMessage(tr("Panel thumbnails refreshed."), 2500);
}

void BoardWindow::showBoardView(QString const& view)
{
    if (view == QLatin1String("layers"))
        m_layerDock->show();
    else if (view == QLatin1String("captions")) {
        m_captionDock->show();
        m_inspectorTabs->setCurrentWidget(m_captions);
    } else if (view == QLatin1String("timeline")) {
        m_timelineDock->show();
        m_timelineDock->raise();
    } else {
        m_thumbnailDock->show();
        m_thumbnailDock->raise();
    }
}

void BoardWindow::editCaptionFields(QString const& operation)
{
    m_captions->editFields(operation == QLatin1String("add"));
}
