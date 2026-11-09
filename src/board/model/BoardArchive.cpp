#include "board/model/BoardArchive.h"
#include "board/model/BoardProject.h"
#include "formats/archive/Zip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>

namespace {
constexpr qsizetype maxContents = 256 * 1024 * 1024;
constexpr qsizetype maxArchive = maxContents + 64 * 1024;
QString const projectEntry = QStringLiteral("project.ecb");

QString tr(char const* message)
{
    return QCoreApplication::translate("BoardArchive", message);
}

bool fail(QString* error, QString const& message)
{
    if (error)
        *error = message;
    return false;
}

bool writeFile(QString const& path, QByteArray const& bytes, QString* error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
        return true;
    return fail(error, tr("Could not write “%1”: %2").arg(QFileInfo(path).fileName(), file.errorString()));
}
}

bool BoardArchive::pack(BoardProject const& project, QString const& projectFile, QString const& archive, QString* error)
{
    if (error)
        error->clear();
    QFileInfo const destination(archive);
    if (projectFile.isEmpty() || destination.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) != 0)
        return fail(error, tr("Choose a .zip destination for the open project."));
    if (destination.isSymLink())
        return fail(error, tr("Choose a regular file for the archive."));
    QString const audioPath = project.audioPath(projectFile);
    QString const existing = destination.canonicalFilePath();
    if (!existing.isEmpty() && (existing.compare(QFileInfo(projectFile).canonicalFilePath(), Qt::CaseInsensitive) == 0
        || (!audioPath.isEmpty() && existing.compare(QFileInfo(audioPath).canonicalFilePath(), Qt::CaseInsensitive) == 0)))
        return fail(error, tr("Choose a destination outside the files used by this project."));
    QByteArray projectBytes = project.toBytes(error);
    if (projectBytes.isEmpty())
        return false;
    QList<QPair<QString, QByteArray>> entries;
    qsizetype const remaining = maxContents - projectBytes.size();
    entries.append({projectEntry, std::move(projectBytes)});
    if (!project.sound.asset.isEmpty()) {
        QFile sound(audioPath);
        if (!sound.open(QIODevice::ReadOnly))
            return fail(error, tr("Could not read the project sound: %1").arg(sound.errorString()));
        if (sound.size() > remaining)
            return fail(error, tr("The packed project exceeds the 256 MiB contents limit."));
        QByteArray bytes = sound.read(remaining + 1);
        if (sound.error() != QFileDevice::NoError || bytes.isEmpty())
            return fail(error, tr("The project sound is empty or could not be read."));
        if (bytes.size() > remaining)
            return fail(error, tr("The packed project exceeds the 256 MiB contents limit."));
        entries.append({QStringLiteral("audio/") + project.sound.asset, std::move(bytes)});
    }
    return writeFile(archive, Zip::write(entries), error);
}

QString BoardArchive::restore(QString const& archive, QString const& directory, QString* error)
{
    if (error)
        error->clear();
    auto reject = [error](QString const& message) {
        fail(error, message);
        return QString();
    };
    QDir parent(directory);
    if (directory.isEmpty() || !parent.exists())
        return reject(tr("Choose an existing folder in which to restore the project."));
    QFile file(archive);
    if (!file.open(QIODevice::ReadOnly))
        return reject(tr("Could not open the archive: %1").arg(file.errorString()));
    if (file.size() > maxArchive)
        return reject(tr("The packed project exceeds the size limit."));
    QByteArray const packed = file.read(maxArchive + 1);
    if (file.error() != QFileDevice::NoError || packed.size() > maxArchive)
        return reject(tr("The packed project could not be read within the size limit."));
    auto const entries = Zip::readChecked(packed, 2, maxContents);
    auto const projectBytes = entries.constFind(projectEntry);
    if (projectBytes == entries.cend())
        return reject(tr("The archive is damaged or missing its project file."));
    BoardProject project;
    if (!BoardProject::fromBytes(*projectBytes, project, error))
        return {};
    QString const audioEntry = QStringLiteral("audio/") + project.sound.asset;
    auto const audioBytes = entries.constFind(audioEntry);
    bool const hasAudio = !project.sound.asset.isEmpty();
    if (entries.size() != (hasAudio ? 2 : 1) || (hasAudio && (audioBytes == entries.cend() || audioBytes->isEmpty())))
        return reject(tr("The archive does not contain exactly the project and its required audio."));

    QTemporaryDir staged(parent.filePath(QStringLiteral(".board-restore-XXXXXX")));
    if (!staged.isValid())
        return reject(tr("Could not prepare the restore folder: %1").arg(staged.errorString()));
    QDir const working(staged.path());
    QString const filename = project.name + QStringLiteral(".ecb");
    if (!project.save(working.filePath(filename), error))
        return {};
    if (hasAudio) {
        if (!writeFile(working.filePath(audioEntry), *audioBytes, error))
            return {};
    }

    QString const base = tr("%1 Restored").arg(project.name);
    QLockFile lock(parent.filePath(QStringLiteral(".eclipse-board-restore.lock")));
    if (!lock.tryLock())
        return reject(tr("Could not lock the destination folder. Check that it is writable and retry."));
    QString name = base;
    for (int number = 2; QFileInfo::exists(parent.filePath(name)) || QFileInfo(parent.filePath(name)).isSymLink(); ++number)
        name = tr("%1 (%2)").arg(base).arg(number);
    if (!parent.rename(working.dirName(), name))
        return reject(tr("Could not publish the restored project folder. Check the destination and try again."));
    staged.setAutoRemove(false);
    return parent.absoluteFilePath(name + QLatin1Char('/') + filename);
}
