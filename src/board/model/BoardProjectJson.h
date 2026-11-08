#pragma once

#include <QJsonObject>

struct BoardProject;
struct Drawing;

namespace BoardProjectJson {
bool encode(BoardProject const& project, QJsonObject& json, QString* error);
bool decode(QJsonObject const& json, BoardProject& project, QString* error);
bool supportedDrawing(Drawing const& drawing);
}
