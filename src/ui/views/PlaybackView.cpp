#include "ui/views/PlaybackView.h"
#include "ui/views/PlaybackSurface.h"
#include "ui/views/PlaybackMovie.h"
#include "model/scene/PlaybackTime.h"
#include "model/media/RawPixels.h"
#include "render/images/ImageFiles.h"
#include <QAction>
#include <QActionGroup>
#include <QCollator>
#include <QColorSpace>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QLocale>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

PlaybackView::~PlaybackView() { stop(); }

bool PlaybackView::loadFolder(QString const& folder,QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return false; };
    if(m_loading) return fail(tr("Another movie is being prepared."));
    QDir dir(folder); if(!dir.exists()) return fail(tr("The frame folder does not exist."));
    QStringList files;
    for(auto const& entry : dir.entryInfoList(QDir::Files|QDir::Readable,QDir::NoSort)) {
        auto const* format=ImageFiles::find(entry.suffix().toLower());
        if(format && format->readable && !RawPixels::needsSettings(entry.absoluteFilePath())) files << entry.absoluteFilePath();
    }
    if(files.isEmpty()) return fail(tr("This folder has no supported picture frames."));
    if(files.size()>100000) return fail(tr("Load at most 100000 picture frames at once."));
    QCollator order(QLocale(QLocale::English,QLocale::UnitedStates)); order.setNumericMode(true); order.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(),files.end(),[&](QString const& a,QString const& b) { int const compared=order.compare(QFileInfo(a).fileName(),QFileInfo(b).fileName()); return compared==0?a<b:compared<0; });
    QString why; QImage first=ImageFiles::load(files.first(),&why);
    if(first.isNull()) return fail(tr("Cannot read %1: %2").arg(QFileInfo(files.first()).fileName(),why));
    if(m_resolution!=100) first=first.scaled(std::max(1,first.width()*m_resolution/100),std::max(1,first.height()*m_resolution/100),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    stop(); m_cache.clear(); m_files=files; m_folder=dir.absolutePath(); m_frame=0; m_movie.reset(); m_preview.clear(); m_previewSound.clear();
    m_cache.insert(0,new QImage(first),std::max(1,int((first.sizeInBytes()+1023)/1024)));
    QSignalBlocker a(m_frameBox),b(m_first),c(m_last),d(m_slider);
    for(auto* box : {m_frameBox,m_first,m_last}) box->setRange(1,int(files.size()));
    m_first->setValue(1); m_last->setValue(int(files.size())); m_frameBox->setValue(1); m_slider->setRange(0,int(files.size())-1); m_slider->setValue(0);
    m_surface->setImage(first); m_surface->fit(); m_surface->setFocus(); updateInfo(); return true;
}

