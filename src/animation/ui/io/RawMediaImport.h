#pragma once
#include "model/media/RawPixels.h"
#include "model/media/RawVideo.h"
#include <QTemporaryDir>
#include <memory>
class QWidget;
namespace RawMediaImport {
struct Images {
    QStringList files;
    std::unique_ptr<QTemporaryDir> folder;
    bool ready=true;
    void keep() { if(folder) folder->setAutoRemove(false); }
};
Images images(QWidget* parent,QStringList const& files,QString const& scenePath,bool linked,QString* error,bool forceRaw=false);
bool video(QWidget* parent,QString const& path,double fps,RawVideo::Options& options,QString* error);
}
