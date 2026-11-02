#pragma once
#include "EclipseExport.h"
#include <QCache>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QStringList>
#include <QTimer>

class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSpinBox;
class PlaybackSurface;

class ECLIPSE_UI_API PlaybackView : public QDialog {
    Q_OBJECT
public:
    explicit PlaybackView(double fps,QWidget* parent=nullptr);
    bool loadFolder(QString const& folder,QString* error=nullptr);
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
    void play(int direction=1);
    void stop();
    QString properties() const;
protected:
    void hideEvent(QHideEvent* event) override;
private:
    void chooseFolder();
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
    QDoubleSpinBox* m_rate=nullptr;
    QSlider* m_slider=nullptr;
    QLabel* m_info=nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_frame=0,m_from=0,m_direction=1;
    bool m_loop=true,m_advancing=false,m_single=false;
};
