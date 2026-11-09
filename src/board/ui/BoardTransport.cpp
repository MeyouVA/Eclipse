#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardTimeline.h"
#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QDockWidget>
#include <QInputDialog>
#include <QStatusBar>

void BoardWindow::buildTimeline()
{
    m_timelineDock = new QDockWidget(tr("Timeline"), this);
    m_timelineDock->setObjectName(QStringLiteral("BoardTimelineDock"));
    m_timelineDock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_timeline = new BoardTimeline(m_timelineDock);
    m_timelineDock->setWidget(m_timeline);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    tabifyDockWidget(m_thumbnailDock, m_timelineDock);
    m_thumbnailDock->raise();
    m_playback = new BoardPlayback(&m_document, this);
    connect(m_timeline, &BoardTimeline::seek, m_playback, [this](int frame) { m_playback->seek(frame); });
    connect(m_timeline, &BoardTimeline::durationChanged, &m_document, &BoardDocument::setPanelLength);
    connect(m_playback, &BoardPlayback::frameChanged, this, &BoardWindow::showPlaybackFrame);
    connect(m_playback, &BoardPlayback::playingChanged, this, [this](bool playing) {
        if (!playing) {
            if (m_cameraPreview)
                showPlaybackFrame(m_timeline->frame());
            else
                m_stage->hidePreview();
        }
        updatePlaybackActions();
    });
    connect(m_playback, &BoardPlayback::message, this, [this](QString const& text) { statusBar()->showMessage(text, 4500); });
}

void BoardWindow::showPlaybackFrame(int frame)
{
    m_timeline->setFrame(frame, true);
    auto const* span = m_timeline->spanAt(frame);
    if (!span)
        return;
    auto const address = qMakePair(span->shot, span->panel);
    if (address != m_selected)
        selectPanel(span->shot, span->panel, false, false);
    auto const& shots = m_document.project().shots;
    int const dissolve = span->shot > 0 && span->panel == 0 ? std::min(span->length, shots[span->shot].dissolve) : 0;
    int const offset = frame - span->start;
    m_stage->setCameraFrame(offset);
    bool const throughCamera = m_cameraPreview || m_playback->playing();
    if (offset < dissolve && (throughCamera || !m_stage->cameraTool())) {
        auto const previous = qMakePair(span->shot - 1, int(shots[span->shot - 1].panels.size()) - 1);
        m_stage->showPreview(address, offset, previous, double(offset + 1) / dissolve);
    } else if (throughCamera) {
        m_stage->showPreview(address, offset, {-1, -1}, 1.0);
    } else {
        m_stage->hidePreview();
    }
    updateCameraActions();
}

void BoardWindow::updatePlaybackActions()
{
    bool const active = m_timeline->length() > 0;
    bool const hasSound = !m_document.project().sound.asset.isEmpty();
    auto enabled = [this](char const* id, bool on) {
        if (auto* action = m_actions->action(QLatin1String(id)))
            action->setEnabled(on);
    };
    enabled("BoardPlay", active);
    enabled("BoardStop", m_playback->playing());
    enabled("BoardFirstFrame", active);
    enabled("BoardPreviousFrame", active);
    enabled("BoardNextFrame", active);
    enabled("BoardLastFrame", active);
    for (auto id : {"BoardPanelFirst", "BoardPanelLast", "BoardPanelPrevious", "BoardPanelNext",
                    "BoardScenePrevious", "BoardSceneNext", "BoardSequencePrevious", "BoardSequenceNext",
                    "BoardGoToScene", "BoardGoToFrame"})
        enabled(id, active);
    enabled("BoardSoundImport", active);
    enabled("BoardSoundPosition", active && hasSound);
    enabled("BoardSoundMute", active && hasSound);
    enabled("BoardSoundRemove", active && hasSound);
    if (auto* soundImport = m_actions->action(QStringLiteral("BoardSoundImport")))
        soundImport->setText(hasSound ? tr("Replace Sound...") : tr("Import Sound..."));
    m_actions->setChecked(QStringLiteral("BoardPlay"), m_playback->playing());
    m_actions->setChecked(QStringLiteral("BoardSoundMute"), m_document.project().sound.muted);
}

