#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectFolder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QScopeGuard>
#include <QTemporaryDir>

namespace {
bool sameContents(QString const& source, QString const& destination)
{
    QFile a(source), b(destination);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly) || a.size() != b.size())
        return false;
    while (!a.atEnd()) {
        auto const bytes = a.read(256 * 1024);
        if (bytes.isEmpty() || bytes != b.read(bytes.size()))
            return false;
    }
    return b.atEnd() && a.error() == QFileDevice::NoError && b.error() == QFileDevice::NoError;
}
}

bool BoardDocument::saveAs(QString const& path, QString* error)
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_path.isEmpty())
        return fail(tr("Open or create a project before saving a copy."));

    QFileInfo const destination(path);
    QDir const folder = destination.absoluteDir();
    if (destination.suffix().compare(QLatin1String("ecb"), Qt::CaseInsensitive) != 0 || !folder.exists())
        return fail(tr("Choose a new .ecb file in an existing folder."));

    QLockFile lock(folder.filePath(QStringLiteral(".eclipse-board-copy.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
        return fail(tr("Could not lock the destination folder. Check that it is writable and retry."));
    if (destination.exists() || destination.isSymLink())
        return fail(tr("A file already exists at that destination. Choose a new filename."));

    QDir const sourceFolder = QFileInfo(m_path).absoluteDir();
    bool const copyAudio = sourceFolder.canonicalPath() != folder.canonicalPath();
    QStringList createdFiles;
    bool createdAudioFolder = false;
    auto rollback = qScopeGuard([&] {
        for (auto const& file : createdFiles)
            QFile::remove(file);
        if (createdAudioFolder)
            folder.rmdir(QStringLiteral("audio"));
    });
    for (auto const& asset : soundAssets()) {
        QString const relative = QStringLiteral("audio/") + asset;
        QString const source = sourceFolder.filePath(relative);
        if (!QFileInfo(source).isFile())
            return fail(tr("The project sound file “%1” is missing.").arg(asset));
        if (!copyAudio)
            continue;
        QFileInfo const audioFolder(folder.filePath(QStringLiteral("audio")));
        if (audioFolder.isSymLink() || audioFolder.isJunction())
            return fail(tr("The destination audio folder must be a local folder."));
        if (!audioFolder.isDir()) {
            if (!folder.mkdir(QStringLiteral("audio")))
                return fail(tr("Could not create the destination audio folder."));
            createdAudioFolder = true;
        }
        QString const output = folder.filePath(relative);
        QFileInfo const existing(output);
        if (existing.isSymLink() || (existing.exists() && !sameContents(source, output)))
            return fail(tr("The destination contains a different sound file named “%1”.").arg(asset));
        if (existing.exists())
            continue;
        if (!QFile::copy(source, output))
            return fail(tr("Could not copy the project sound file “%1”.").arg(asset));
        createdFiles.append(output);
    }

    QTemporaryDir const staged(folder.filePath(QStringLiteral(".board-copy-XXXXXX")));
    if (!staged.isValid())
        return fail(tr("Could not prepare the project copy: %1").arg(staged.errorString()));
    QString const projectFile = staged.filePath(QStringLiteral("copy.ecb"));
    auto const bytes = m_project.toBytes(error);
    if (bytes.isEmpty() || !BoardProjectFolder::save(projectFile, bytes, error, folder.absolutePath()))
        return false;
    QFile copy(projectFile);
    if (!copy.rename(destination.absoluteFilePath()))
        return fail(tr("Could not publish the project copy: %1").arg(copy.errorString()));

    rollback.dismiss();
    m_path = destination.absoluteFilePath();
    m_undo.setClean();
    return true;
}
