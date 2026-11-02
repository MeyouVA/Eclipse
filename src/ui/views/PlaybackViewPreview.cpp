#include "ui/views/PlaybackView.h"
#include "ui/views/PlaybackSurface.h"
#include "ui/views/PlaybackMovie.h"
#include "actions/theme/AppSettings.h"
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <algorithm>
#include <cmath>

bool PlaybackView::loadPreview(QVector<QImage> const& frames,QVector<qint16> const& audio,double fps,QString* error)
{
    if(m_loading || frames.isEmpty() || frames.size()>100000 || !std::isfinite(fps) || fps<.001 || fps>1000 || audio.size()%2 || std::any_of(frames.cbegin(),frames.cend(),[](QImage const& image) { return image.isNull(); })) {
        if(error) *error=tr("Playback requires a complete preview, a valid rate and stereo sound."); return false;
    }
    stop(); m_cache.clear(); m_movie.reset(); m_folder.clear(); m_preview=frames; m_previewSound=audio; m_previewRate=fps;
    m_files.clear(); for(qsizetype i=0;i<frames.size();++i) m_files<<tr("Preview %1").arg(i+1);
    m_frame=0;
    QSignalBlocker a(m_frameBox),b(m_first),c(m_last),d(m_slider);
    for(auto* box : {m_frameBox,m_first,m_last}) box->setRange(1,int(frames.size()));
    m_first->setValue(1); m_last->setValue(int(frames.size())); m_frameBox->setValue(1); m_slider->setRange(0,int(frames.size())-1); m_slider->setValue(0);
    setRate(fps); setFrame(0); m_surface->fit(); m_surface->setFocus(); updateInfo(); return true;
}
void PlaybackView::applyPreferences()
{
    auto const prefs=settings();
    setCacheMegabytes(std::clamp(prefs.value(QStringLiteral("prefs/playbackCacheMb"),256).toInt(),1,2048));
    setCacheEnabled(prefs.value(QStringLiteral("prefs/playbackCacheEnabled"),true).toBool());
    int const resolution=prefs.value(QStringLiteral("prefs/playbackResolution"),100).toInt();
    setResolution(resolution==25 || resolution==50?resolution:100);
}
