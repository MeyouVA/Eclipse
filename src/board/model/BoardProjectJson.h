#pragma once

#include <QJsonObject>

struct BoardProject;
struct BoardSound;
struct Drawing;

namespace BoardProjectJson {
inline constexpr qsizetype maxEmbeddedImageBytes = 128 * 1024 * 1024;
inline constexpr qsizetype maxEmbeddedImagesBytes = 512 * 1024 * 1024;
bool encode(BoardProject const& project, QJsonObject& json, QString* error);
bool decode(QJsonObject const& json, BoardProject& project, QString* error);
bool supportedDrawing(Drawing const& drawing);
bool validSound(BoardSound const& sound);
}
