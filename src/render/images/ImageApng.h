#pragma once
#include "render/images/ImageFiles.h"

namespace ImageCodec {
bool isApng(QString const& path);
QVector<ImageFiles::Frame> readApng(QString const& path, QString* error);
bool writeApng(QVector<ImageFiles::Frame> const& frames, QString const& path, QString* error, int loops);
}
