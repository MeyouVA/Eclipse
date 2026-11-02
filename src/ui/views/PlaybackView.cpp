#include "ui/views/PlaybackView.h"
#include "ui/views/PlaybackSurface.h"
#include "model/scene/PlaybackTime.h"
#include "model/media/RawPixels.h"
#include "render/images/ImageFiles.h"
#include <QAction>
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

PlaybackView::PlaybackView(double fps,QWidget* parent):QDialog(parent)
{
    setObjectName(QStringLiteral("PlaybackView")); setWindowTitle(tr("Playback")); resize(1000,700);
    auto* layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    auto* bar=new QMenuBar(this); layout->setMenuBar(bar);
    auto* file=bar->addMenu(tr("File")); auto* playMenu=bar->addMenu(tr("Play")); auto* view=bar->addMenu(tr("View"));
    auto* toolbar=new QToolBar(this); toolbar->setObjectName(QStringLiteral("PlaybackControls"));
    auto add=[this](QMenu* menu,QString const& text,QString const& name,auto callback) {
        auto* action=menu->addAction(text); action->setObjectName(name); connect(action,&QAction::triggered,this,callback); return action;
    };
    auto* load=add(file,tr("Load From Folder..."),QStringLiteral("PlaybackLoad"),[this] { chooseFolder(); });
    auto* refresh=add(file,tr("Reload Frames"),QStringLiteral("PlaybackReload"),[this] { QString error; if(!reload(&error)) report(error); });
    auto* clear=add(file,tr("Unload Frames"),QStringLiteral("PlaybackUnload"),[this] { unload(); });
    auto* propertiesAction=add(file,tr("Loaded Files Properties..."),QStringLiteral("PlaybackProperties"),[this] { QMessageBox::information(this,tr("Loaded Files Properties"),properties()); });
    toolbar->addAction(load); toolbar->addAction(refresh); toolbar->addAction(clear); toolbar->addSeparator();
    toolbar->addAction(add(playMenu,tr("Play Forward"),QStringLiteral("PlaybackForward"),[this] { play(1); }));
    toolbar->addAction(add(playMenu,tr("Play Backward"),QStringLiteral("PlaybackBackward"),[this] { play(-1); }));
    toolbar->addAction(add(playMenu,tr("Stop"),QStringLiteral("PlaybackStop"),[this] { stop(); }));
    auto* loop=add(playMenu,tr("Loop"),QStringLiteral("PlaybackLoop"),[this](bool on) { setLoop(on); }); loop->setCheckable(true); loop->setChecked(true);
    add(playMenu,tr("First Frame"),QStringLiteral("PlaybackFirst"),[this] { setFrame(m_first->value()-1); });
    add(playMenu,tr("Previous Frame"),QStringLiteral("PlaybackPrevious"),[this] { setFrame(m_frame-1); });
    add(playMenu,tr("Next Frame"),QStringLiteral("PlaybackNext"),[this] { setFrame(m_frame+1); });
    add(playMenu,tr("Last Frame"),QStringLiteral("PlaybackLast"),[this] { setFrame(m_last->value()-1); });
    add(view,tr("Zoom In"),QStringLiteral("PlaybackZoomIn"),[this] { m_surface->zoom(1.25); });
    add(view,tr("Zoom Out"),QStringLiteral("PlaybackZoomOut"),[this] { m_surface->zoom(.8); });
    add(view,tr("Reset Zoom / Fit Image"),QStringLiteral("PlaybackFit"),[this] { m_surface->fit(); });
    add(view,tr("Actual Size"),QStringLiteral("PlaybackActualSize"),[this] { m_surface->actualSize(); });
    auto* single=add(view,tr("Single Image"),QStringLiteral("PlaybackSingleImage"),[this](bool on) { m_single=on; if(on) stop(); }); single->setCheckable(true);
    view->addAction(propertiesAction);
    layout->addWidget(toolbar);
    m_surface=new PlaybackSurface(this); layout->addWidget(m_surface,1);
    m_surface->setFocusPolicy(Qt::StrongFocus);
    add(playMenu,tr("Play / Stop"),QStringLiteral("PlaybackToggle"),[this] { if(playing()) stop(); else play(); });
    auto bind=[this](char const* name,Qt::Key key) { auto* action=findChild<QAction*>(QLatin1String(name)); action->setShortcut(QKeySequence(key)); action->setShortcutContext(Qt::WidgetShortcut); m_surface->addAction(action); };
    bind("PlaybackToggle",Qt::Key_Space); bind("PlaybackPrevious",Qt::Key_Left); bind("PlaybackNext",Qt::Key_Right); bind("PlaybackFirst",Qt::Key_Home); bind("PlaybackLast",Qt::Key_End);
    m_surface->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_surface,&QWidget::customContextMenuRequested,this,[this,file,playMenu,view](QPoint const& point) {
        QMenu menu(this); menu.addActions(file->actions()); menu.addSeparator(); menu.addActions(playMenu->actions()); menu.addSeparator(); menu.addActions(view->actions()); menu.exec(m_surface->mapToGlobal(point));
    });
    m_slider=new QSlider(Qt::Horizontal,this); m_slider->setObjectName(QStringLiteral("PlaybackFrameSlider")); m_slider->setRange(0,0); layout->addWidget(m_slider);
    auto* fields=new QWidget(this); auto* grid=new QGridLayout(fields); int field=0;
    auto addField=[grid,&field,fields](QString const& title,QWidget* widget) { int const row=field/3,column=(field%3)*2; grid->addWidget(new QLabel(title,fields),row,column); grid->addWidget(widget,row,column+1); ++field; };
    auto spin=[this,&addField](char const* name,QString const& title,int maximum) { auto* box=new QSpinBox(this); box->setObjectName(QLatin1String(name)); box->setRange(1,maximum); addField(title,box); return box; };
    m_frameBox=spin("PlaybackFrame",tr("Frame "),1); m_first=spin("PlaybackStart",tr("Start "),1); m_last=spin("PlaybackEnd",tr("Stop "),1);
    m_rate=new QDoubleSpinBox(this); m_rate->setObjectName(QStringLiteral("PlaybackRate")); m_rate->setRange(.001,1000); m_rate->setDecimals(6); m_rate->setValue(std::clamp(fps,.001,1000.0)); addField(tr("FPS "),m_rate);
    m_speed=spin("PlaybackPercent",tr("Speed "),800); m_speed->setMinimum(5); m_speed->setValue(100); m_speed->setSuffix(QStringLiteral("%"));
    auto* cache=spin("PlaybackCacheMB",tr("Cache "),2048); cache->setMinimum(1); cache->setValue(256); cache->setSuffix(tr(" MB"));
    layout->addWidget(fields);
    m_info=new QLabel(this); m_info->setObjectName(QStringLiteral("PlaybackInfo")); m_info->setWordWrap(true); m_info->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_info);
    connect(m_frameBox,&QSpinBox::valueChanged,this,[this](int value) { setFrame(value-1); });
    connect(m_slider,&QSlider::valueChanged,this,&PlaybackView::setFrame);
    connect(m_first,&QSpinBox::valueChanged,this,[this](int value) { setRange(value-1,std::max(value,m_last->value())-1); });
    connect(m_last,&QSpinBox::valueChanged,this,[this](int value) { setRange(std::min(value,m_first->value())-1,value-1); });
    auto reanchor=[this] { if(playing()) { m_from=m_frame; m_clock.restart(); } updateInfo(); };
    connect(m_rate,&QDoubleSpinBox::valueChanged,this,reanchor); connect(m_speed,&QSpinBox::valueChanged,this,reanchor);
    connect(cache,&QSpinBox::valueChanged,this,&PlaybackView::setCacheMegabytes);
    connect(&m_timer,&QTimer::timeout,this,&PlaybackView::tick); m_timer.setTimerType(Qt::PreciseTimer); m_timer.setInterval(5);
    setCacheMegabytes(256); updateInfo();
}

