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
    auto* movie=add(file,tr("Open Movie..."),QStringLiteral("PlaybackMovie"),[this] { chooseMovie(); });
    auto* refresh=add(file,tr("Reload Frames"),QStringLiteral("PlaybackReload"),[this] { QString error; if(!reload(&error)) report(error); });
    auto* clear=add(file,tr("Unload Frames"),QStringLiteral("PlaybackUnload"),[this] { unload(); });
    auto* propertiesAction=add(file,tr("Loaded Files Properties..."),QStringLiteral("PlaybackProperties"),[this] { QMessageBox::information(this,tr("Loaded Files Properties"),properties()); });
    toolbar->addAction(movie); toolbar->addAction(load); toolbar->addAction(refresh); toolbar->addAction(clear); toolbar->addSeparator();
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
    auto* caching=add(view,tr("Cache Frames"),QStringLiteral("PlaybackCacheEnabled"),[this](bool on) { setCacheEnabled(on); }); caching->setCheckable(true); caching->setChecked(true);
    auto* resolution=view->addMenu(tr("Playback Resolution")); auto* sizes=new QActionGroup(this);
    for(int percent : {100,50,25}) {
        auto* action=add(resolution,tr("%1%").arg(percent),QStringLiteral("PlaybackResolution%1").arg(percent),[this,percent] { setResolution(percent); });
        action->setCheckable(true); action->setChecked(percent==100); sizes->addAction(action);
    }
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
    m_volume=spin("PlaybackVolume",tr("Volume "),100); m_volume->setMinimum(0); m_volume->setValue(100); m_volume->setSuffix(QStringLiteral("%"));
    m_volume->setToolTip(tr("Movie sound plays forward at its native frame rate and 100% speed."));
    connect(m_volume,&QSpinBox::valueChanged,this,[this] { if(playing()) { m_from=m_frame; m_clock.restart(); m_audioCycle=0; startSound(); } });
    layout->addWidget(fields);
    m_info=new QLabel(this); m_info->setObjectName(QStringLiteral("PlaybackInfo")); m_info->setWordWrap(true); m_info->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_info);
    connect(m_frameBox,&QSpinBox::valueChanged,this,[this](int value) { setFrame(value-1); });
    connect(m_slider,&QSlider::valueChanged,this,&PlaybackView::setFrame);
    connect(m_first,&QSpinBox::valueChanged,this,[this](int value) { setRange(value-1,std::max(value,m_last->value())-1); });
    connect(m_last,&QSpinBox::valueChanged,this,[this](int value) { setRange(std::min(value,m_first->value())-1,value-1); });
    auto reanchor=[this] { if(playing()) { m_from=m_frame; m_clock.restart(); m_audioCycle=0; startSound(); } updateInfo(); };
    connect(m_rate,&QDoubleSpinBox::valueChanged,this,reanchor); connect(m_speed,&QSpinBox::valueChanged,this,reanchor);
    connect(cache,&QSpinBox::valueChanged,this,&PlaybackView::setCacheMegabytes);
    connect(&m_timer,&QTimer::timeout,this,&PlaybackView::tick); m_timer.setTimerType(Qt::PreciseTimer); m_timer.setInterval(5);
    setCacheMegabytes(256); updateInfo();
}

