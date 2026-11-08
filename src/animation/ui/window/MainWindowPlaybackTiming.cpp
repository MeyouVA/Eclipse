#include "animation/ui/window/MainWindow.h"
#include "model/scene/PlaybackTime.h"
#include "model/document/Commands.h"
#include "audio/sound/Sound.h"
#include "animation/ui/timeline/Timeline.h"
#include <QScopedValueRollback>
#include <QSlider>

void MainWindow::advanceFrame()
{
    if(!m_playTimer.isActive()) return;
    auto const at=PlaybackTime::at(m_playStart,playStop(),m_playFrom,m_playDirection,
        m_document->scene().fps*m_playSpeed/100.0,m_playClock.elapsed()/1000.0,m_loopPause,m_loop);
    QScopedValueRollback<bool> changing(m_playAdvancing,true);
    if(at.frame!=frame()) m_timeline->setCurrentFrame(at.frame);
    if(at.ended) { stop(); return; }
    if(at.holding && !m_playHolding) m_player.stop();
    if(!at.holding && (at.cycle!=m_playCycle || m_playHolding) && m_soundOn && !m_playMix.isEmpty())
        m_player.play(m_playMix,qint64(at.frame/m_document->scene().fps*SoundData::kRate));
    m_playCycle=at.cycle;
    m_playHolding=at.holding;
}

void MainWindow::onActionJogForward() { stop(); stepFrame(1); }
void MainWindow::onActionJogBackward() { stop(); stepFrame(-1); }

void MainWindow::onActionShuttleReset()
{
    m_shuttleTimer.stop();
    m_shuttleClock.invalidate();
    if(m_shuttle) m_shuttle->setValue(0);
}

void MainWindow::changeShuttle(int direction)
{
    if(m_playTimer.isActive()) stop();
    if(!m_shuttle) return;
    m_shuttleAt=frame();
    m_shuttleClock.start();
    m_shuttle->setValue(std::clamp(m_shuttle->value()+direction*25,-100,100));
    if(m_shuttle->value()) m_shuttleTimer.start();
    else m_shuttleTimer.stop();
}

void MainWindow::onActionShuttleForward() { changeShuttle(1); }
void MainWindow::onActionShuttleBackward() { changeShuttle(-1); }
