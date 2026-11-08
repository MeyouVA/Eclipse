#include "board/model/BoardProjectJson.h"
#include "board/model/BoardProject.h"
#include "model/scene/SceneReadParts.h"
#include "model/scene/SceneWriteParts.h"
#include "render/images/ImageFiles.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool fail(QString* error, char const* message)
{
    if (error)
        *error = QCoreApplication::translate("BoardProject", message);
    return false;
}

bool validTimeline(QJsonObject const& board)
{
    if (!board["shots"].isArray() || !board["captionFields"].isArray())
        return false;
    auto const shots = board["shots"].toArray();
    auto const fields = board["captionFields"].toArray();
    if (shots.isEmpty() || shots.size() > 50000 || fields.size() > 128)
        return false;
    QSet<QString> captionNames;
    for (auto const& value : fields) {
        QString const name = value.toString().trimmed().toCaseFolded();
        if (name.isEmpty() || name.size() > 128 || captionNames.contains(name))
            return false;
        captionNames.insert(name);
    }
    qint64 frames = 0;
    qsizetype panels = 0;
    for (auto const& value : shots) {
        auto const shot = value.toObject();
        auto const shotPanels = shot["panels"].toArray();
        if (shot["name"].toString().trimmed().isEmpty() || shotPanels.isEmpty())
            return false;
        for (auto const& panelValue : shotPanels) {
            auto const panel = panelValue.toObject();
            int const length = panel["length"].toInt();
            if (length < 1 || length > 1000000 || ++panels > 50000)
                return false;
            frames += length;
            if (frames > std::numeric_limits<int>::max())
                return false;
            if (panel.contains("captions") && !panel["captions"].isArray())
                return false;
            QSet<QString> used;
            for (auto const& captionValue : panel["captions"].toArray()) {
                auto const caption = captionValue.toObject();
                QString const field = caption["field"].toString().trimmed().toCaseFolded();
                if (!captionNames.contains(field) || used.contains(field) || !caption["text"].isString())
                    return false;
                used.insert(field);
            }
        }
    }
    return true;
}

bool readImages(QJsonValue const& value, QHash<QString, QImage>& images)
{
    if (value.isUndefined())
        return true;
    if (!value.isObject())
        return false;
    auto const stored = value.toObject();
    qint64 decodedBytes = 0;
    for (auto it = stored.begin(); it != stored.end(); ++it) {
        auto const bytes = QByteArray::fromBase64Encoding(it.value().toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes || (!bytes.decoded.startsWith("\x89PNG\r\n\x1a\n")
                       && !bytes.decoded.startsWith(QByteArray("II*\0", 4))
                       && !bytes.decoded.startsWith(QByteArray("MM\0*", 4))))
            return false;
        QImage const image = ImageFiles::decodeEmbedded(bytes.decoded);
        decodedBytes += image.sizeInBytes();
        if (image.isNull() || decodedBytes > 512 * 1024 * 1024)
            return false;
        scene_write::rememberPng(image, bytes.decoded);
        images.insert(it.key(), image);
    }
    return true;
}

bool readLayer(QJsonObject const& json, QHash<QString, QImage> const& images, BoardLayer& layer)
{
    auto const kind = json["kind"].toString();
    auto const drawing = json["drawing"].toObject();
    layer.name = json["name"].toString();
    if (layer.name.trimmed().isEmpty() || (kind != QLatin1String("vector") && kind != QLatin1String("bitmap"))
        || !json["drawing"].isObject() || !drawing["model"].toString().isEmpty())
        return false;
    for (auto const& value : drawing["pictures"].toArray()) {
        auto const picture = value.toObject();
        if (picture["linked"].toBool() || !images.contains(picture["image"].toString()))
            return false;
    }
    layer.kind = kind == QLatin1String("bitmap") ? BoardLayer::Kind::Bitmap : BoardLayer::Kind::Vector;
    layer.visible = json["visible"].toBool(true);
    layer.drawing = scene_read::drawingFromJson(drawing, {}, images);
    return BoardProjectJson::supportedDrawing(layer.drawing);
}
}

