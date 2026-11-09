#pragma once

#include "EclipseExport.h"

#include <QString>

struct BoardProject;

namespace BoardArchive {
ECLIPSE_BOARD_MODEL_API bool pack(BoardProject const& project, QString const& projectFile, QString const& archive, QString* error = nullptr);
ECLIPSE_BOARD_MODEL_API QString restore(QString const& archive, QString const& directory, QString* error = nullptr);
}
