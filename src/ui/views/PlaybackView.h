#pragma once
#include "EclipseExport.h"
#include <QCache>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QStringList>
#include <QTimer>
#include "audio/sound/SoundPlayer.h"
#include <memory>

class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;
class PlaybackSurface;
struct PlaybackMovie;

class ECLIPSE_UI_API PlaybackView : public QDialog {
    Q_OBJECT
public:
    explicit PlaybackView(double fps,QWidget* parent=nullptr);
    ~PlaybackView() override;
    bool loadFolder(QString const& folder,QString* error=nullptr);
    bool loadMovie(QString const& path,QString* error=nullptr);
    bool loadPreview(QVector<QImage> const& frames,QVector<qint16> const& audio,double fps,QString* error=nullptr);
    bool previewLoaded() const { return !m_preview.isEmpty(); }
    void applyPreferences();
    bool reload(QString* error=nullptr);
    void unload();
    int frameCount() const { return int(m_files.size()); }
    int currentFrame() const { return m_frame; }
    bool playing() const { return m_timer.isActive(); }
    QImage currentImage() const;
    QStringList files() const { return m_files; }
    qint64 cacheBytes() const { return qint64(m_cache.totalCost())*1024; }
    void setFrame(int frame);
    void setRange(int first,int last);
    void setRate(double fps);
    void setLoop(bool on);
    void setCacheMegabytes(int megabytes);
    void setCacheEnabled(bool enabled);
    void setResolution(int percent);
    void setVolume(int percent);
    void setPreroll(int frames);
    bool prerollActive() const { return m_padding; }
    void setImageChannel(int channel);
    QImage displayImage() const;
    bool hasAudio() const;
    QString movieSource() const;
    QVector<qint16> playbackSound() const;
    void play(int direction=1);
    void stop();
    QString properties() const;
protected:
    void hideEvent(QHideEvent* event) override;
private:
    void chooseFolder();
    void chooseMovie();
    void startSound();
    void restartAtCurrent();
    void tick();
    void updateInfo();
    void report(QString const& error);
    QImage read(int frame,QString* error);
    QStringList m_files;
    QString m_folder;
    QCache<int,QImage> m_cache;
    PlaybackSurface* m_surface=nullptr;
    QSpinBox* m_frameBox=nullptr;
    QSpinBox* m_first=nullptr;
    QSpinBox* m_last=nullptr;
    QSpinBox* m_speed=nullptr;
    QSpinBox* m_volume=nullptr;
    QDoubleSpinBox* m_rate=nullptr;
    QSlider* m_slider=nullptr;
    QLabel* m_info=nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_frame=0,m_from=0,m_direction=1;
    bool m_loop=true,m_advancing=false,m_single=false;
    bool m_loading=false;
    bool m_cacheEnabled=true;
    int m_cacheMegabytes=256,m_resolution=100;
    std::unique_ptr<PlaybackMovie> m_movie;
    QVector<QImage> m_preview;
    QVector<qint16> m_previewSound;
    double m_previewRate=24;
    SoundPlayer m_player;
    qint64 m_audioCycle=0;
    int m_preroll=0,m_loopPause=0;
    bool m_audioHolding=false;
    int m_initialPreroll=0;
    bool m_padding=false;
};
