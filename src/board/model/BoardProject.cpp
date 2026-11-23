#include "board/model/BoardProject.h"
#include "board/model/BoardProjectFolder.h"
#include "board/model/BoardProjectJson.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cmath>

namespace {
constexpr qsizetype kFileLimit = BoardProjectFolder::maxProjectBytes;

QString tr(char const* message)
{
    return QCoreApplication::translate("BoardProject", message);
}
}

bool BoardProject::validName(QString const& name)
{
    if (name.isEmpty() || name.size() > 120 || name != name.trimmed() || name.endsWith(QLatin1Char('.')))
        return false;
    for (QChar c : name)
        if (c.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(c))
            return false;
    static QRegularExpression const reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³]) *(\\.|$)"),
                                             QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(name).hasMatch();
}

bool BoardProject::validFormat(int width, int height, double fps)
{
    return width >= 1 && width <= 32768 && height >= 1 && height <= 32768 && std::isfinite(fps) && fps >= 1.0 && fps <= 240.0;
}

namespace {
bool validProject(BoardProject const& project)
{
    return BoardProject::validName(project.name) && project.title.size() <= 4096 && project.subtitle.size() <= 4096
        && project.episode.size() <= 4096 && project.copyright.size() <= 4096
        && project.startFrame >= 0 && project.startFrame <= BoardProject::maxStartFrame
        && BoardProject::validFormat(project.width, project.height, project.fps);
}

bool readProject(QByteArray const& bytes, QString const& path, BoardProject& project, QString* error, bool* legacy = nullptr)
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return false;
    };
    if (bytes.size() > kFileLimit)
        return fail(tr("The project exceeds the file size limit."));
    QJsonParseError parse;
    QJsonDocument const document = QJsonDocument::fromJson(bytes, &parse);
    if (!document.isObject())
        return fail(tr("Invalid project JSON: %1").arg(parse.errorString()));
    auto json = document.object();
    if (json["format"].toString() != QLatin1String("eclipse-board") || json["version"].toInt() != 1)
        return fail(tr("This is not a supported Eclipse Storyboard file."));

    BoardProject loaded{json["name"].toString(), json["title"].toString(), json["subtitle"].toString(),
                        json["width"].toInt(), json["height"].toInt(), json["fps"].toDouble(), {}};
    if ((json.contains("episode") && !json["episode"].isString()) || (json.contains("copyright") && !json["copyright"].isString())
        || (json.contains("startFrame") && !json["startFrame"].isDouble()))
        return fail(tr("The project has invalid title details."));
    loaded.episode = json["episode"].toString();
    loaded.copyright = json["copyright"].toString();
    loaded.startFrame = json.contains("startFrame") ? json["startFrame"].toInt(-1) : 0;
    if (!validProject(loaded))
        return fail(tr("The project has an invalid name or camera settings."));
    if (json.contains("storage")) {
        if (path.isEmpty())
            return fail(tr("Open this project from its folder to read its artwork."));
        if (!BoardProjectFolder::assemble(path, json, error))
            return false;
    }
    if (!BoardProjectJson::decode(json, loaded, error))
        return false;
    if (legacy)
        *legacy = !json.contains("storage");
    project = std::move(loaded);
    return true;
}
}

QString BoardProject::create(QString const& directory, QString* error) const
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return QString();
    };
    if (!validProject(*this))
        return fail(tr("Enter a valid project name, title and camera settings."));
    QDir parent(directory);
    if (directory.trimmed().isEmpty() || !parent.exists())
        return fail(tr("The project directory does not exist."));
    QFileInfo const destination(parent.filePath(name));
    if (destination.exists() || destination.isSymLink())
        return fail(tr("A file or folder with that project name already exists."));
    QTemporaryDir staged(parent.filePath(QStringLiteral(".board-create-XXXXXX")));
    if (!staged.isValid())
        return fail(tr("Could not prepare the project folder: %1").arg(staged.errorString()));

    QString const filename = name + QStringLiteral(".ecb");
    BoardProject initial = *this;
    if (initial.shots.isEmpty())
        initial.shots = {{QStringLiteral("1"), {}, {}, {blankPanel(int(std::lround(fps)))}}};
    if (!initial.save(staged.filePath(filename), error))
        return {};
    if (!parent.rename(QDir(staged.path()).dirName(), name))
        return fail(tr("Could not publish the project folder. The name may already be in use or the directory may be read-only."));
    staged.setAutoRemove(false);
    return QDir(destination.absoluteFilePath()).absoluteFilePath(filename);
}

bool BoardProject::save(QString const& path, QString* error) const
{
    QByteArray const bytes = toBytes(error);
    return !bytes.isEmpty() && BoardProjectFolder::save(path, bytes, error);
}

QByteArray BoardProject::toBytes(QString* error) const
{
    if (error)
        error->clear();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    if (!validProject(*this))
        return fail(tr("Enter a valid project name, title and camera settings."));
    QJsonObject json{{"format", "eclipse-board"}, {"version", 1}, {"name", name}, {"title", title},
                     {"subtitle", subtitle}, {"width", width}, {"height", height}, {"fps", fps}};
    if (!episode.isEmpty())
        json["episode"] = episode;
    if (!copyright.isEmpty())
        json["copyright"] = copyright;
    if (startFrame != 0)
        json["startFrame"] = startFrame;
    if (!BoardProjectJson::encode(*this, json, error))
        return {};
    QByteArray const bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (bytes.size() > kFileLimit)
        return fail(tr("The project exceeds the file size limit."));
    return bytes;
}

bool BoardProject::open(QString const& path, BoardProject& project, QString* error)
{
    return open(path, project, error, nullptr);
}

bool BoardProject::open(QString const& path, BoardProject& project, QString* error, bool* legacy)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray const bytes = file.read(kFileLimit + 1);
        if (file.error() == QFile::NoError)
            return readProject(bytes, path, project, error, legacy);
    }
    if (error)
        *error = file.errorString();
    return false;
}

bool BoardProject::fromBytes(QByteArray const& bytes, BoardProject& project, QString* error)
{
    return readProject(bytes, {}, project, error);
}
