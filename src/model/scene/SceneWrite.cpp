// Eclipse - the scene: written to a file.
#include "model/scene/Scene.h"

#include <QBuffer>
#include <QFileInfo>
#include <QDir>

#include "model/geometry/Deform.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>

// Strokes are stored compactly: one flat array [x, y, pressure, x, y, pressure, ...].

static QJsonObject strokeToJson(Stroke const& s)
{
    QJsonArray pts;
    for (StrokePoint const& p : s.points) {
        pts.append(std::round(p.pos.x() * 100.0) / 100.0);
        pts.append(std::round(p.pos.y() * 100.0) / 100.0);
        pts.append(std::round(double(p.pressure) * 1000.0) / 1000.0);
    }
    QJsonObject o{{"color", s.color.name(QColor::HexArgb)}, {"swatch", s.swatch}, {"width", double(s.width)}, {"points", pts}};
    if (s.group != 0)
        o["group"] = s.group;
    QJsonArray corners;
    for (int i = 0; i < s.points.size(); ++i)
        if (s.points[i].corner)
            corners.append(i);
    if (!corners.isEmpty())
        o["corners"] = corners;
    // The points that were given handles: index, in x y, out x y.
    QJsonArray handles;
    for (int i = 0; i < s.points.size(); ++i) {
        StrokePoint const& p = s.points[i];
        if (!p.handles)
            continue;
        handles.append(i);
        for (double v : {p.in.x(), p.in.y(), p.out.x(), p.out.y()})
            handles.append(std::round(v * 100.0) / 100.0);
    }
    if (!handles.isEmpty())
        o["handles"] = handles;
    if (s.join != Stroke::RoundJoin)
        o["join"] = s.join == Stroke::MitreJoin ? "mitre" : "bevel";
    if (s.capStart != Stroke::RoundCap)
        o["capStart"] = "flat";
    if (s.capEnd != Stroke::RoundCap)
        o["capEnd"] = "flat";
    if (s.curve)
        o["curve"] = true;
    if (s.closed)
        o["closed"] = true;
    return o;
}

static QJsonObject fillToJson(Fill const& f)
{
    QJsonArray outlines;
    for (QPolygonF const& o : f.outlines) {
        QJsonArray pts;
        for (QPointF const& p : o) {
            pts.append(std::round(p.x() * 100.0) / 100.0);
            pts.append(std::round(p.y() * 100.0) / 100.0);
        }
        outlines.append(pts);
    }
    QJsonObject o{{"color", f.color.name(QColor::HexArgb)}, {"swatch", f.swatch}, {"outlines", outlines}};
    if (f.gradPlaced)
        o["grad"] = QJsonArray{f.gradFrom.x(), f.gradFrom.y(), f.gradTo.x(), f.gradTo.y()};
    if (f.group != 0)
        o["group"] = f.group;
    if (!f.text.isEmpty()) {
        o["text"] = f.text;
        o["textAlign"] = f.textAlign;
        o["textSpacing"] = f.textSpacing;
        if (f.textKerning != 100)
            o["textKerning"] = f.textKerning;
        o["textFont"] = f.textFont;
        o["textAt"] = QJsonArray{f.textAt.x(), f.textAt.y()};
    }
    if (!f.contours.isEmpty()) {
        // (The outlines are written too, so that the file still shows in a reader that does
        // not know contours; this one makes them again from the contours.)
        QJsonArray contours;
        for (Stroke const& contour : f.contours)
            contours.append(strokeToJson(contour));
        o["contours"] = contours;
    }
    return o;
}

// The path of a file of the scene from the folder of the scene; empty without a folder.
static QString pathFrom(QString const& folder, QString const& file)
{
    return folder.isEmpty() || file.isEmpty() ? QString() : QDir(folder).relativeFilePath(file);
}

static QJsonArray pairsJson(QVector<QPair<QString, QString>> const& pairs)
{
    QJsonArray array;
    for (QPair<QString, QString> const& pair : pairs)
        array.append(QJsonArray{pair.first, pair.second});
    return array;
}

