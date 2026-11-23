#include "board/model/BoardPackCache.h"
#include "board/model/BoardArchive.h"
#include "board/model/BoardProject.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString const markerName = QStringLiteral("pack.json");

QString tr(char const* message)
{
    return QCoreApplication::translate("BoardPackCache", message);
}

bool fail(QString* error, QString const& message)
{
    if (error)
        *error = message;
    return false;
}

QString absolutePack(QString const& pack)
{
    QFileInfo const info(pack);
    QString const canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString folderFor(QString const& pack)
{
    QByteArray const key = absolutePack(pack).toCaseFolded().toUtf8();
    return QDir(BoardPackCache::root()).filePath(QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(24)));
}

BoardPackCache::Entry readMarker(QString const& folder)
{
    QFile file(QDir(folder).filePath(markerName));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return {};
    QJsonObject const json = QJsonDocument::fromJson(file.readAll()).object();
    QString const project = json["project"].toString();
    QStringList const parts = project.split(QLatin1Char('/'));
    if (json["format"].toString() != QLatin1String("eclipse-board-pack-cache") || json["version"].toInt() != 1
        || parts.size() != 2 || parts.contains(QStringLiteral("..")) || parts.contains(QStringLiteral(".")) || parts.contains(QString())
        || !BoardProject::validName(parts[0]) || parts[1] != parts[0] + QStringLiteral(".ecb") || !json["committed"].isBool())
        return {};
    BoardPackCache::Entry entry;
    entry.pack = json["pack"].toString();
    entry.folder = QDir(folder).absolutePath();
    entry.projectFile = QDir(folder).absoluteFilePath(project);
    entry.committed = json["committed"].toBool();
    entry.hasRecovery = QFileInfo::exists(entry.projectFile + QStringLiteral(".recovery"));
    if (entry.pack.isEmpty() || !QFileInfo(entry.projectFile).isFile())
        return {};
    return entry;
}

bool writeMarker(QString const& folder, QString const& pack, QString const& projectFile, bool committed)
{
    QString const project = QDir(folder).relativeFilePath(projectFile);
    QJsonObject const json{{"format", "eclipse-board-pack-cache"}, {"version", 1}, {"pack", absolutePack(pack)},
                           {"project", project}, {"committed", committed}};
    QSaveFile file(QDir(folder).filePath(markerName));
    QByteArray const bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool insideRoot(QString const& path)
{
    QString const base = QDir::cleanPath(QDir(BoardPackCache::root()).absolutePath()) + QLatin1Char('/');
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).startsWith(base, Qt::CaseInsensitive);
}
}

QString BoardPackCache::root()
{
    QString const set = qEnvironmentVariable("ECLIPSE_TOOLS_FOLDER");
    QString const data = !set.isEmpty() ? set : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Eclipse");
    QString const folder = QDir(data).filePath(QStringLiteral("Board Pack Cache"));
    QDir().mkpath(folder);
    return folder;
}

bool BoardPackCache::isPack(QString const& path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("ecbz"), Qt::CaseInsensitive) == 0;
}

BoardPackCache::Entry BoardPackCache::find(QString const& pack)
{
    Entry entry = readMarker(folderFor(pack));
    if (entry.pack.compare(absolutePack(pack), Qt::CaseInsensitive) != 0)
        return {};
    return entry;
}

BoardPackCache::Entry BoardPackCache::forProject(QString const& projectFile)
{
    if (projectFile.isEmpty() || !insideRoot(projectFile))
        return {};
    QDir folder = QFileInfo(projectFile).absoluteDir();
    if (!folder.cdUp())
        return {};
    Entry entry = readMarker(folder.absolutePath());
    if (entry.projectFile.compare(QFileInfo(projectFile).absoluteFilePath(), Qt::CaseInsensitive) != 0)
        return {};
    return entry;
}

QVector<BoardPackCache::Entry> BoardPackCache::pending()
{
    QVector<Entry> found;
    QDir const folder(root());
    for (QString const& name : folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        Entry entry = readMarker(folder.filePath(name));
        if (!entry.pack.isEmpty() && entry.pending())
            found.push_back(entry);
    }
    return found;
}

QString BoardPackCache::unpack(QString const& pack, QString* error)
{
    if (error)
        error->clear();
    auto reject = [error](QString const& message) {
        fail(error, message);
        return QString();
    };
    if (!isPack(pack) || !QFileInfo(pack).isFile())
        return reject(tr("Choose an existing .ecbz packed project."));
    QString const folder = folderFor(pack);
    if (QFileInfo::exists(folder))
        return reject(tr("This packed project is already unpacked in the cache."));
    if (!QDir().mkpath(folder))
        return reject(tr("Could not create the project cache folder."));
    QString const projectFile = BoardArchive::extract(pack, folder, error);
    if (projectFile.isEmpty() || !writeMarker(folder, pack, projectFile, true)) {
        if (!projectFile.isEmpty())
            fail(error, tr("Could not record the packed project in the cache."));
        QDir(folder).removeRecursively();
        return {};
    }
    return projectFile;
}

bool BoardPackCache::setCommitted(QString const& projectFile, bool committed)
{
    Entry const entry = forProject(projectFile);
    if (entry.pack.isEmpty())
        return false;
    return entry.committed == committed || writeMarker(entry.folder, entry.pack, entry.projectFile, committed);
}

bool BoardPackCache::commit(BoardProject const& project, QString const& projectFile, QString* error)
{
    Entry const entry = forProject(projectFile);
    if (entry.pack.isEmpty())
        return fail(error, tr("This project was not opened from a packed file."));
    if (!BoardArchive::pack(project, entry.projectFile, entry.pack, error))
        return false;
    if (!writeMarker(entry.folder, entry.pack, entry.projectFile, true))
        return fail(error, tr("The packed file was updated, but the cache could not record it."));
    return true;
}

bool BoardPackCache::discard(Entry const& entry, QString* error)
{
    if (entry.folder.isEmpty() || !insideRoot(entry.folder + QStringLiteral("/x")) || readMarker(entry.folder).pack.isEmpty())
        return fail(error, tr("This is not a packed project cache."));
    {
        QLockFile lock(entry.projectFile + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            return fail(error, tr("The cached project is open in another editor."));
    }
    if (!QDir(entry.folder).removeRecursively())
        return fail(error, tr("Could not remove the cached project."));
    return true;
}