bool PlaybackView::loadFolder(QString const& folder,QString* error)
{
    auto fail=[&](QString const& why) { if(error) *error=why; return false; };
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
    QString why; QImage const first=ImageFiles::load(files.first(),&why);
    if(first.isNull()) return fail(tr("Cannot read %1: %2").arg(QFileInfo(files.first()).fileName(),why));
    stop(); m_cache.clear(); m_files=files; m_folder=dir.absolutePath(); m_frame=0;
    m_cache.insert(0,new QImage(first),std::max(1,int((first.sizeInBytes()+1023)/1024)));
    QSignalBlocker a(m_frameBox),b(m_first),c(m_last),d(m_slider);
    for(auto* box : {m_frameBox,m_first,m_last}) box->setRange(1,int(files.size()));
    m_first->setValue(1); m_last->setValue(int(files.size())); m_frameBox->setValue(1); m_slider->setRange(0,int(files.size())-1); m_slider->setValue(0);
    m_surface->setImage(first); m_surface->fit(); m_surface->setFocus(); updateInfo(); return true;
}

bool PlaybackView::reload(QString* error)
{
    if(m_folder.isEmpty()) { if(error) *error=tr("Load a frame folder first."); return false; }
    int const frame=m_frame,first=m_first->value()-1,last=m_last->value()-1;
    bool const toEnd=last==frameCount()-1; QString const current=m_files.value(frame);
    if(!loadFolder(m_folder,error)) return false;
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
    stop(); m_files.clear(); m_folder.clear(); m_cache.clear(); m_frame=0; m_surface->setImage({});
    QSignalBlocker a(m_frameBox),b(m_first),c(m_last),d(m_slider);
    for(auto* box : {m_frameBox,m_first,m_last}) { box->setRange(1,1); box->setValue(1); }
    m_slider->setRange(0,0); updateInfo();
}
QImage PlaybackView::read(int frame,QString* error)
{
    if(auto* image=m_cache.object(frame)) return *image;
    QImage const image=ImageFiles::load(m_files[frame],error);
    if(!image.isNull()) m_cache.insert(frame,new QImage(image),std::max(1,int((image.sizeInBytes()+1023)/1024)));
    return image;
}
QImage PlaybackView::currentImage() const { return m_surface->image(); }
void PlaybackView::setFrame(int frame)
{
    if(m_files.isEmpty()) return;
    frame=std::clamp(frame,0,frameCount()-1);
    QString error; QImage const image=read(frame,&error);
    if(image.isNull()) { stop(); updateInfo(); report(tr("Cannot read %1: %2").arg(QFileInfo(m_files[frame]).fileName(),error)); return; }
    m_frame=frame; m_surface->setImage(image);
    if(playing() && !m_advancing) {
        if(frame<m_first->value()-1 || frame>m_last->value()-1) stop();
        else { m_from=frame; m_clock.restart(); }
    }
    updateInfo();
}
void PlaybackView::setRange(int first,int last)
{
    if(m_files.isEmpty()) return;
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
    megabytes=std::clamp(megabytes,1,2048); m_cache.setMaxCost(megabytes*1024);
    if(auto* box=findChild<QSpinBox*>(QStringLiteral("PlaybackCacheMB"))) { QSignalBlocker block(box); box->setValue(megabytes); }
    if(m_info) updateInfo();
}
void PlaybackView::play(int direction)
{
    if(m_files.isEmpty() || m_single) return;
    stop(); m_direction=direction<0?-1:1;
    int const first=m_first->value()-1,last=m_last->value()-1;
    if(m_frame<first || m_frame>last || (m_direction<0?m_frame==first:m_frame==last)) {
        int const target=m_direction<0?last:first;
        setFrame(target); if(m_frame!=target) return;
    }
    m_from=m_frame; m_clock.restart(); m_timer.start(); updateInfo();
}
void PlaybackView::stop() { m_timer.stop(); if(m_info) updateInfo(); }
void PlaybackView::tick()
{
    auto const at=PlaybackTime::at(m_first->value()-1,m_last->value()-1,m_from,m_direction,m_rate->value()*m_speed->value()/100.0,m_clock.elapsed()/1000.0,0,m_loop);
    QScopedValueRollback<bool> advancing(m_advancing,true);
    if(at.frame!=m_frame) setFrame(at.frame);
    if(at.ended) stop();
}
void PlaybackView::hideEvent(QHideEvent* event) { stop(); QDialog::hideEvent(event); }
void PlaybackView::updateInfo()
{
    QSignalBlocker a(m_frameBox),b(m_slider); m_frameBox->setValue(m_frame+1); m_slider->setValue(m_frame);
    if(m_files.isEmpty()) { m_info->setText(tr("No frames loaded. Each picture file supplies one frame.")); return; }
    m_info->setText(tr("%1 / %2  |  %3 s / %4 s  |  %5  |  Cache %6 MB / %7 MB%8")
        .arg(m_frame+1).arg(frameCount()).arg(m_frame/m_rate->value(),0,'f',3).arg(frameCount()/m_rate->value(),0,'f',3)
        .arg(QFileInfo(m_files[m_frame]).fileName()).arg(m_cache.totalCost()/1024.0,0,'f',1).arg(m_cache.maxCost()/1024).arg(playing()?tr("  |  Playing"):QString()));
}
void PlaybackView::report(QString const& error) { m_info->setText(error); m_info->setToolTip(error); }
QString PlaybackView::properties() const
{
    if(m_files.isEmpty()) return tr("No frames loaded.");
    QImage const image=currentImage(); qint64 bytes=0; for(auto const& file : m_files) bytes+=QFileInfo(file).size();
    return tr("Folder: %1\nFrames: %2\nCurrent file: %3\nCurrent dimensions: %4 x %5\nCurrent pixel depth: %6 bits\nAlpha channel: %7\nColour profile: %8\nFrame rate: %9 fps\nSource files: %10 bytes\nCached pixels: %11 bytes\nCache budget: %12 bytes\nEach file supplies its first picture; dimensions may vary between files.")
        .arg(QDir::toNativeSeparators(m_folder)).arg(frameCount()).arg(QFileInfo(m_files[m_frame]).fileName()).arg(image.width()).arg(image.height()).arg(image.depth())
        .arg(image.hasAlphaChannel()?tr("Yes"):tr("No")).arg(image.colorSpace().isValid()?image.colorSpace().description():tr("Unspecified")).arg(m_rate->value(),0,'g',9).arg(bytes).arg(cacheBytes()).arg(qint64(m_cache.maxCost())*1024);
}
