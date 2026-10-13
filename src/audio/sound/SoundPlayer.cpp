#include "audio/sound/SoundPlayer.h"

#define NOMINMAX
#include <windows.h>

#include <mmsystem.h>

#include "audio/sound/Sound.h"

struct SoundPlayer::Impl {
    HWAVEOUT out = nullptr;
    WAVEHDR header{};
    QVector<qint16> buffer;  // what is playing: the device reads it until stopped
};

SoundPlayer::SoundPlayer() : m(new Impl) {}

SoundPlayer::~SoundPlayer()
{
    stop();
    delete m;
}

void SoundPlayer::stop()
{
    if (!m->out)
        return;
    waveOutReset(m->out);
    waveOutUnprepareHeader(m->out, &m->header, sizeof(WAVEHDR));
    waveOutClose(m->out);
    m->out = nullptr;
    m->buffer.clear();
}

static bool& silentSwitch()
{
    static bool on = false;
    return on;
}

void SoundPlayer::setSilent(bool silent)
{
    silentSwitch() = silent;
}

bool SoundPlayer::silent()
{
    return silentSwitch();
}

bool SoundPlayer::play(QVector<qint16> const& stereo, qint64 first, qint64 count)
{
    stop();
    if (silent())
        return true;
    qint64 const frames = stereo.size() / 2;
    if (first < 0 || first >= frames)
        return true;  // nothing to play is not a failure
    if (count < 0 || first + count > frames)
        count = frames - first;
    m->buffer = stereo.mid(first * 2, count * 2);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = SoundData::kRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = SoundData::kRate * 4;
    if (waveOutOpen(&m->out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        m->out = nullptr;
        m->buffer.clear();
        return false;
    }
    m->header = WAVEHDR{};
    m->header.lpData = reinterpret_cast<LPSTR>(m->buffer.data());
    m->header.dwBufferLength = DWORD(m->buffer.size() * 2);
    if (waveOutPrepareHeader(m->out, &m->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR
        || waveOutWrite(m->out, &m->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        waveOutClose(m->out);
        m->out = nullptr;
        m->buffer.clear();
        return false;
    }
    return true;
}
