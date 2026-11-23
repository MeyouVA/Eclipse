#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QVector>

struct BoardProject;

namespace BoardPackCache {
struct Entry {
    QString pack;
    QString folder;
    QString projectFile;
    bool committed = true;
    bool hasRecovery = false;
    bool pending() const { return !committed || hasRecovery; }
};

ECLIPSE_BOARD_MODEL_API QString root();
ECLIPSE_BOARD_MODEL_API bool isPack(QString const& path);
ECLIPSE_BOARD_MODEL_API Entry find(QString const& pack);
ECLIPSE_BOARD_MODEL_API Entry forProject(QString const& projectFile);
ECLIPSE_BOARD_MODEL_API QVector<Entry> pending();
ECLIPSE_BOARD_MODEL_API QString unpack(QString const& pack, QString* error = nullptr);
ECLIPSE_BOARD_MODEL_API bool setCommitted(QString const& projectFile, bool committed);
ECLIPSE_BOARD_MODEL_API bool commit(BoardProject const& project, QString const& projectFile, QString* error = nullptr);
ECLIPSE_BOARD_MODEL_API bool discard(Entry const& entry, QString* error = nullptr);
}
