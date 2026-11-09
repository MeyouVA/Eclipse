#include "board/model/BoardProjectJson.h"
#include "board/model/BoardProject.h"
#include "model/scene/SceneReadParts.h"
#include "model/scene/SceneWriteParts.h"
#include "render/images/ImageFiles.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool readCamera(QJsonValue const& value, BoardPanel& panel)
{
    if (value.isUndefined())
        return true;
    if (!value.isArray())
        return false;
    double const missing = std::numeric_limits<double>::quiet_NaN();
    int previous = -1;
    for (auto const& entry : value.toArray()) {
        auto const key = entry.toObject();
        int const frame = key["frame"].toInt(-1);
        BoardCameraPose const pose{{key["x"].toDouble(missing), key["y"].toDouble(missing)},
                                   key["zoom"].toDouble(missing), key["rotation"].toDouble(missing)};
        if (frame <= previous || frame >= panel.length || !BoardCamera::valid(pose))
            return false;
        panel.camera.push_back({frame, pose});
        previous = frame;
    }
    return true;
}

bool writeCamera(BoardPanel const& panel, QJsonObject& json)
{
    QJsonArray camera;
    int previous = -1;
    for (auto const& key : panel.camera) {
        if (key.frame <= previous || key.frame >= panel.length || !BoardCamera::valid(key.pose))
            return false;
        camera.append(QJsonObject{{"frame", key.frame}, {"x", key.pose.offset.x()}, {"y", key.pose.offset.y()},
                                  {"zoom", key.pose.zoom}, {"rotation", key.pose.rotation}});
        previous = key.frame;
    }
    if (!camera.isEmpty())
        json["camera"] = camera;
    return true;
}

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
        if (image.isNull() || decodedBytes > BoardProjectJson::maxEmbeddedImagesBytes)
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
    layer.locked = json["locked"].toBool(false);
    layer.drawing = scene_read::drawingFromJson(drawing, {}, images);
    return BoardProjectJson::supportedDrawing(layer.drawing);
}
}

bool BoardProjectJson::supportedDrawing(Drawing const& drawing)
{
    if (!drawing.model.isEmpty())
        return false;
    for (Picture const& picture : drawing.pictures)
        if (picture.linked || picture.image.isNull() || picture.image.sizeInBytes() > maxEmbeddedImageBytes)
            return false;
    return true;
}

bool BoardProjectJson::validSound(BoardSound const& sound)
{
    if (sound.asset.isEmpty())
        return sound == BoardSound{};
    static QRegularExpression const assetPattern(QStringLiteral("\\A[0-9a-f]{32}(?:\\.[A-Za-z0-9]{1,12})?\\z"));
    return assetPattern.match(sound.asset).hasMatch() && !sound.name.trimmed().isEmpty() && sound.name.size() <= 256
        && sound.start >= 0 && sound.sourceOffset >= 0 && sound.length > 0
        && qint64(std::max(sound.start, sound.sourceOffset)) + sound.length <= std::numeric_limits<int>::max();
}

bool BoardProjectJson::encode(BoardProject const& project, QJsonObject& json, QString* error)
{
    if (!validSound(project.sound))
        return fail(error, "The sound track is invalid.");
    if (!project.sound.asset.isEmpty()) {
        auto const& sound = project.sound;
        QJsonObject stored{{"asset", sound.asset}, {"name", sound.name}, {"start", sound.start},
                           {"length", sound.length}, {"muted", sound.muted}};
        if (sound.sourceOffset != 0)
            stored["sourceOffset"] = sound.sourceOffset;
        json["sound"] = stored;
    }
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
                        if (imageBytes > maxEmbeddedImagesBytes)
                            return fail(error, "The embedded layer images are too large.");
                    }
                }
                layers.append(QJsonObject{{"name", layer.name}, {"visible", layer.visible}, {"locked", layer.locked},
                    {"kind", layer.kind == BoardLayer::Kind::Bitmap ? "bitmap" : "vector"},
                    {"drawing", scene_write::drawingToJson(layer.drawing, {}, images, imageKeys)}});
            }
            panel["layers"] = layers;
            if (!writeCamera(project.shots[s].panels[p], panel))
                return fail(error, "A panel contains an invalid camera key.");
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
    if (json.contains("sound")) {
        auto const audio = json["sound"].toObject();
        project.sound = {audio["asset"].toString(), audio["name"].toString(),
                         audio["start"].toInt(-1), audio["length"].toInt(-1), audio["muted"].toBool(false),
                         audio.contains("sourceOffset") ? audio["sourceOffset"].toInt(-1) : 0};
        if (!validSound(project.sound))
            return fail(error, "The sound track is invalid.");
    }
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
        if (timed.transition == QLatin1String("Dissolve"))
            shot.dissolve = std::clamp(timed.transitionLength, 0, 1000000);
        auto const panels = shots[s].toObject()["panels"].toArray();
        for (qsizetype p = 0; p < panels.size(); ++p) {
            BoardPanel panel;
            static_cast<Storyboard::Panel&>(panel) = timed.panels[p];
            auto const panelObject = panels[p].toObject();
            if (!readCamera(panelObject["camera"], panel))
                return fail(error, "A panel contains an invalid camera key.");
            auto const layers = panelObject["layers"];
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