bool BoardProjectJson::supportedDrawing(Drawing const& drawing)
{
    if (!drawing.model.isEmpty())
        return false;
    for (Picture const& picture : drawing.pictures)
        if (picture.linked || picture.image.isNull() || picture.image.sizeInBytes() > 128 * 1024 * 1024)
            return false;
    return true;
}

bool BoardProjectJson::encode(BoardProject const& project, QJsonObject& json, QString* error)
{
    QJsonObject content = project.timeline().toJson();
    if (!validTimeline(content))
        return fail(error, "Invalid scenes, panels or captions.");
    auto shots = content["shots"].toArray();
    QJsonObject images;
    QHash<qint64, QString> imageKeys;
    QSet<qint64> countedImages;
    qint64 imageBytes = 0;
    for (qsizetype s = 0; s < shots.size(); ++s) {
        auto shot = shots[s].toObject();
        auto panels = shot["panels"].toArray();
        for (qsizetype p = 0; p < panels.size(); ++p) {
            auto panel = panels[p].toObject();
            QJsonArray layers;
            for (BoardLayer const& layer : project.shots[s].panels[p].layers) {
                if (layer.name.trimmed().isEmpty() || !supportedDrawing(layer.drawing))
                    return fail(error, "A layer has an invalid name or unsupported artwork.");
                for (Picture const& picture : layer.drawing.pictures) {
                    qint64 const key = picture.image.cacheKey();
                    if (!countedImages.contains(key)) {
                        countedImages.insert(key);
                        imageBytes += picture.image.sizeInBytes();
                        if (imageBytes > 512 * 1024 * 1024)
                            return fail(error, "The embedded layer images are too large.");
                    }
                }
                layers.append(QJsonObject{{"name", layer.name}, {"visible", layer.visible},
                    {"kind", layer.kind == BoardLayer::Kind::Bitmap ? "bitmap" : "vector"},
                    {"drawing", scene_write::drawingToJson(layer.drawing, {}, images, imageKeys)}});
            }
            panel["layers"] = layers;
            panels[p] = panel;
        }
        shot["panels"] = panels;
        shots[s] = shot;
    }
    for (auto it = images.begin(); it != images.end(); ++it)
        if (it.value().toString().isEmpty())
            return fail(error, "Could not encode a layer image.");
    content["shots"] = shots;
    json["content"] = content;
    if (!images.isEmpty())
        json["images"] = images;
    return true;
}

bool BoardProjectJson::decode(QJsonObject const& json, BoardProject& project, QString* error)
{
    if (!json.contains("content")) {
        if (json.contains("images"))
            return fail(error, "Project content is missing.");
        project.shots = {{QStringLiteral("1"), {}, {}, {BoardProject::blankPanel(int(std::lround(project.fps)))}}};
        return true;
    }
    auto const content = json["content"].toObject();
    if (!validTimeline(content))
        return fail(error, "Invalid scenes, panels or captions.");
    QHash<QString, QImage> images;
    if (!readImages(json["images"], images))
        return fail(error, "The embedded layer images are invalid or too large.");

    Storyboard const timing = Storyboard::fromJson(content);
    auto const shots = content["shots"].toArray();
    for (qsizetype s = 0; s < shots.size(); ++s) {
        auto const& timed = timing.shots[s];
        BoardShot shot{timed.name, timed.sequence, timed.act, {}};
        auto const panels = shots[s].toObject()["panels"].toArray();
        for (qsizetype p = 0; p < panels.size(); ++p) {
            BoardPanel panel;
            static_cast<Storyboard::Panel&>(panel) = timed.panels[p];
            auto const layers = panels[p].toObject()["layers"];
            if (!layers.isArray())
                return fail(error, "A panel has an invalid layer list.");
            for (auto const& layerValue : layers.toArray()) {
                BoardLayer layer;
                if (!readLayer(layerValue.toObject(), images, layer))
                    return fail(error, "A panel contains an invalid layer.");
                panel.layers.push_back(std::move(layer));
            }
            shot.panels.push_back(std::move(panel));
        }
        project.shots.push_back(std::move(shot));
    }
    project.captionFields = timing.captionFields;
    return true;
}
