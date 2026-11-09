#include "board/model/BoardProjectFolder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
QString tr(char const* message) { return QCoreApplication::translate("BoardProject", message); }

bool fail(QString* error, QString const& message)
{
    if (error)
        *error = message;
    return false;
}

bool localFolder(QFileInfo const& info) { return info.isDir() && !info.isSymLink() && !info.isJunction(); }
bool localFile(QFileInfo const& info) { return info.isFile() && !info.isSymLink(); }

bool consume(qsizetype& remaining, qsizetype bytes, QString* error)
{
    if (bytes > remaining)
        return fail(error, tr("The project assets exceed the size limit."));
    remaining -= bytes;
    return true;
}

QString digest(QByteArray const& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool writeFile(QString const& path, QByteArray const& bytes, QString* error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
        return true;
    return fail(error, tr("Could not write “%1”: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
}

class Assets {
public:
    explicit Assets(QDir root, QString* error) : m_root(std::move(root)), m_error(error) {}

    QString put(QString const& directory, QString const& suffix, QByteArray const& bytes)
    {
        QString const relative = directory + QLatin1Char('/') + digest(bytes) + suffix;
        if (m_written.contains(relative))
            return relative;
        QString const path = m_root.filePath(relative);
        QFileInfo const info(path);
        if (info.exists() || info.isSymLink()) {
            QFile file(path);
            if (!localFile(info) || info.size() != bytes.size() || !file.open(QIODevice::ReadOnly) || file.read(bytes.size() + 1) != bytes) {
                fail(m_error, tr("A project asset conflicts with “%1”.").arg(relative));
                return {};
            }
        } else if (!writeFile(path, bytes, m_error)) {
            return {};
        }
        m_written.insert(relative);
        return relative;
    }

    QByteArray get(QString const& path, QRegularExpression const& pattern)
    {
        auto const match = pattern.match(path);
        if (!match.hasMatch()) {
            fail(m_error, tr("The project contains an invalid asset path."));
            return {};
        }
        auto const cached = m_read.constFind(path);
        if (cached != m_read.cend())
            return *cached;
        QFileInfo const info(m_root.filePath(path));
        if (!localFolder(QFileInfo(m_root.filePath(QStringLiteral("elements")))) || !localFolder(QFileInfo(info.absolutePath()))
            || !localFile(info) || info.size() <= 0 || info.size() > m_readRemaining) {
            fail(m_error, tr("The project asset “%1” is missing, unsafe or too large.").arg(path));
            return {};
        }
        QFile file(info.absoluteFilePath());
        QByteArray const bytes = file.open(QIODevice::ReadOnly) ? file.read(info.size() + 1) : QByteArray();
        if (bytes.size() != info.size() || file.error() != QFileDevice::NoError || digest(bytes) != match.captured(1)) {
            fail(m_error, tr("The project asset “%1” could not be read or is damaged.").arg(path));
            return {};
        }
        m_readRemaining -= bytes.size();
        m_read.insert(path, bytes);
        return bytes;
    }

private:
    QDir m_root;
    QString* m_error;
    QSet<QString> m_written;
    QHash<QString, QByteArray> m_read;
    qsizetype m_readRemaining = BoardProjectFolder::maxProjectBytes;
};

bool mapLayers(QJsonObject& json, auto const& convert)
{
    auto mapArray = [&convert](QJsonArray& layers) {
        for (qsizetype i = 0; i < layers.size(); ++i) {
            auto layer = layers[i].toObject();
            if (!convert(layer))
                return false;
            layers[i] = layer;
        }
        return true;
    };
    if (json.value("library").isArray()) {
        auto library = json.value("library").toArray();
        if (!mapArray(library))
            return false;
        json["library"] = library;
    }
    auto content = json["content"].toObject();
    auto shots = content["shots"].toArray();
    for (qsizetype s = 0; s < shots.size(); ++s) {
        auto shot = shots[s].toObject();
        auto panels = shot["panels"].toArray();
        for (qsizetype p = 0; p < panels.size(); ++p) {
            auto panel = panels[p].toObject();
            if (!panel.value("layers").isArray())
                continue;
            auto layers = panel["layers"].toArray();
            if (!mapArray(layers))
                return false;
            panel["layers"] = layers;
            panels[p] = panel;
        }
        shot["panels"] = panels;
        shots[s] = shot;
    }
    content["shots"] = shots;
    json["content"] = content;
    return true;
}
}

QRegularExpression const& BoardProjectFolder::drawingAssetPattern()
{
    static QRegularExpression const pattern(QStringLiteral(R"(\Aelements/drawings/([0-9a-f]{64})\.ecd\z)"));
    return pattern;
}

QRegularExpression const& BoardProjectFolder::pictureAssetPattern()
{
    static QRegularExpression const pattern(QStringLiteral(R"(\Aelements/pictures/([0-9a-f]{64})\.(?:png|tif)\z)"));
    return pattern;
}

bool BoardProjectFolder::save(QString const& path, QByteArray const& embedded, QString* error, QString const& assetRoot)
{
    QDir const root = assetRoot.isEmpty() ? QFileInfo(path).absoluteDir() : QDir(assetRoot);
    QLockFile lock(root.filePath(QLatin1String(saveLockName)));
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
        return fail(error, tr("Could not lock the project folder for saving."));
    if (QFileInfo(path).isSymLink())
        return fail(error, tr("Choose a regular project file for saving."));
    for (auto directory : {"elements", "elements/drawings", "elements/pictures", "palette-library", "frames", "audio"}) {
        QString const name = QLatin1String(directory);
        QFileInfo const info(root.filePath(name));
        if ((info.exists() || info.isSymLink()) ? !localFolder(info) : !root.mkdir(name))
            return fail(error, tr("Could not prepare the project folder “%1”.").arg(name));
    }

    auto manifest = QJsonDocument::fromJson(embedded).object();
    Assets assets(root, error);
    qsizetype budget = maxProjectBytes;
    auto const pictures = manifest.value("images").toObject();
    QJsonObject externalPictures;
    QHash<QString, QString> pictureIds;
    for (auto it = pictures.constBegin(); it != pictures.constEnd(); ++it) {
        auto const encoded = it.value().toString().toLatin1();
        auto const bytes = QByteArray::fromBase64(encoded);
        QString const suffix = bytes.startsWith("\x89PNG\r\n\x1a\n") ? QStringLiteral(".png") : QStringLiteral(".tif");
        QString const file = assets.put(QStringLiteral("elements/pictures"), suffix, bytes);
        if (file.isEmpty())
            return false;
        QString const key = QFileInfo(file).completeBaseName();
        if (!externalPictures.contains(key) && !consume(budget, encoded.size(), error))
            return false;
        pictureIds.insert(it.key(), key);
        externalPictures[key] = file;
    }
    if (!externalPictures.isEmpty())
        manifest["images"] = externalPictures;
    if (!mapLayers(manifest, [&](QJsonObject& layer) {
            auto drawing = layer.take("drawing").toObject();
            if (drawing.contains("pictures")) {
                auto placed = drawing["pictures"].toArray();
                for (qsizetype i = 0; i < placed.size(); ++i) {
                    auto picture = placed[i].toObject();
                    QString const id = pictureIds.value(picture["image"].toString());
                    if (id.isEmpty())
                        return fail(error, tr("A drawing refers to a missing project image."));
                    picture["image"] = id;
                    placed[i] = picture;
                }
                drawing["pictures"] = placed;
            }
            auto const bytes = QJsonDocument(drawing).toJson(QJsonDocument::Compact);
            if (!consume(budget, bytes.size(), error))
                return false;
            QString const file = assets.put(QStringLiteral("elements/drawings"), QStringLiteral(".ecd"), bytes);
            if (file.isEmpty())
                return false;
            layer["drawingFile"] = file;
            return true;
        }))
        return false;
    manifest["storage"] = QStringLiteral("folder");
    auto const manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (manifestBytes.size() > maxProjectBytes)
        return fail(error, tr("The project manifest exceeds the size limit."));
    return writeFile(path, manifestBytes, error);
}

bool BoardProjectFolder::assemble(QString const& path, QJsonObject& manifest, QString* error)
{
    if (manifest["storage"].toString() != QLatin1String("folder") || !manifest["content"].isObject()
        || (manifest.contains("images") && !manifest["images"].isObject()))
        return fail(error, tr("The project has an invalid folder manifest."));
    Assets assets(QFileInfo(path).absoluteDir(), error);
    qsizetype budget = maxProjectBytes;
    auto pictures = manifest.value("images").toObject();
    for (auto it = pictures.begin(); it != pictures.end(); ++it) {
        QString const file = it.value().toString();
        if (it.key() != QFileInfo(file).completeBaseName())
            return fail(error, tr("A project image has an invalid identifier."));
        auto const bytes = assets.get(file, pictureAssetPattern());
        if (bytes.isEmpty())
            return false;
        if (!consume(budget, ((bytes.size() + 2) / 3) * 4, error))
            return false;
        it.value() = QString::fromLatin1(bytes.toBase64());
    }
    if (!pictures.isEmpty())
        manifest["images"] = pictures;
    QHash<QString, QJsonObject> drawings;
    return mapLayers(manifest, [&](QJsonObject& layer) {
        if (layer.contains("drawing"))
            return fail(error, tr("A project layer has conflicting drawing data."));
        QString const file = layer.take("drawingFile").toString();
        auto const bytes = assets.get(file, drawingAssetPattern());
        if (bytes.isEmpty())
            return false;
        if (!consume(budget, bytes.size(), error))
            return false;
        auto cached = drawings.constFind(file);
        if (cached == drawings.cend()) {
            auto const document = QJsonDocument::fromJson(bytes);
            if (!document.isObject())
                return fail(error, tr("The drawing “%1” is invalid.").arg(file));
            drawings.insert(file, document.object());
            cached = drawings.constFind(file);
        }
        layer["drawing"] = *cached;
        return true;
    });
}
