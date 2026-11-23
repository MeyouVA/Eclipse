#include "board/model/BoardRecoveredAssets.h"
#include "board/model/BoardProjectFolder.h"
#include "board/model/BoardProjectJson.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <algorithm>

namespace {
using namespace BoardProjectFolder;

QString tr(char const* message)
{
    return QCoreApplication::translate("BoardRecoveredAssets", message);
}

bool isRecoveryName(QString const& name)
{
    static QRegularExpression const pattern(QStringLiteral(R"(\ARecovered (?:Assets|Artwork)-[A-Za-z0-9]{6}\z)"));
    return pattern.match(name).hasMatch();
}

QString digestOf(QString const& relative)
{
    auto match = drawingAssetPattern().match(relative);
    if (!match.hasMatch())
        match = pictureAssetPattern().match(relative);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool isAudio(QString const& relative)
{
    return relative.startsWith(QLatin1String("audio/")) && BoardProjectJson::validSoundAsset(relative.mid(6));
}

bool isLocalFile(QFileInfo const& info)
{
    return info.isFile() && !info.isSymLink() && !info.isJunction();
}

bool sameBytes(QString const& a, QString const& b)
{
    QFile first(a);
    QFile second(b);
    if (first.size() != second.size() || !first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;
    while (!first.atEnd()) {
        QByteArray const chunk = first.read(1 << 20);
        if (chunk.isEmpty() || second.read(chunk.size()) != chunk)
            return false;
    }
    return second.atEnd();
}

QStringList recoveredFiles(QDir const& folder)
{
    QStringList files;
    for (auto const* directory : {"elements/drawings", "elements/pictures", "audio"}) {
        QString const name = QLatin1String(directory);
        if (!isLocalDirectory(QFileInfo(folder.filePath(name))))
            continue;
        for (auto const& file : QDir(folder.filePath(name)).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks)) {
            QString const relative = name + QLatin1Char('/') + file.fileName();
            if (!digestOf(relative).isEmpty() || isAudio(relative))
                files.append(relative);
        }
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}
}

QVector<BoardRecoveredAssets::Folder> BoardRecoveredAssets::list(QString const& projectFile)
{
    QVector<Folder> folders;
    QDir const root = QFileInfo(projectFile).absoluteDir();
    for (auto const& entry : root.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (!isRecoveryName(entry.fileName()) || !isLocalDirectory(entry))
            continue;
        QStringList files = recoveredFiles(QDir(entry.absoluteFilePath()));
        if (!files.isEmpty())
            folders.append({entry.absoluteFilePath(), entry.lastModified(), std::move(files)});
    }
    std::stable_sort(folders.begin(), folders.end(), [](Folder const& a, Folder const& b) { return a.modified > b.modified; });
    return folders;
}

QStringList BoardRecoveredAssets::missingArtwork(QString const& projectFile)
{
    QFile file(projectFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray const bytes = file.read(maxProjectBytes + 1);
    auto const json = QJsonDocument::fromJson(bytes).object();
    QSet<QString> used;
    if (bytes.size() > maxProjectBytes || json.value("format").toString() != QLatin1String("eclipse-board")
        || json.value("version").toInt() != 1 || !referencedArtwork(json, used))
        return {};
    QDir const root = QFileInfo(projectFile).absoluteDir();
    QStringList missing;
    for (auto const& path : used)
        if (!isLocalFile(QFileInfo(root.filePath(path))))
            missing.append(path);
    missing.sort(Qt::CaseInsensitive);
    return missing;
}

bool BoardRecoveredAssets::restore(QString const& projectFile, QString const& folder, QStringList const& only,
                                   Result& result, QString* error)
{
    result = {};
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return false;
    };
    if (error)
        error->clear();
    QDir const root = QFileInfo(projectFile).absoluteDir();
    QFileInfo const source(folder);
    if (!isRecoveryName(source.fileName()) || !isLocalDirectory(source)
        || QFileInfo(source.absolutePath()).canonicalFilePath() != QFileInfo(root.absolutePath()).canonicalFilePath())
        return fail(tr("Choose a recovered assets folder inside the project folder."));
    QLockFile copyLock(root.filePath(QLatin1String(copyLockName)));
    copyLock.setStaleLockTime(0);
    QLockFile saveLock(root.filePath(QLatin1String(saveLockName)));
    saveLock.setStaleLockTime(0);
    if (!copyLock.tryLock() || !saveLock.tryLock())
        return fail(tr("The project folder is busy. Try again after the save or copy finishes."));

    QDir const recovered(source.absoluteFilePath());
    for (auto const& relative : recoveredFiles(recovered)) {
        if (!only.isEmpty() && !only.contains(relative))
            continue;
        QFileInfo const from(recovered.filePath(relative));
        QFileInfo const to(root.filePath(relative));
        QString const digest = digestOf(relative);
        if (!digest.isEmpty() && !matchesDigest(from, digest)) {
            result.conflicts.append(relative);
            continue;
        }
        if (to.exists() || to.isSymLink()) {
            bool const identical = isLocalFile(to) && (digest.isEmpty() ? sameBytes(from.absoluteFilePath(), to.absoluteFilePath())
                                                                      : matchesDigest(to, digest));
            if (identical && QFile::remove(from.absoluteFilePath()))
                ++result.duplicates;
            else
                result.conflicts.append(relative);
            continue;
        }
        QFileInfo const parent(to.absolutePath());
        if ((parent.exists() || parent.isSymLink()) ? !isLocalDirectory(parent) : !root.mkpath(parent.absoluteFilePath()))
            return fail(tr("Could not prepare the project folder for “%1”.").arg(relative));
        if (!QFile::rename(from.absoluteFilePath(), to.absoluteFilePath()))
            return fail(tr("Could not move “%1” back into the project folder.").arg(relative));
        ++result.restored;
    }
    for (auto const* directory : {"elements/drawings", "elements/pictures", "elements", "audio"})
        recovered.rmdir(QLatin1String(directory));
    result.removedFolder = root.rmdir(source.fileName());
    return true;
}
