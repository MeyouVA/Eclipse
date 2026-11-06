#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace ThirdParty {

// A package's folder beside the program (thirdparty/<package>, or ../thirdparty/<package> in a build), or empty.
inline QString folder(QString const& package)
{
    QDir const app(QCoreApplication::applicationDirPath());
    for (QString const& root : {app.filePath(QStringLiteral("thirdparty")), app.filePath(QStringLiteral("../thirdparty"))}) {
        QFileInfo const dir(QDir(root).filePath(package));
        if (dir.isDir())
            return dir.canonicalFilePath();
    }
    return {};
}

inline QString executable(QString const& package, QString const& name)
{
    QDir const app(QCoreApplication::applicationDirPath());
    QStringList const roots{app.filePath(QStringLiteral("thirdparty")), app.filePath(QStringLiteral("../thirdparty"))};
    for (QString const& root : roots) {
        QDir const folder(QDir(root).filePath(package));
        for (QString const& relative : {QStringLiteral("bin/") + name + QStringLiteral(".exe"), name + QStringLiteral(".exe")}) {
            QFileInfo const file(folder.filePath(relative));
            if (file.isFile() && file.isExecutable())
                return file.canonicalFilePath();
        }
    }
    return {};
}

}
