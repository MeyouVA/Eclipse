#pragma once

#include "EclipseExport.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace BoardRecoveredAssets {
struct Folder {
    QString path;
    QDateTime modified;
    QStringList files;
};

struct Result {
    int restored = 0;
    int duplicates = 0;
    QStringList conflicts;
    bool removedFolder = false;
};

// Recovery folders made by asset cleanup beside the project file, newest first.
ECLIPSE_BOARD_MODEL_API QVector<Folder> list(QString const& projectFile);
// Drawings and pictures the saved project refers to that are not in the project folder.
ECLIPSE_BOARD_MODEL_API QStringList missingArtwork(QString const& projectFile);
// Moves recovered files back; an empty `only` restores everything in the folder.
ECLIPSE_BOARD_MODEL_API bool restore(QString const& projectFile, QString const& folder, QStringList const& only,
                                     Result& result, QString* error = nullptr);
}