void BoardWindow::useTransport(QString const& operation)
{
    if (operation == QLatin1String("play"))
        m_playback->toggle();
    else if (operation == QLatin1String("stop"))
        m_playback->stop();
    else if (operation == QLatin1String("first"))
        m_playback->seek(0);
    else if (operation == QLatin1String("last"))
        m_playback->seek(m_timeline->length() - 1);
    else if (operation == QLatin1String("previous") || operation == QLatin1String("next"))
        m_playback->seek(m_timeline->frame() + (operation == QLatin1String("previous") ? -1 : 1));
    else
        navigateBoard(operation);
}

void BoardWindow::loopPlayback(bool enabled) { m_playback->setLoop(enabled); }

void BoardWindow::editTiming(QString const& operation)
{
    auto const [shot, panel] = m_selected;
    auto const* current = m_document.panel(shot, panel);
    if (!current)
        return;
    m_playback->stop();
    bool accepted = false;
    if (operation == QLatin1String("duration")) {
        int const frames = QInputDialog::getInt(this, tr("Panel Duration"), tr("Frames"), current->length, 1, 1000000, 1, &accepted);
        if (accepted)
            m_document.setPanelLength(shot, panel, frames);
    } else if (operation == QLatin1String("zoom-in") || operation == QLatin1String("zoom-out")) {
        m_timeline->zoom(operation == QLatin1String("zoom-in") ? 1 : -1);
    } else if (shot > 0) {
        auto const& scene = m_document.project().shots[shot];
        int const maximum = scene.panels.first().length;
        int const frames = QInputDialog::getInt(this, tr("Scene Dissolve"), tr("Frames into this scene (0 for a cut)"),
            std::min(scene.dissolve, maximum), 0, maximum, 1, &accepted);
        if (accepted)
            m_document.setDissolve(shot, frames);
    }
}

void BoardWindow::navigateBoard(QString const& operation)
{
    auto const* span = m_timeline->spanAt(m_timeline->frame());
    if (!span)
        return;
    int const start = span->start;
    int const end = start + span->length;
    int const shot = span->shot;
    auto const& project = m_document.project();
    auto const& scenes = project.shots;
    int frame = -1;
    if (operation == QLatin1String("panel-first")) {
        frame = start;
    } else if (operation == QLatin1String("panel-last")) {
        frame = end - 1;
    } else if (operation == QLatin1String("panel-previous")) {
        if (auto const* previous = m_timeline->spanAt(start - 1))
            frame = previous->start;
    } else if (operation == QLatin1String("panel-next")) {
        if (end < m_timeline->length())
            frame = end;
    } else if (operation == QLatin1String("scene-previous")) {
        frame = m_timeline->panelStart(shot - 1, 0);
    } else if (operation == QLatin1String("scene-next")) {
        frame = m_timeline->panelStart(shot + 1, 0);
    } else if (operation == QLatin1String("sequence-previous")) {
        auto const group = BoardProject::SceneGroup::Sequence;
        int const previous = project.groupRange(shot, group).first - 1;
        if (previous >= 0)
            frame = m_timeline->panelStart(project.groupRange(previous, group).first, 0);
    } else if (operation == QLatin1String("sequence-next")) {
        frame = m_timeline->panelStart(project.groupRange(shot, BoardProject::SceneGroup::Sequence).second, 0);
    } else if (operation == QLatin1String("go-to-frame")) {
        m_playback->stop();
        bool accepted = false;
        int const number = QInputDialog::getInt(this, tr("Go to Frame"), tr("Project frame:"),
            m_timeline->frame() + 1, 1, m_timeline->length(), 1, &accepted);
        if (accepted)
            frame = number - 1;
    } else if (operation == QLatin1String("go-to-scene")) {
        m_playback->stop();
        QStringList choices;
        for (qsizetype i = 0; i < scenes.size(); ++i)
            choices.append(tr("%1 — %2").arg(i + 1).arg(scenes[i].name));
        bool accepted = false;
        QString const choice = QInputDialog::getItem(this, tr("Go to Scene"), tr("Scene:"), choices, shot, false, &accepted);
        if (accepted)
            frame = m_timeline->panelStart(int(choices.indexOf(choice)), 0);
    }
    if (frame >= 0)
        m_playback->seek(frame);
}
