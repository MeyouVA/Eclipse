#include "board/model/BoardProject.h"
#include "board/model/BoardProjectJson.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>

namespace {
constexpr qint64 kFileLimit = 128 * 1024 * 1024;

QString tr(char const* message)
{
    return QCoreApplication::translate("BoardProject", message);
}

bool validName(QString const& name)
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

bool validProject(BoardProject const& project)
{
    return validName(project.name) && project.title.size() <= 4096 && project.subtitle.size() <= 4096
        && project.width >= 1 && project.width <= 32768 && project.height >= 1 && project.height <= 32768
        && std::isfinite(project.fps) && project.fps >= 1.0 && project.fps <= 240.0;
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
    QDir const parent(directory);
    if (directory.trimmed().isEmpty() || !parent.exists())
        return fail(tr("The project directory does not exist."));
    if (!parent.mkdir(name))
        return fail(tr("Could not create the project folder. The name may already be in use or the directory may be read-only."));

    QString const path = parent.absoluteFilePath(name + QLatin1Char('/') + name + QStringLiteral(".ecb"));
    BoardProject initial = *this;
    if (initial.shots.isEmpty())
        initial.shots = {{QStringLiteral("1"), {}, {}, {blankPanel(int(std::lround(fps)))}}};
    if (initial.save(path, error))
        return path;
    parent.rmdir(name);
    return {};
}

bool BoardProject::save(QString const& path, QString* error) const
{
    QByteArray const bytes = toBytes(error);
    if (bytes.isEmpty())
        return false;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
        return true;
    if (error)
        *error = tr("Could not save the project: %1").arg(file.errorString());
    return false;
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
    if (!BoardProjectJson::encode(*this, json, error))
        return {};
    QByteArray const bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (bytes.size() > kFileLimit)
        return fail(tr("The project exceeds the file size limit."));
    return bytes;
}

bool BoardProject::open(QString const& path, BoardProject& project, QString* error)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray const bytes = file.read(kFileLimit + 1);
        if (file.error() == QFile::NoError)
            return fromBytes(bytes, project, error);
    }
    if (error)
        *error = file.errorString();
    return false;
}

bool BoardProject::fromBytes(QByteArray const& bytes, BoardProject& project, QString* error)
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
    QJsonObject const json = document.object();
    if (json.value(QStringLiteral("format")).toString() != QLatin1String("eclipse-board")
        || json.value(QStringLiteral("version")).toInt() != 1)
        return fail(tr("This is not a supported Eclipse Storyboard file."));

    BoardProject loaded{json.value(QStringLiteral("name")).toString(), json.value(QStringLiteral("title")).toString(),
                        json.value(QStringLiteral("subtitle")).toString(), json.value(QStringLiteral("width")).toInt(),
                        json.value(QStringLiteral("height")).toInt(), json.value(QStringLiteral("fps")).toDouble(), {}};
    if (!validProject(loaded))
        return fail(tr("The project has an invalid name or camera settings."));
    if (!BoardProjectJson::decode(json, loaded, error))
        return false;
    project = std::move(loaded);
    return true;
}
