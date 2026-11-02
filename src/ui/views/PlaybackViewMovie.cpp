#include "ui/views/PlaybackView.h"
#include "ui/views/PlaybackMovie.h"
#include "model/media/MediaFormats.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QProgressDialog>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>
#include <cmath>

bool PlaybackView::loadMovie(QString const& path,QString* error)
{
    if(m_loading) { if(error) *error=tr("Another movie is being prepared."); return false; }
    if(error) error->clear(); stop();
    std::unique_ptr<PlaybackMovie> movie;
    {
        QScopedValueRollback<bool> loading(m_loading,true);
        QProgressDialog progress(tr("Preparing movie playback..."),tr("Cancel"),0,0,this);
        progress.setWindowModality(Qt::ApplicationModal); progress.setMinimumDuration(500);
        movie=PlaybackMovie::decode(path,[&] { QApplication::processEvents(); return !progress.wasCanceled(); },error);
    }
    if(!movie) return false;
    if(!loadFolder(movie->folder->path(),error)) return false;
    m_movie=std::move(movie); setRate(m_movie->rate); updateInfo(); return true;
}
void PlaybackView::chooseMovie()
{
    QString const path=QFileDialog::getOpenFileName(this,tr("Open Movie for Playback"),m_movie?m_movie->source:QString(),MediaFormats::videoFilter(),nullptr,QFileDialog::HideNameFilterDetails);
    if(path.isEmpty()) return;
    QString error; if(!loadMovie(path,&error) && !error.isEmpty()) report(error);
}
bool PlaybackView::hasAudio() const { return m_movie && !m_movie->audio.isEmpty(); }
QString PlaybackView::movieSource() const { return m_movie?m_movie->source:QString(); }
void PlaybackView::setVolume(int percent)
{
    m_volume->setValue(std::clamp(percent,0,100));
}
QVector<qint16> PlaybackView::playbackSound() const
{
    if(!hasAudio() || m_volume->value()==0 || m_direction<0 || m_speed->value()!=100 || std::abs(m_rate->value()-m_movie->rate)>.000001) return {};
    qint64 const first=qint64(m_frame/m_movie->rate*44100),end=std::min<qint64>(qint64((m_last->value())/m_movie->rate*44100),m_movie->audio.size()/2);
    if(first>=end) return {};
    auto samples=m_movie->audio.mid(first*2,(end-first)*2);
    if(m_volume->value()!=100) for(auto& sample : samples) sample=qint16(std::lround(sample*m_volume->value()/100.0));
    return samples;
}
void PlaybackView::startSound()
{
    m_player.stop(); auto const samples=playbackSound();
    if(!samples.isEmpty() && !m_player.play(samples,0)) report(tr("The default sound output could not be opened."));
}