QJsonObject Scene::toJson(QString const& folder) const
{
    QJsonArray layerArray;
    for (Layer const& layer : layers) {
        QJsonArray drawings;
        for (Drawing const& d : layer.drawings) {
            QJsonArray strokes, fills;
            for (Stroke const& s : d.strokes)
                strokes.append(strokeToJson(s));
            for (Fill const& f : d.fills)
                fills.append(fillToJson(f));
            QJsonObject drawing{{"strokes", strokes}, {"fills", fills}};
            if (!d.colourStrokes.isEmpty() || !d.colourFills.isEmpty()) {
                QJsonArray colourStrokes, colourFills;
                for (Stroke const& s : d.colourStrokes)
                    colourStrokes.append(strokeToJson(s));
                for (Fill const& f : d.colourFills)
                    colourFills.append(fillToJson(f));
                drawing["colourStrokes"] = colourStrokes;
                drawing["colourFills"] = colourFills;
            }
            for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::OverlaySheet)}) {
                if (d.strokesOf(sheet).isEmpty() && d.fillsOf(sheet).isEmpty())
                    continue;
                QJsonArray sheetStrokes, sheetFills;
                for (Stroke const& s : d.strokesOf(sheet))
                    sheetStrokes.append(strokeToJson(s));
                for (Fill const& f : d.fillsOf(sheet))
                    sheetFills.append(fillToJson(f));
                drawing[sheet == Drawing::UnderlaySheet ? "underlayStrokes" : "overlayStrokes"] = sheetStrokes;
                drawing[sheet == Drawing::UnderlaySheet ? "underlayFills" : "overlayFills"] = sheetFills;
            }
            if (!d.pictures.isEmpty()) {
                QJsonArray pictures;
                for (Picture const& p : d.pictures) {
                    QTransform const& t = p.placement;
                    QJsonObject picture{{"file", p.file},
                                        {"width", p.size.width()},
                                        {"height", p.size.height()},
                                        {"placement", QJsonArray{t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()}}};
                    if (p.linked) {
                        picture["linked"] = true;
                        if (QString const relative = pathFrom(folder, p.file); !relative.isEmpty())
                            picture["relative"] = relative;
                    } else {
                        // Kept in the scene file itself, as a PNG.
                        QByteArray bytes;
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::WriteOnly);
                        p.image.save(&buffer, "PNG");
                        picture["png"] = QString::fromLatin1(bytes.toBase64());
                    }
                    pictures.append(picture);
                }
                drawing["pictures"] = pictures;
            }
            if (d.pinned)
                drawing["pinned"] = true;
            if (!d.name.isEmpty())
                drawing["name"] = d.name;
            drawings.append(drawing);
        }
        QJsonArray cells;
        for (int c : layer.cells)
            cells.append(c);
        QJsonArray morphs;
        for (Morph const& morph : layer.morphs)
            morphs.append(QJsonArray{morph.from, morph.easeIn, morph.easeOut});
        QJsonArray keys;
        for (Keyframe const& k : layer.keys) {
            QJsonObject key{{"frame", k.frame}, {"x", k.pose.x}, {"y", k.pose.y}, {"sx", k.pose.scaleX},
                            {"sy", k.pose.scaleY}, {"rot", k.pose.rotation}, {"tween", k.tween},
                            {"ein", k.easeIn}, {"eout", k.easeOut}};
            if (k.pose.opacity != 1.0)
                key["op"] = k.pose.opacity;
            if (k.pose.z != 0.0)
                key["z"] = k.pose.z;
            if (k.pose.rotX != 0.0)
                key["rx"] = k.pose.rotX;
            if (k.pose.rotY != 0.0)
                key["ry"] = k.pose.rotY;
            if (k.pose.fov > 0.0)
                key["fov"] = k.pose.fov;
            if (!k.pose.fx.isEmpty()) {
                QJsonArray fx;
                for (double v : k.pose.fx)
                    fx.append(v);
                key["fx"] = fx;
            }
            if (!k.pose.bend.isEmpty()) {
                QJsonArray bend;
                for (double b : k.pose.bend)
                    bend.append(b);
                key["bend"] = bend;
            }
            if (!k.pose.quad.isEmpty()) {
                QJsonArray pulled;
                for (QPointF const& q : k.pose.quad) {
                    pulled.append(q.x());
                    pulled.append(q.y());
                }
                key["quad"] = pulled;
            }
            if (!k.pose.warp.isEmpty()) {
                QJsonArray warp;
                for (QPointF const& w : k.pose.warp) {
                    warp.append(w.x());
                    warp.append(w.y());
                }
                key["warp"] = warp;
            }
            keys.append(key);
        }
        QJsonArray warp;
        for (QPointF const& w : layer.warp) {
            warp.append(w.x());
            warp.append(w.y());
        }
        QJsonArray bones;
        for (QPointF const& joint : layer.bones) {
            bones.append(joint.x());
            bones.append(joint.y());
        }
        static char const* const kKinds[] = {"drawing", "peg", "camera", "sound", "card", "group", "bone", "quadmap"};
        QJsonArray quad;
        for (QPointF const& corner : layer.quad) {
            quad.append(corner.x());
            quad.append(corner.y());
        }
        QJsonObject layerObject{{"id", layer.id},
                                      {"kind", kKinds[layer.kind]},
                                      {"cardColor", layer.cardColor.name(QColor::HexArgb)},
                                      {"boneLength", layer.boneLength},
                                      {"fov", layer.fov},
                                      {"quad", quad},
                                      {"quadSize", QJsonArray{layer.quadSize.width(), layer.quadSize.height()}},
                                      {"bones", bones},
                                      {"warp", warp},
                                      {"warpClosed", layer.warpClosed},
                                      {"soundFile", layer.soundFile},
                                      {"lipsync", layer.lipsync},
                                      {"soundRelative", pathFrom(folder, layer.soundFile)},
                                      {"soundStart", layer.soundStart},
                                      {"parent", layer.parent},
                                      {"parentBone", layer.parentBone},
                                      {"pivot", QJsonArray{layer.pivot.x(), layer.pivot.y()}},
                                      {"keys", keys},
                                      {"morphs", morphs},
                                      {"name", layer.name},
                                      {"metadata", pairsJson(layer.metadata)},
                                      {"visible", layer.visible},
                                      {"locked", layer.locked},
                                      {"solo", layer.solo},
                                      {"onion", layer.onion},
                                      {"collapsed", layer.collapsed},
                                      {"track", layer.trackColor.isValid() ? layer.trackColor.name() : QString()},
                                      {"opacity", layer.opacity},
                                      {"drawings", drawings},
                                      {"cells", cells}};
        if (!layer.effects.isEmpty()) {
            QJsonArray effects;
            for (Effect const& e : layer.effects)
                effects.append(QJsonObject{{"type", e.type == Effect::Blur ? "blur" : e.type == Effect::Tint ? "tint" : e.type == Effect::Glow ? "glow"
                                                   : e.type == Effect::Shadow ? "shadow" : "cutter"},
                                           {"enabled", e.enabled}, {"amount", e.amount}, {"animated", e.animated},
                                           {"color", e.color.name(QColor::HexArgb)}, {"dx", e.offset.x()}, {"dy", e.offset.y()},
                                           {"matte", e.matte}, {"invert", e.invert}});
            layerObject["effects"] = effects;
        }
        if (layer.nodePlaced)
            layerObject["node"] = QJsonArray{layer.nodePos.x(), layer.nodePos.y()};
        layerArray.append(layerObject);
    }
    QJsonArray paletteArray;
    for (Swatch const& s : palette) {
        QJsonObject swatch{{"id", s.id}, {"name", s.name}, {"color", s.color.name(QColor::HexArgb)}, {"palette", s.palette}};
        if (s.isGradient()) {
            swatch["kind"] = int(s.kind);
            swatch["stops"] = s.stopsJson();
        }
        if (s.isProtected)
            swatch["protected"] = true;
        paletteArray.append(swatch);
    }
    QJsonArray markerArray;
    for (Marker const& m : markers)
        markerArray.append(QJsonObject{{"frame", m.frame}, {"length", m.length}, {"name", m.name}, {"color", m.color.name()}});
    QJsonArray guideArray;
    for (Guide const& guide : guides) {
        if (guide.kind == Guide::Line) {
            guideArray.append(QJsonArray{guide.vertical, guide.at});
            continue;
        }
        QJsonArray points;
        for (QPointF const& point : guide.points)
            points.append(QJsonArray{point.x(), point.y()});
        guideArray.append(QJsonObject{{"kind", int(guide.kind)}, {"points", points}});
    }
    QJsonArray referenceArray;
    for (QString const& file : references)
        referenceArray.append(QJsonObject{{"file", file}, {"relative", pathFrom(folder, file)}});
    return QJsonObject{{"format", "eclipse-scene"},
                       {"version", 3},
                       {"markers", markerArray},
                       {"palette", paletteArray},
                       {"paletteNames", QJsonArray::fromStringList(paletteNames)},
                       {"width", width},
                       {"height", height},
                       {"fps", fps},
                       {"camera", activeCamera},
                       {"references", referenceArray},
                       {"guides", guideArray},
                       {"metadata", pairsJson(metadata)},
                       {"frames", m_frameCount},
                       {"background", background.name()},
                       {"layers", layerArray}};
}

bool Scene::save(QString const& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray const bytes = QJsonDocument(toJson(QFileInfo(path).absolutePath())).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
