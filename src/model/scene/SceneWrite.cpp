// Eclipse - the scene: written to a file.
#include "model/scene/TextureBank.h"
#include "model/scene/Scene.h"

#include <QBuffer>
#include <QFileInfo>
#include <QDir>

#include "model/geometry/Deform.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "model/scene/SceneWriteParts.h"

using namespace scene_write;

QJsonObject Scene::toJson(QString const& folder) const
{
    // Pictures kept in the file itself: each image once, as a PNG under a key the pictures that show it name.
    QJsonObject images;
    QHash<qint64, QString> imageKeys;
    QJsonArray layerArray;
    for (Layer const& layer : layers)
        layerArray.append(layerToJson(layer, folder, images, imageKeys));
    QJsonArray paletteArray;
    for (Swatch const& s : palette) {
        QJsonObject swatch{{"id", s.id}, {"name", s.name}, {"color", s.color.name(QColor::HexArgb)}, {"palette", s.palette}};
        if (s.isGradient()) {
            swatch["kind"] = int(s.kind);
            swatch["stops"] = s.stopsJson();
        }
        if (s.isTexture())
            swatch["texture"] = s.textureText();
        if (s.isProtected)
            swatch["protected"] = true;
        if (s.cloneOf != 0)
            swatch["clone"] = s.cloneOf;
        paletteArray.append(swatch);
    }
    QJsonArray ownerArray;
    for (int i = 0; i < paletteNames.size(); ++i)
        ownerArray.append(paletteOwner(i));
    QJsonArray markerArray;
    for (Marker const& m : markers) {
        QJsonObject marker{{"frame", m.frame}, {"length", m.length}, {"name", m.name}, {"color", m.color.name()}};
        if (!m.note.isEmpty())
            marker["note"] = m.note;
        markerArray.append(marker);
    }
    QJsonArray guideArray;
    for (Guide const& guide : guides)
        guideArray.append(guideToJson(guide));
    QJsonArray referenceArray;
    for (QString const& file : references)
        referenceArray.append(QJsonObject{{"file", file}, {"relative", pathFrom(folder, file)}});
    QJsonArray deskArray;
    for (DeskItem const& item : desk)
        deskArray.append(QJsonObject{{"layer", item.layer}, {"drawing", item.drawing}, {"shown", item.shown}});
    // A symbol is written as a scene of its own (without symbols inside it: they are all here, side by side).
    QJsonArray symbolArray;
    for (Symbol const& symbol : symbols) {
        Scene inner = symbolScene(symbol);
        inner.symbols.clear();
        symbolArray.append(QJsonObject{{"id", symbol.id}, {"name", symbol.name}, {"scene", inner.toJson(folder)}});
    }
    // The textures of your own that anything in the scene is drawn with go with it.
    QJsonArray textureArray;
    {
        QVector<int> used;
        auto note = [&](int texture) {
            if (texture >= TextureBank::kFirst && !used.contains(texture))
                used.push_back(texture);
        };
        for (Layer const& layer : layers) {
            for (Drawing const& drawing : layer.drawings) {
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    for (Stroke const& stroke : drawing.strokesOf(sheet))
                        note(stroke.texture);
                    for (Fill const& fill : drawing.fillsOf(sheet))
                        note(fill.texture);
                }
            }
        }
        std::sort(used.begin(), used.end());
        for (int id : std::as_const(used)) {
            TextureBank::Entry const entry = TextureBank::entry(id);
            if (entry.id != 0)
                textureArray.append(QJsonObject{{"name", entry.name}, {"edge", entry.edge}, {"png", TextureBank::grainToText(entry.grain)}});
        }
    }
    QJsonArray controllerArray;
    for (Controller const& c : controllers) {
        QJsonArray states;
        for (Controller::State const& s : c.states) {
            QJsonArray poses;
            for (Pose const& p : s.poses) {
                QJsonArray bend;
                for (double b : p.bend)
                    bend.append(b);
                QJsonObject one{{"x", p.x}, {"y", p.y}, {"sx", p.scaleX}, {"sy", p.scaleY}, {"rot", p.rotation}, {"skew", p.skew},
                                {"z", p.z}, {"op", p.opacity}, {"bend", bend}};
                // (What the rest of a keyframe has, kept only when there is any of it.)
                if (p.rotX != 0.0)
                    one["rx"] = p.rotX;
                if (p.rotY != 0.0)
                    one["ry"] = p.rotY;
                if (p.fov != 0.0)
                    one["fov"] = p.fov;
                if (!p.fx.isEmpty()) {
                    QJsonArray fx;
                    for (double f : p.fx)
                        fx.append(f);
                    one["fx"] = fx;
                }
                for (auto const& [key, points] : {std::pair<char const*, QVector<QPointF> const*>{"warp", &p.warp}, {"quad", &p.quad}}) {
                    if (points->isEmpty())
                        continue;
                    QJsonArray flat;
                    for (QPointF const& q : *points)
                        flat << q.x() << q.y();
                    one[QLatin1String(key)] = flat;
                }
                poses.append(one);
            }
            QJsonArray drawings;
            for (int d : s.drawings)
                drawings.append(d);
            QJsonObject state{{"name", s.name}, {"poses", poses}, {"drawings", drawings}};
            if (!s.at.isNull())
                state["at"] = QJsonArray{s.at.x(), s.at.y()};
            states.append(state);
        }
        QJsonArray layerIds;
        for (int id : c.layers)
            layerIds.append(id);
        QJsonObject controller{{"id", c.id}, {"name", c.name}, {"layers", layerIds}, {"states", states}, {"value", c.value}};
        if (c.point) {
            controller["point"] = true;
            controller["where"] = QJsonArray{c.where.x(), c.where.y()};
        }
        if (!c.keys.isEmpty()) {
            QJsonArray keys;
            for (auto it = c.keys.cbegin(); it != c.keys.cend(); ++it)
                keys.append(QJsonArray{it.key(), it.value()});
            controller["keys"] = keys;
        }
        if (!c.pointKeys.isEmpty()) {
            QJsonArray keys;
            for (auto it = c.pointKeys.cbegin(); it != c.pointKeys.cend(); ++it)
                keys.append(QJsonArray{it.key(), it.value().x(), it.value().y()});
            controller["pointKeys"] = keys;
        }
        controllerArray.append(controller);
    }
    QJsonArray writeArray;
    for (Write const& w : writes) {
        QJsonObject wo{{"name", w.name}, {"movie", w.movie}, {"path", w.path}, {"format", w.format}, {"percent", w.percent}, {"enabled", w.enabled}};
        if (w.layers)
            wo["layers"] = true;
        writeArray.append(wo);
    }
    QJsonObject whole{{"format", "eclipse-scene"},
                       {"controllers", controllerArray},
                       {"writes", writeArray},
                       {"symbols", symbolArray},
                       {"version", 3},
                       {"markers", markerArray},
                       {"palette", paletteArray},
                       {"paletteNames", QJsonArray::fromStringList(paletteNames)},
                       {"paletteLinks", QJsonArray::fromStringList(paletteLinks)},
                       {"paletteOwners", ownerArray},
                       {"width", width},
                       {"height", height},
                       {"fps", fps},
                       {"soloMode", soloMode},
                       {"framesPerBeat", framesPerBeat},
                       {"beatsPerBar", beatsPerBar},
                       {"orthographic", orthographic},
                       {"fovAcross", fovAcross},
                       {"fov", fov},
                       {"camera", activeCamera},
                       {"display", display},
                       {"references", referenceArray},
                       {"desk", deskArray},
                       {"guides", guideArray},
                       {"metadata", pairsJson(metadata)},
                       {"frames", m_frameCount},
                       {"background", background.name(QColor::HexArgb)},
                       {"layers", layerArray}};
    if (!functions.isEmpty()) {
        QJsonArray columns;
        for (FunctionColumn const& c : functions) {
            QJsonArray keys, path, velocity;
            for (Keyframe const& k : c.curve.keys)
                keys.append(keyToJson(k));
            for (FunctionColumn::PathPoint const& p : c.path)
                path.append(QJsonArray{p.x, p.y, p.z, p.frame, p.tension, p.continuity, p.bias, p.locked});
            for (Keyframe const& k : c.velocity.keys)
                velocity.append(keyToJson(k));
            QJsonObject column{{"id", c.id}, {"name", c.name}, {"kind", c.kind}};
            if (c.extendedTiming)
                column["extendedTiming"] = true;
            if (c.expressionOffset)
                column["expressionOffset"] = c.expressionOffset;
            if (c.expressionFrames)
                column["expressionFrames"] = c.expressionFrames;
            if (c.velocityRangeSet)
                column["velocityRange"] = QJsonArray{c.velocityStart, c.velocityEnd};
            if (!keys.isEmpty())
                column["points"] = keys;
            if (!c.expression.isEmpty())
                column["expression"] = c.expression;
            if (!path.isEmpty())
                column["path"] = path;
            if (!velocity.isEmpty())
                column["velocity"] = velocity;
            if (c.constantZ)
                column["constantZ"] = true;
            if (!c.shown)
                column["shown"] = false;
            columns.append(column);
        }
        whole["functions"] = columns;
    }
    if (!textureArray.isEmpty())
        whole["textures"] = textureArray;
    if (!images.isEmpty())
        whole["images"] = images;
    if (!graph.isEmpty())
        whole["graph"] = graphToJson(graph);
    if (!backdrops.isEmpty())
        whole["backdrops"] = backdropsToJson(backdrops);
    if (!sheetMarkers.isEmpty()) {
        QJsonArray sheet;
        for (SheetMarker const& m : sheetMarkers)
            sheet.append(QJsonObject{{"layer", m.layer}, {"frame", m.frame}, {"length", m.length}, {"colour", m.colour}});
        whole["sheetMarkers"] = sheet;
    }
    if (!uiState.isEmpty())
        whole["uiState"] = uiState;
    return whole;
}

bool Scene::save(QString const& path, QString* error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray bytes = QJsonDocument(toJson(QFileInfo(path).absolutePath())).toJson(QJsonDocument::Compact);
    // Packed (Preferences): a mark, then the scene zlib-packed.
    if (compressFiles())
        bytes = QByteArray("ECLZ") + qCompress(bytes, 9);
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // The palettes linked to a file of their own are written there too, for the other scenes linked to it.
    for (int i = 0; i < paletteLinks.size() && i < paletteNames.size(); ++i)
        if (!paletteLinks[i].isEmpty() && !writePaletteFile(i, paletteLinks[i])) {
            if (error)
                *error = QStringLiteral("Could not write linked palette: %1").arg(paletteLinks[i]);
            return false;
        }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

namespace {
bool gCompressFiles = false;
}

void Scene::setCompressFiles(bool on)
{
    gCompressFiles = on;
}

bool Scene::compressFiles()
{
    return gCompressFiles;
}
