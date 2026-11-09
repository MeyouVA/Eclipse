#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectFolder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>

namespace {
using namespace BoardProjectFolder;

bool recordDrawings(QJsonValue const& value, QSet<QString>& used)
{
    if (!value.isArray())
        return false;
    for (auto const& layer : value.toArray()) {
        QString const path = layer.toObject().value("drawingFile").toString();
        if (!drawingAssetPattern().match(path).hasMatch())
            return false;
        used.insert(path);
    }
    return true;
}

bool recordArtwork(QJsonObject const& manifest, QSet<QString>& used)
{
    if (manifest.value("storage").toString() != QLatin1String("folder")
        || (manifest.contains("library") && !recordDrawings(manifest.value("library"), used))
        || (manifest.contains("images") && !manifest.value("images").isObject()))
        return false;
    auto const images = manifest.value("images").toObject();
    for (auto const& value : images) {
        QString const path = value.toString();
        if (!pictureAssetPattern().match(path).hasMatch())
            return false;
        used.insert(path);
    }
    auto const shots = manifest.value("content").toObject().value("shots").toArray();
    if (shots.isEmpty())
        return false;
    for (auto const& shot : shots) {
        auto const panels = shot.toObject().value("panels").toArray();
        if (panels.isEmpty())
            return false;
        for (auto const& panel : panels)
            if (!recordDrawings(panel.toObject().value("layers"), used))
                return false;
    }
    return true;
}

bool isLocalDirectory(QFileInfo const& folder)
{
    return folder.isDir() && !folder.isSymLink() && !folder.isJunction();
}

bool matchesContent(QFileInfo const& info, QString const& digest)
{
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > maxProjectBytes)
        return false;
    QFile file(info.absoluteFilePath());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file)
        && file.error() == QFileDevice::NoError && QString::fromLatin1(hash.result().toHex()) == digest;
}
}

qsizetype BoardDocument::moveUnusedArtwork(QString& recoveryFolder, QString* error) const
{
    using namespace BoardProjectFolder;
    recoveryFolder.clear();
    if (error)
        error->clear();
    auto fail = [error](QString const& message) -> qsizetype {
        if (error)
            *error = message;
        return -1;
    };
    if (m_path.isEmpty() || m_legacy)
        return fail(tr("Open a folder-based board first."));
    QFileInfo const projectFile(m_path);
    QDir const root = projectFile.absoluteDir();
    QLockFile copyLock(root.filePath(QLatin1String(copyLockName)));
    copyLock.setStaleLockTime(0);
    QLockFile saveLock(root.filePath(QLatin1String(saveLockName)));
    saveLock.setStaleLockTime(0);
    if (!copyLock.tryLock() || !saveLock.tryLock())
        return fail(tr("The project folder is busy. Try again after the save or copy finishes."));

    QSet<QString> used;
    bool foundProject = false;
    auto const entries = root.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (auto const& entry : entries) {
        if (entry.suffix().compare(QLatin1String("ecb"), Qt::CaseInsensitive) != 0)
            continue;
        QFile file(entry.absoluteFilePath());
        if (!entry.isFile() || entry.isSymLink() || !file.open(QIODevice::ReadOnly))
            return fail(tr("Could not inspect the saved project “%1”.").arg(entry.fileName()));
        auto const bytes = file.read(maxProjectBytes + 1);
        auto const json = QJsonDocument::fromJson(bytes).object();
        bool const external = json.contains("storage");
        if (file.error() != QFileDevice::NoError || bytes.size() > maxProjectBytes
            || json.value("format").toString() != QLatin1String("eclipse-board") || json.value("version").toInt() != 1
            || (external && !recordArtwork(json, used)))
            return fail(tr("Could not verify the artwork references in “%1”.").arg(entry.fileName()));
        if (external && entry.fileName().compare(projectFile.fileName(), Qt::CaseInsensitive) == 0)
            foundProject = true;
    }
    if (!foundProject || !isLocalDirectory(QFileInfo(root.filePath(QStringLiteral("elements")))))
        return fail(tr("The project folder could not be verified."));

    QStringList candidates;
    auto collect = [&](QString const& directory, QRegularExpression const& pattern) {
        QDir const folder(root.filePath(directory));
        if (!isLocalDirectory(QFileInfo(folder.absolutePath())))
            return false;
        for (auto const& file : folder.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks)) {
            QString const relative = directory + QLatin1Char('/') + file.fileName();
            if (used.contains(relative))
                continue;
            auto const match = pattern.match(relative);
            if (match.hasMatch() && matchesContent(file, match.captured(1)))
                candidates.append(relative);
        }
        return true;
    };
    if (!collect(QStringLiteral("elements/drawings"), drawingAssetPattern())
        || !collect(QStringLiteral("elements/pictures"), pictureAssetPattern()))
        return fail(tr("The project artwork folders could not be verified."));
    if (candidates.isEmpty())
        return 0;

    QTemporaryDir recovery(root.filePath(QStringLiteral("Recovered Artwork-XXXXXX")));
    if (!recovery.isValid())
        return fail(tr("Could not create the recovery folder: %1").arg(recovery.errorString()));
    QDir const target(recovery.path());
    for (auto const& path : candidates) {
        QString const destination = target.filePath(path);
        if (!target.mkpath(QFileInfo(destination).absolutePath()) || !QFile::rename(root.filePath(path), destination))
            return fail(tr("Could not move “%1”. Any files already moved are in the recovery folder.").arg(path));
        recovery.setAutoRemove(false);
        recoveryFolder = target.absolutePath();
    }
    return candidates.size();
}
