#include "board/ui/BoardPlayback.h"
#include "board/model/BoardDocument.h"
#include "audio/sound/Sound.h"

#include <cmath>

BoardPlayback::BoardPlayback(BoardDocument* document, QObject* parent) : QObject(parent), m_document(document)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(4);
    connect(&m_timer, &QTimer::timeout, this, &BoardPlayback::advance);
}

void BoardPlayback::setLength(int length)
{
    m_length = length;
    m_frame = std::clamp(m_frame, 0, std::max(0, length - 1));
}

void BoardPlayback::seek(int frame, bool notify)
{
    stop();
    m_frame = std::clamp(frame, 0, std::max(0, m_length - 1));
    if (notify)
        emit frameChanged(m_frame);
}

void BoardPlayback::toggle()
{
    if (playing()) {
        stop();
        return;
    }
    if (m_length <= 0)
        return;
    auto const& project = m_document->project();
    QString error;
    if (!project.sound.asset.isEmpty() && !project.sound.muted) {
        m_sound = Sound::load(project.audioPath(m_document->path()), &error);
        if (!m_sound)
            emit message(tr("Could not preview the sound track: %1").arg(error));
    }
    m_origin = m_frame >= m_length - 1 ? 0 : m_frame;
    m_cycle = 0;
    m_soundStarted = false;
    m_frame = m_origin;
    m_clock.start();
    m_timer.start();
    emit playingChanged(true);
    startSound(m_frame);
    emit frameChanged(m_frame);
}

void BoardPlayback::stop()
{
    if (!playing())
        return;
    m_timer.stop();
    m_player.stop();
    m_sound.reset();
    emit playingChanged(false);
}

void BoardPlayback::advance()
{
    double const fps = m_document->project().fps;
    qint64 const absolute = m_origin + qint64(double(m_clock.nsecsElapsed()) * fps / 1e9);
    if (absolute >= m_length && !m_loop) {
        m_frame = m_length - 1;
        emit frameChanged(m_frame);
        stop();
        return;
    }
    qint64 const cycle = absolute / m_length;
    if (cycle != m_cycle) {
        m_cycle = cycle;
        m_soundStarted = false;
        m_player.stop();
    }
    int const frame = int(absolute % m_length);
    startSound(frame);
    if (frame != m_frame) {
        m_frame = frame;
        emit frameChanged(frame);
    }
}

void BoardPlayback::startSound(int frame)
{
    if (!m_sound || m_soundStarted)
        return;
    auto const& project = m_document->project();
    auto const& clip = project.sound;
    int const end = std::min(m_length, clip.start + clip.length);
    if (frame < clip.start || frame >= end)
        return;
    m_soundStarted = true;
    double const samplesPerFrame = double(SoundData::kRate) / project.fps;
    qint64 const first = std::llround((frame - clip.start) * samplesPerFrame);
    qint64 const last = std::llround((end - clip.start) * samplesPerFrame);
    if (!m_player.play(m_sound->samples, first, last - first))
        emit message(tr("The audio output could not be opened."));
}
