#include "board/ui/export/BoardExportFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

namespace BoardExport {
bool fail(QString* error, QString const& message)
{
    if (error)
        *error = message;
    return false;
}

bool publishFolder(QString const& folder, std::function<bool(QDir const&)> const& write, QString* error)
{
    QFileInfo const destination(folder);
    QDir parent = destination.absoluteDir();
    if (destination.exists() || destination.isSymLink())
        return fail(error, QCoreApplication::translate("BoardExport", "The output folder already exists: %1").arg(folder));
    QTemporaryDir staging(parent.filePath(QStringLiteral(".eclipse-export-XXXXXX")));
    if (!staging.isValid())
        return fail(error, QCoreApplication::translate("BoardExport", "Could not create a temporary output folder."));
    QDir const temporary(staging.path());
    QString const name = destination.fileName();
    if (!temporary.mkdir(name))
        return fail(error, QCoreApplication::translate("BoardExport", "Could not create the output folder."));
    QDir const prepared(temporary.filePath(name));
    if (!write(prepared))
        return false;
    if (!parent.rename(prepared.absolutePath(), destination.absoluteFilePath()))
        return fail(error, QCoreApplication::translate("BoardExport", "Could not publish the output folder: %1").arg(folder));
    return true;
}
}
