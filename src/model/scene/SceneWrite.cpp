// Eclipse - the scene: written to a file.
#include "model/scene/TextureBank.h"
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
    if (s.gradPlaced)
        o["grad"] = QJsonArray{s.gradFrom.x(), s.gradFrom.y(), s.gradTo.x(), s.gradTo.y()};
    if (s.texture != 0) {
        o["texture"] = s.texture;
        o["textureAmount"] = std::round(double(s.textureAmount) * 1000.0) / 1000.0;
        if (s.textureCrisp)
            o["textureCrisp"] = true;
        if (s.textureLow > 0.0f || s.textureHigh < 1.0f)
            o["textureRamp"] = QJsonArray{std::round(double(s.textureLow) * 1000.0) / 1000.0, std::round(double(s.textureHigh) * 1000.0) / 1000.0};
    }
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
    if (s.order != 0)
        o["order"] = s.order;
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
    if (f.order != 0)
        o["order"] = f.order;
    if (f.gradPlaced)
        o["grad"] = QJsonArray{f.gradFrom.x(), f.gradFrom.y(), f.gradTo.x(), f.gradTo.y()};
    if (f.texture != 0) {
        o["texture"] = f.texture;
        o["textureAmount"] = std::round(double(f.textureAmount) * 1000.0) / 1000.0;
        if (f.textureCrisp)
            o["textureCrisp"] = true;
        if (f.textureLow > 0.0f || f.textureHigh < 1.0f)
            o["textureRamp"] = QJsonArray{std::round(double(f.textureLow) * 1000.0) / 1000.0, std::round(double(f.textureHigh) * 1000.0) / 1000.0};
    }
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
        if (!f.textShape.isIdentity())
            o["textShape"] = QJsonArray{f.textShape.m11(), f.textShape.m12(), f.textShape.m21(), f.textShape.m22()};
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
        for (Drawing const& real : layer.drawings) {
            // (The drawings of a layer that shows a symbol are made from the symbol: only their names are kept.)
            Drawing blank;
            blank.name = real.name;
            blank.pinned = true;
            Drawing const& d = layer.showsSymbol() ? blank : real;
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
                    if (p.painted)
                        picture["painted"] = true;
                    if (p.painted && p.sheet != Drawing::LineSheet)
                        picture["sheet"] = p.sheet;
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
            if (d.mark != Drawing::NoMark)
                drawing["mark"] = d.mark;
            drawings.append(drawing);
        }
        QJsonArray cells;
        for (int c : layer.cells)
            cells.append(c);
        QJsonArray morphs;
        for (Morph const& morph : layer.morphs) {
            QJsonArray entry{morph.from, morph.easeIn, morph.easeOut};
            if (!morph.hints.isEmpty() || !morph.velocity.isEmpty()) {
                QJsonArray hints;
                for (QLineF const& hint : morph.hints)
                    hints.append(QJsonArray{hint.x1(), hint.y1(), hint.x2(), hint.y2()});
                entry.append(hints);
            }
            if (!morph.velocity.isEmpty()) {
                QJsonArray curve;
                for (QPointF const& v : morph.velocity)
                    curve.append(QJsonArray{v.x(), v.y()});
                entry.append(curve);
            }
            morphs.append(entry);
        }
        QJsonArray keys;
        for (Keyframe const& k : layer.keys) {
            QJsonObject key{{"frame", k.frame}, {"x", k.pose.x}, {"y", k.pose.y}, {"sx", k.pose.scaleX},
                            {"sy", k.pose.scaleY}, {"rot", k.pose.rotation}, {"tween", k.tween},
                            {"ein", k.easeIn}, {"eout", k.easeOut}};
            if (k.liftIn != 0.0)
                key["lin"] = k.liftIn;
            if (k.parts != Keyframe::AllParts)
                key["parts"] = k.parts;
            if (!k.controls.isEmpty()) {
                QJsonArray controls;
                for (QPointF const& c : k.controls) {
                    controls.append(c.x());
                    controls.append(c.y());
                }
                key["controls"] = controls;
                if (!k.controlsZ.isEmpty()) {
                    QJsonArray depths;
                    for (double z : k.controlsZ)
                        depths.append(z);
                    key["controlsZ"] = depths;
                }
                key["tcb"] = QJsonArray{k.tension, k.continuity, k.bias};
            }
            if (k.liftOut != 0.0)
                key["lout"] = k.liftOut;
            if (k.pose.opacity != 1.0)
                key["op"] = k.pose.opacity;
            if (k.pose.z != 0.0)
                key["z"] = k.pose.z;
            if (k.pose.skew != 0.0)
                key["skew"] = k.pose.skew;
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
                                      {"tagged", layer.tagged},
                                      {"collapsed", layer.collapsed},
                                      {"track", layer.trackColor.isValid() ? layer.trackColor.name() : QString()},
                                      {"opacity", layer.opacity},
                                      {"drawings", drawings},
                                      {"cells", cells}};
        if (!layer.effects.isEmpty()) {
            QJsonArray effects;
            for (Effect const& e : layer.effects) {
                QJsonObject one{{"type", e.type == Effect::Blur ? "blur" : e.type == Effect::Tint ? "tint" : e.type == Effect::Glow ? "glow"
                                         : e.type == Effect::Shadow ? "shadow" : e.type == Effect::Particles ? "particles"
                                         : e.type == Effect::Transparency ? "transparency" : e.type == Effect::DirectionalBlur ? "directionalBlur"
                                         : e.type == Effect::HueSaturation ? "hueSaturation" : e.type == Effect::BrightnessContrast ? "brightnessContrast"
                                         : e.type == Effect::Grain ? "grain" : e.type == Effect::RadialBlur ? "radialBlur" : e.type == Effect::ColourScale ? "colourScale"
                                         : e.type == Effect::Negate ? "negate" : e.type == Effect::Median ? "median" : e.type == Effect::Ripple ? "ripple"
                                         : "cutter"},
                                {"enabled", e.enabled}, {"amount", e.amount}, {"animated", e.animated},
                                {"color", e.color.name(QColor::HexArgb)}, {"dx", e.offset.x()}, {"dy", e.offset.y()},
                                {"matte", e.matte}, {"invert", e.invert}};
                if (e.type == Effect::DirectionalBlur || e.type == Effect::Ripple)
                    one["direction"] = e.direction;
                if (e.type == Effect::Grain)
                    one["seed"] = e.seed;
                if (e.type == Effect::HueSaturation) {
                    one["hue"] = e.hue;
                    one["saturation"] = e.saturation;
                    one["lightness"] = e.lightness;
                }
                if (e.type == Effect::BrightnessContrast) {
                    one["brightness"] = e.brightness;
                    one["contrast"] = e.contrast;
                }
                if (e.type == Effect::Particles) {
                    one["life"] = e.life;
                    one["speed"] = e.speed;
                    one["spread"] = e.spread;
                    one["direction"] = e.direction;
                    one["gravity"] = e.gravity;
                    one["seed"] = e.seed;
                    one["endSize"] = e.endSize;
                    one["follow"] = e.followLayer;
                    if (e.endColor.isValid())
                        one["endColor"] = e.endColor.name(QColor::HexArgb);
                    one["wind"] = e.wind;
                    one["spin"] = e.spin;
                    one["regionW"] = e.regionWidth;
                    one["regionH"] = e.regionHeight;
                    one["regionShape"] = e.regionShape;
                    one["floor"] = e.floor;
                    one["bounce"] = e.bounce;
                    one["walls"] = e.walls;
                }
                effects.append(one);
            }
            layerObject["effects"] = effects;
        }
        if (layer.nodePlaced)
            layerObject["node"] = QJsonArray{layer.nodePos.x(), layer.nodePos.y()};
        if (!layer.frameMarks.isEmpty()) {
            QJsonArray marks;
            for (auto it = layer.frameMarks.constBegin(); it != layer.frameMarks.constEnd(); ++it)
                marks.append(QJsonArray{it.key(), it.value().name()});
            layerObject["frameMarks"] = marks;
        }
        if (!layer.keyExposures.isEmpty()) {
            QJsonArray marks;
            for (int frame : layer.keyExposures)
                marks.append(frame);
            layerObject["keyExposures"] = marks;
        }
        if (layer.showsSymbol())
            layerObject["symbol"] = layer.symbolId;
        if (layer.syncGroup != 0)
            layerObject["sync"] = layer.syncGroup;
        if (layer.cloneGroup != 0)
            layerObject["clone"] = layer.cloneGroup;
        if (layer.cloneGroup != 0 && !layer.cloneTiming)
            layerObject["cloneTiming"] = false;
        if (layer.bitmap)
            layerObject["bitmap"] = true;
        if (layer.bitmapArt != 0)
            layerObject["bitmapArt"] = layer.bitmapArt;
        if (layer.bendOnBones)
            layerObject["bendOnBones"] = true;
        if (layer.bitmapResolution != 100)
            layerObject["bitmapResolution"] = layer.bitmapResolution;
        if (layer.hiddenSheets != 0)
            layerObject["hiddenSheets"] = layer.hiddenSheets;
        if (layer.ikExcluded)
            layerObject["ikExcluded"] = true;
        if (layer.ikLimited)
            layerObject["ikLimits"] = QJsonArray{layer.ikMin, layer.ikMax};
        layerArray.append(layerObject);
    }
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
        paletteArray.append(swatch);
    }
    QJsonArray markerArray;
    for (Marker const& m : markers) {
        QJsonObject marker{{"frame", m.frame}, {"length", m.length}, {"name", m.name}, {"color", m.color.name()}};
        if (!m.note.isEmpty())
            marker["note"] = m.note;
        markerArray.append(marker);
    }
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
        controllerArray.append(controller);
    }
    QJsonArray writeArray;
    for (Write const& w : writes)
        writeArray.append(QJsonObject{{"name", w.name}, {"movie", w.movie}, {"path", w.path}, {"format", w.format},
                                      {"percent", w.percent}, {"enabled", w.enabled}});
    QJsonObject whole{{"format", "eclipse-scene"},
                       {"controllers", controllerArray},
                       {"writes", writeArray},
                       {"symbols", symbolArray},
                       {"version", 3},
                       {"markers", markerArray},
                       {"palette", paletteArray},
                       {"paletteNames", QJsonArray::fromStringList(paletteNames)},
                       {"paletteLinks", QJsonArray::fromStringList(paletteLinks)},
                       {"width", width},
                       {"height", height},
                       {"fps", fps},
                       {"orthographic", orthographic},
                       {"fov", fov},
                       {"camera", activeCamera},
                       {"display", display},
                       {"references", referenceArray},
                       {"guides", guideArray},
                       {"metadata", pairsJson(metadata)},
                       {"frames", m_frameCount},
                       {"background", background.name()},
                       {"layers", layerArray}};
    if (!textureArray.isEmpty())
        whole["textures"] = textureArray;
    return whole;
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
    // The palettes linked to a file of their own are written there too, for the other scenes linked to it.
    for (int i = 0; i < paletteLinks.size() && i < paletteNames.size(); ++i)
        if (!paletteLinks[i].isEmpty())
            writePaletteFile(i, paletteLinks[i]);
    return true;
}
