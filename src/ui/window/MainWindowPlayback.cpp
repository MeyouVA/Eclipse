// Eclipse - the main window: playback and going from frame to frame.
#include "ui/window/MainWindow.h"
#include "ui/views/PreviewManagerView.h"

#include <algorithm>
#include <cmath>

#include <QInputDialog>
#include <QSlider>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/LipSyncView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QSpinBox>

void MainWindow::startPlayingFrom(int from)
{
    m_playFrom = from;
    m_playClock.restart();
    if (m_soundOn && !m_playMix.isEmpty())
        m_player.play(m_playMix, qint64(from / m_document->scene().fps * SoundData::kRate));
}

// Play > Render and Play: every frame is rendered once and kept, then the scene plays from those.
void MainWindow::onActionRenderAndPlay()
{
    if (m_playTimer.isActive())
        stop();
    PreviewManagerView* const preview = findChild<PreviewManagerView*>();
    if (preview && preview->render() > 0)
        onActionTogglePlayback();
}

void MainWindow::onActionTogglePlayback()
{
    if (m_playTimer.isActive()) {
        stop();
        return;
    }
    // Starting from the last frame would stop at once: begin again.  And from outside the
    // frames playing runs between, begin at their start.
    if (frame() >= playStop() || frame() < m_playStart)
        m_timeline->setCurrentFrame(m_playStart);
    m_canvas->setPlaying(true);
    m_actions->setChecked(QStringLiteral("PlayToggle"), true);
    m_playMix = m_soundOn && m_playSpeed == 100 ? Sound::mix(m_document->scene()) : QVector<qint16>();
    startPlayingFrom(frame());
    // Look at the clock a few times per frame.
    m_playTimer.start(std::max(1, int(250.0 / m_document->scene().fps)));
}

void MainWindow::stop()
{
    m_playTimer.stop();
    m_player.stop();
    m_playMix.clear();
    m_canvas->setPlaying(false);
    m_actions->setChecked(QStringLiteral("PlayToggle"), false);
}

void MainWindow::advanceFrame()
{
    Scene const& scene = m_document->scene();
    int const last = playStop();
    int const due = m_playFrom + int(m_playClock.elapsed() * scene.fps * m_playSpeed / 100.0 / 1000.0);
    if (due <= last) {
        if (due != frame())
            m_timeline->setCurrentFrame(due);
    } else if (m_loop) {
        m_timeline->setCurrentFrame(m_playStart);
        startPlayingFrom(m_playStart);
    } else {
        m_timeline->setCurrentFrame(last);
        stop();
    }
}

void MainWindow::setPlaybackSpeed(int percent)
{
    percent = std::clamp(percent, 5, 800);
    if (percent == m_playSpeed)
        return;
    bool const playing = m_playTimer.isActive();
    if (playing)
        stop();
    m_playSpeed = percent;
    if (playing)
        onActionTogglePlayback();
}

void MainWindow::onActionPlaybackSpeed()
{
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Playback Speed"), tr("Speed, as a percentage of the scene's frame rate\n(the sound plays only at 100):"),
                                             m_playSpeed, 5, 800, 5, &ok);
    if (ok)
        setPlaybackSpeed(percent);
}

int MainWindow::playStop() const
{
    int const last = m_document->scene().frameCount() - 1;
    return m_playStop < 0 ? last : std::min(m_playStop, last);
}

void MainWindow::setPlayRange(int start, int stop)
{
    int const last = m_document->scene().frameCount() - 1;
    start = std::clamp(start, 0, last);
    stop = stop < 0 || stop >= last ? -1 : std::max(stop, start);
    m_playStart = start;
    m_playStop = stop;
    QSignalBlocker const a(m_startBox), b(m_stopBox);
    m_startBox->setValue(m_playStart + 1);
    m_stopBox->setValue(playStop() + 1);
    m_timeline->setPlayRange(m_playStart, m_playStop);
}

void MainWindow::onActionPlaybackStartFrame()
{
    bool ok = false;
    int const f = QInputDialog::getInt(this, tr("Playback Start Frame"), tr("Start playing at frame:"), m_playStart + 1, 1, playStop() + 1, 1, &ok);
    if (ok)
        setPlayRange(f - 1, m_playStop);
}

void MainWindow::onActionPlaybackStopFrame()
{
    bool ok = false;
    int const f = QInputDialog::getInt(this, tr("Playback Stop Frame"), tr("Stop playing at frame:"), playStop() + 1, m_playStart + 1,
                                       m_document->scene().frameCount(), 1, &ok);
    if (ok)
        setPlayRange(m_playStart, f - 1);
}

void MainWindow::onActionFirstFrame()
{
    m_timeline->setCurrentFrame(0);
}

void MainWindow::onActionLastFrame()
{
    m_timeline->setCurrentFrame(m_document->scene().frameCount() - 1);
}

void MainWindow::onActionGotoFrame()
{
    bool ok = false;
    int const target = QInputDialog::getInt(this, tr("Go to Frame"), tr("Frame:"), frame() + 1, 1, m_document->scene().frameCount(), 1, &ok);
    if (ok)
        m_timeline->setCurrentFrame(target - 1);
}

void MainWindow::stepFrame(int delta)
{
    m_timeline->setCurrentFrame(frame() + delta);
}

void MainWindow::stepDrawing(int direction)
{
    Layer const& l = m_document->scene().layers[layer()];
    int const target = direction < 0 ? l.previousDrawingFrame(frame()) : l.nextDrawingFrame(frame());
    if (target >= 0)
        m_timeline->setCurrentFrame(target);
}

void MainWindow::shuttleStep()
{
    Scene const& scene = m_document->scene();
    if (!m_shuttle || scene.frameCount() < 1)
        return;
    if (int(std::lround(m_shuttleAt)) != frame())
        m_shuttleAt = frame();      // the frame was changed some other way meanwhile
    // Up to four times the scene's speed at the end of its travel, gently near the middle.
    double const pull = m_shuttle->value() / 100.0;
    double const framesPerSecond = pull * std::abs(pull) * 4.0 * scene.fps;
    m_shuttleAt = std::clamp(m_shuttleAt + framesPerSecond * 0.04, 0.0, double(scene.frameCount() - 1));
    int const to = int(std::lround(m_shuttleAt));
    if (to != frame())
        m_timeline->setCurrentFrame(to);
}
