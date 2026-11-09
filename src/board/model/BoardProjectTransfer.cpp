#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

namespace {
int frameOfScene(QVector<BoardSpan> const& spans, int shot)
{
    auto const first = std::lower_bound(spans.cbegin(), spans.cend(), shot,
        [](BoardSpan const& span, int scene) { return span.shot < scene; });
    return first == spans.cend() ? spans.last().start + spans.last().length : first->start;
}

bool copySound(QString const& source, QString const& destination)
{
    QFileInfo const folder(QFileInfo(destination).absolutePath());
    if (!QFileInfo(source).isFile() || folder.isSymLink() || folder.isJunction())
        return false;
    QDir const parent = folder.absoluteDir();
    bool const created = !folder.isDir() && parent.mkdir(folder.fileName());
    if (QFile::copy(source, destination))
        return true;
    if (created)
        parent.rmdir(folder.fileName());
    return false;
}
}

QString BoardDocument::extractProject(int first, int last, QString const& name, QString const& directory, QString* error) const
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return QString();
    };
    if (m_path.isEmpty() || first > last || !hasPanel(first, 0) || !hasPanel(last, 0))
        return fail(tr("Choose a valid range of scenes."));
    QDir parent(directory);
    if (directory.isEmpty() || !parent.exists())
        return fail(tr("Choose an existing folder for the extracted project."));

    QTemporaryDir const staging(parent.filePath(QStringLiteral(".board-extract-XXXXXX")));
    if (!staging.isValid())
        return fail(tr("Could not prepare the output folder."));
    QString const prepared = writeSceneRange(first, last, name, staging.path(), error);
    if (prepared.isEmpty())
        return {};
    QFileInfo const destination(parent.filePath(name));
    if (destination.exists() || destination.isSymLink())
        return fail(tr("A project folder with that name already exists."));
    if (!parent.rename(QFileInfo(prepared).absolutePath(), destination.absoluteFilePath()))
        return fail(tr("Could not publish the extracted project folder."));
    return parent.absoluteFilePath(name + QLatin1Char('/') + QFileInfo(prepared).fileName());
}

QString BoardDocument::writeSceneRange(int first, int last, QString const& name, QString const& directory, QString* error) const
{
    BoardProject extracted = m_project;
    extracted.name = name;
    extracted.shots = m_project.shots.mid(first, last - first + 1);
    extracted.shots.first().dissolve = 0;
    auto const spans = m_project.spans();
    extracted.sound = m_project.sound.within(frameOfScene(spans, first), frameOfScene(spans, last + 1));
    QString const prepared = extracted.create(directory, error);
    if (prepared.isEmpty())
        return {};
    if (extracted.sound.asset.isEmpty() || copySound(m_project.audioPath(m_path), extracted.audioPath(prepared)))
        return prepared;
    if (error)
        *error = tr("Could not copy the selected scenes’ sound.");
    return {};
}

QString BoardDocument::splitProject(int firstSceneOfSecondPart, QString const& name, QString const& directory, QString* error) const
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return QString();
    };
    if (m_path.isEmpty() || firstSceneOfSecondPart < 1 || !hasPanel(firstSceneOfSecondPart, 0))
        return fail(tr("Choose a scene boundary that leaves at least one scene in each part."));
    QDir parent(directory);
    if (directory.isEmpty() || !parent.exists() || !BoardProject::validName(name))
        return fail(tr("Choose an existing location and enter a valid output folder name."));
    QFileInfo const destination(parent.filePath(name));
    if (destination.exists() || destination.isSymLink())
        return fail(tr("An output folder with that name already exists."));
    QTemporaryDir staging(parent.filePath(QStringLiteral(".board-split-XXXXXX")));
    if (!staging.isValid())
        return fail(tr("Could not prepare the split project folder."));
    if (writeSceneRange(0, firstSceneOfSecondPart - 1, QStringLiteral("Part 1"), staging.path(), error).isEmpty()
        || writeSceneRange(firstSceneOfSecondPart, int(m_project.shots.size()) - 1, QStringLiteral("Part 2"), staging.path(), error).isEmpty())
        return {};
    if (!parent.rename(QFileInfo(staging.path()).fileName(), name))
        return fail(tr("Could not publish the split project folder."));
    staging.setAutoRemove(false);
    return destination.absoluteFilePath();
}

bool BoardDocument::insertProject(BoardProject source, QString const& sourceFile, int after, bool useImportedSound, QString* error)
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_path.isEmpty() || sourceFile.isEmpty() || !hasPanel(after, 0) || source.shots.isEmpty()
        || !BoardProjectJson::validSound(source.sound))
        return fail(tr("Choose a valid project and an insertion point."));
    if (source.width != m_project.width || source.height != m_project.height || source.fps != m_project.fps)
        return fail(tr("Both projects must use the same camera dimensions and frame rate."));

    qsizetype panels = 0;
    qint64 frames = 0;
    for (auto const& shot : std::as_const(source.shots))
        for (auto const& panel : shot.panels) {
            ++panels;
            frames += panel.length;
        }
    if (frames <= 0 || !canGrow(panels, frames))
        return fail(tr("The combined board exceeds the project’s timing limits."));

    BoardProject next = m_project;
    QHash<QString, QString> fields;
    for (auto const& field : std::as_const(next.captionFields))
        fields.insert(field.toCaseFolded(), field);
    for (auto const& field : std::as_const(source.captionFields)) {
        QString const key = field.toCaseFolded();
        if (!fields.contains(key)) {
            fields.insert(key, field);
            next.captionFields.append(field);
        }
    }
    if (next.captionFields.size() > 128)
        return fail(tr("The combined board has too many caption fields."));

    QSet<QString> names;
    for (auto const& shot : std::as_const(next.shots))
        names.insert(shot.name.toCaseFolded());
    source.shots.first().dissolve = 0;
    for (auto& shot : source.shots) {
        QString const base = shot.name;
        for (int number = 2; names.contains(shot.name.toCaseFolded()); ++number)
            shot.name = tr("%1 (%2)").arg(base).arg(number);
        names.insert(shot.name.toCaseFolded());
        for (auto& panel : shot.panels)
            for (auto& caption : panel.captions)
                caption.field = fields.value(caption.field.toCaseFolded(), caption.field);
    }

    QString audioSource;
    BoardSound imported = source.sound.within(0, int(frames));
    if (!imported.asset.isEmpty() && (useImportedSound || next.sound.asset.isEmpty())) {
        audioSource = source.audioPath(sourceFile);
        QString const suffix = QFileInfo(imported.asset).suffix();
        imported.asset = QUuid::createUuid().toString(QUuid::Id128);
        if (!suffix.isEmpty())
            imported.asset += QLatin1Char('.') + suffix;
        imported.start += frameOfScene(m_project.spans(), after + 1);
        next.sound = imported;
    }

    int const insertion = after + 1;
    auto tail = next.shots.mid(insertion);
    next.shots.resize(insertion);
    next.shots += std::move(source.shots);
    next.shots += std::move(tail);
    if (next.toBytes(error).isEmpty())
        return false;
    if (!audioSource.isEmpty() && !copySound(audioSource, next.audioPath(m_path)))
        return fail(tr("Could not copy the imported project’s sound."));
    apply(std::move(next), tr("Insert Project Scenes"));
    return true;
}