bool PlaybackView::reload(QString* error)
{
    if(m_loading) { if(error) *error=tr("Another movie is being prepared."); return false; }
    if(previewLoaded()) { stop(); m_cache.clear(); setFrame(m_frame); return true; }
    if(m_folder.isEmpty()) { if(error) *error=tr("Load a frame folder or movie first."); return false; }
    int const frame=m_frame,first=m_first->value()-1,last=m_last->value()-1;
    bool const toEnd=last==frameCount()-1; QString const current=m_files.value(frame);
    QString const movie=m_movie?m_movie->source:QString(); double const oldRate=m_rate->value();
    if(movie.isEmpty()?!loadFolder(m_folder,error):!loadMovie(movie,error)) return false;
    setRate(oldRate);
    setRange(first,toEnd?frameCount()-1:std::min(last,frameCount()-1));
    int const found=m_files.indexOf(current); setFrame(found>=0?found:std::min(frame,frameCount()-1)); return true;
}
void PlaybackView::chooseFolder()
{
    QString const folder=QFileDialog::getExistingDirectory(this,tr("Load Picture Frames"),m_folder);
    if(folder.isEmpty()) return;
    QString error; if(!loadFolder(folder,&error)) report(error);
}
void PlaybackView::unload()
{
    if(m_loading) return;
    stop(); m_files.clear(); m_folder.clear(); m_cache.clear(); m_frame=0; m_surface->setImage({}); m_movie.reset(); m_preview.clear(); m_previewSound.clear();
    QSignalBlocker a(m_frameBox),b(m_first),c(m_last),d(m_slider);
    for(auto* box : {m_frameBox,m_first,m_last}) { box->setRange(1,1); box->setValue(1); }
    m_slider->setRange(0,0); updateInfo();
}
QImage PlaybackView::read(int frame,QString* error)
{
    if(auto* image=m_cache.object(frame)) return *image;
    QImage image=previewLoaded()?m_preview[frame]:ImageFiles::load(m_files[frame],error);
    if(!image.isNull() && m_resolution!=100) image=image.scaled(std::max(1,image.width()*m_resolution/100),std::max(1,image.height()*m_resolution/100),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(!image.isNull()) m_cache.insert(frame,new QImage(image),std::max(1,int((image.sizeInBytes()+1023)/1024)));
    return image;
}
QImage PlaybackView::currentImage() const { return m_surface->image(); }
void PlaybackView::setFrame(int frame)
{
    if(m_files.isEmpty() || m_loading) return;
    frame=std::clamp(frame,0,frameCount()-1);
    QString error; QImage const image=read(frame,&error);
    if(image.isNull()) { stop(); updateInfo(); report(tr("Cannot read %1: %2").arg(QFileInfo(m_files[frame]).fileName(),error)); return; }
    m_frame=frame; m_surface->setImage(image);
    if(playing() && !m_advancing) {
        if(frame<m_first->value()-1 || frame>m_last->value()-1) stop();
        else restartAtCurrent();
    }
    updateInfo();
}
void PlaybackView::setRange(int first,int last)
{
    if(m_files.isEmpty() || m_loading) return;
    first=std::clamp(first,0,frameCount()-1); last=std::clamp(last,first,frameCount()-1);
    QSignalBlocker a(m_first),b(m_last); m_first->setValue(first+1); m_last->setValue(last+1);
    setFrame(std::clamp(m_frame,first,last));
}
void PlaybackView::setRate(double fps) { if(std::isfinite(fps)) m_rate->setValue(std::clamp(fps,.001,1000.0)); }
void PlaybackView::setLoop(bool on)
{
    m_loop=on; if(auto* action=findChild<QAction*>(QStringLiteral("PlaybackLoop"))) action->setChecked(on);
}
void PlaybackView::setCacheMegabytes(int megabytes)
{
    megabytes=std::clamp(megabytes,1,2048); m_cacheMegabytes=megabytes; m_cache.setMaxCost(m_cacheEnabled?megabytes*1024:0);
    if(auto* box=findChild<QSpinBox*>(QStringLiteral("PlaybackCacheMB"))) { QSignalBlocker block(box); box->setValue(megabytes); }
    if(m_info) updateInfo();
}
void PlaybackView::setCacheEnabled(bool enabled)
{
    m_cacheEnabled=enabled; m_cache.setMaxCost(enabled?m_cacheMegabytes*1024:0);
    if(auto* action=findChild<QAction*>(QStringLiteral("PlaybackCacheEnabled"))) action->setChecked(enabled);
    updateInfo();
}
void PlaybackView::setResolution(int percent)
{
    if(m_loading || (percent!=100 && percent!=50 && percent!=25) || percent==m_resolution) return;
    stop(); m_resolution=percent; m_cache.clear();
    if(auto* action=findChild<QAction*>(QStringLiteral("PlaybackResolution%1").arg(percent))) action->setChecked(true);
    if(!m_files.isEmpty()) setFrame(m_frame);
    updateInfo();
}
void PlaybackView::play(int direction)
{
    if(m_files.isEmpty() || m_single || m_loading) return;
    stop(); m_direction=direction<0?-1:1;
    int const first=m_first->value()-1,last=m_last->value()-1;
    if(m_frame<first || m_frame>last || (m_direction<0?m_frame==first:m_frame==last)) {
        int const target=m_direction<0?last:first;
        setFrame(target); if(m_frame!=target) return;
    }
    m_initialPreroll=m_preroll; m_padding=m_initialPreroll>0; m_surface->setBlank(m_padding);
    m_from=m_frame; m_clock.restart(); m_audioCycle=0; m_audioHolding=false; m_timer.start(); startSound(); updateInfo();
}
void PlaybackView::stop() { m_timer.stop(); m_player.stop(); m_padding=false; if(m_surface) m_surface->setBlank(false); if(m_info) updateInfo(); }
void PlaybackView::tick()
{
    auto const timed=PlaybackTime::preview(m_first->value()-1,m_last->value()-1,m_from,m_direction,m_rate->value()*m_speed->value()/100.0,m_clock.elapsed()/1000.0,m_loopPause,m_loop,m_preroll,m_initialPreroll);
    auto const& at=timed.position; bool const wasSilent=m_padding || m_audioHolding;
    m_padding=timed.blank; m_surface->setBlank(m_padding);
    QScopedValueRollback<bool> advancing(m_advancing,true);
    if(at.frame!=m_frame) setFrame(at.frame);
    if((at.holding || m_padding) && !wasSilent) m_player.stop();
    bool const resume=playing() && !at.ended && !at.holding && !m_padding && (at.cycle!=m_audioCycle || wasSilent);
    m_audioHolding=at.holding; m_audioCycle=at.cycle;
    if(resume) startSound();
    if(timed.blank || wasSilent) updateInfo();
    if(at.ended) stop();
}
void PlaybackView::hideEvent(QHideEvent* event) { stop(); QDialog::hideEvent(event); }
void PlaybackView::updateInfo()
{
    QSignalBlocker a(m_frameBox),b(m_slider); m_frameBox->setValue(m_frame+1); m_slider->setValue(m_frame);
    if(m_files.isEmpty()) { m_info->setText(tr("No frames loaded. Each picture file supplies one frame.")); return; }
    m_info->setText(tr("%1 / %2  |  %3 s / %4 s  |  %5  |  Cache %6 MB / %7 MB%8")
        .arg(m_frame+1).arg(frameCount()).arg(m_frame/m_rate->value(),0,'f',3).arg(frameCount()/m_rate->value(),0,'f',3)
        .arg(QFileInfo(m_files[m_frame]).fileName()).arg(m_cache.totalCost()/1024.0,0,'f',1).arg(m_cache.maxCost()/1024).arg(playing()?(m_padding?tr("  |  Blank preroll"):tr("  |  Playing")):QString()));
}
void PlaybackView::report(QString const& error) { m_info->setText(error); m_info->setToolTip(error); }
QString PlaybackView::properties() const
{
    if(m_files.isEmpty()) return tr("No frames loaded.");
    QImage const image=currentImage(); qint64 bytes=0;
    if(previewLoaded()) for(auto const& frame : m_preview) bytes+=frame.sizeInBytes();
    else for(auto const& file : m_files) bytes+=QFileInfo(file).size();
    QString const origin=m_movie?tr("Movie: %1\nNative frame rate: %2 fps\nPlayback audio: %3\nVolume: %4%\n").arg(m_movie->source).arg(m_movie->rate,0,'g',12).arg(hasAudio()?tr("Stereo, 44100 Hz"):tr("None")).arg(m_volume->value()):previewLoaded()?tr("Rendered preview snapshot\nSnapshot pixels: %1 bytes\nNative frame rate: %2 fps\n").arg(bytes).arg(m_previewRate,0,'g',12):QString();
    return origin+tr("Folder: %1\nFrames: %2\nCurrent file: %3\nCurrent dimensions: %4 x %5\nCurrent pixel depth: %6 bits\nAlpha channel: %7\nColour profile: %8\nFrame rate: %9 fps\nSource files: %10 bytes\nCached pixels: %11 bytes\nCache budget: %12 bytes\nEach file supplies its first picture; dimensions may vary between files.")
        .arg(QDir::toNativeSeparators(m_folder)).arg(frameCount()).arg(QFileInfo(m_files[m_frame]).fileName()).arg(image.width()).arg(image.height()).arg(image.depth())
        .arg(image.hasAlphaChannel()?tr("Yes"):tr("No")).arg(image.colorSpace().isValid()?image.colorSpace().description():tr("Unspecified")).arg(m_rate->value(),0,'g',9).arg(bytes).arg(cacheBytes()).arg(qint64(m_cache.maxCost())*1024);
}
