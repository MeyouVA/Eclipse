// Eclipse - the scene: read from a file.
#include "model/scene/Scene.h"

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

static Stroke strokeFromJson(QJsonObject const& o);

static Fill fillFromJson(QJsonObject const& o)
{
    Fill f;
    f.color = QColor(o["color"].toString("#ff000000"));
    f.swatch = o["swatch"].toInt(-1);
    f.group = o["group"].toInt(0);
    if (QJsonArray const grad = o["grad"].toArray(); grad.size() == 4) {
        f.gradPlaced = true;
        f.gradFrom = QPointF(grad[0].toDouble(), grad[1].toDouble());
        f.gradTo = QPointF(grad[2].toDouble(), grad[3].toDouble());
    }
    f.text = o["text"].toString();
    f.textAlign = std::clamp(o["textAlign"].toInt(0), 0, 3);
    f.textSpacing = std::clamp(o["textSpacing"].toDouble(1.0), 0.2, 10.0);
    f.textKerning = std::clamp(o["textKerning"].toInt(100), 50, 400);
    f.textFont = o["textFont"].toString();
    f.textAt = QPointF(o["textAt"].toArray().at(0).toDouble(), o["textAt"].toArray().at(1).toDouble());
    if (QJsonArray const shape = o["textShape"].toArray(); shape.size() == 4)
        f.textShape = QTransform(shape[0].toDouble(1.0), shape[1].toDouble(), shape[2].toDouble(), shape[3].toDouble(1.0), 0.0, 0.0);
    for (QJsonValue const& ov : o["outlines"].toArray()) {
        QJsonArray const pts = ov.toArray();
        QPolygonF poly;
        for (int i = 0; i + 1 < pts.size(); i += 2)
            poly.push_back(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
        if (poly.size() >= 3)
            f.outlines.push_back(poly);
    }
    for (QJsonValue const& cv : o["contours"].toArray()) {
        Stroke contour = strokeFromJson(cv.toObject());
        contour.closed = true;
        if (contour.points.size() >= 3)
            f.contours.push_back(contour);
    }
    f.rebuild();
    return f;
}

static Stroke strokeFromJson(QJsonObject const& o)
{
    Stroke s;
    s.color = QColor(o["color"].toString("#ff000000"));
    s.swatch = o["swatch"].toInt(-1);
    s.width = float(o["width"].toDouble(4.0));
    QString const join = o["join"].toString();
    s.join = join == QLatin1String("mitre") ? Stroke::MitreJoin : join == QLatin1String("bevel") ? Stroke::BevelJoin : Stroke::RoundJoin;
    // ("cap" was both ends at once, for a short while.)
    bool const both = o["cap"].toString() == QLatin1String("flat");
    s.capStart = both || o["capStart"].toString() == QLatin1String("flat") ? Stroke::FlatCap : Stroke::RoundCap;
    s.capEnd = both || o["capEnd"].toString() == QLatin1String("flat") ? Stroke::FlatCap : Stroke::RoundCap;
    s.curve = o["curve"].toBool(false);
    s.group = o["group"].toInt(0);
    s.closed = o["closed"].toBool(false);
    if (QJsonArray const grad = o["grad"].toArray(); grad.size() == 4) {
        s.gradPlaced = true;
        s.gradFrom = QPointF(grad[0].toDouble(), grad[1].toDouble());
        s.gradTo = QPointF(grad[2].toDouble(), grad[3].toDouble());
    }
    QJsonArray const pts = o["points"].toArray();
    for (int i = 0; i + 2 < pts.size(); i += 3)
        s.points.push_back({QPointF(pts[i].toDouble(), pts[i + 1].toDouble()), float(pts[i + 2].toDouble(1.0))});
    for (QJsonValue const& c : o["corners"].toArray())
        if (c.toInt(-1) >= 0 && c.toInt() < s.points.size())
            s.points[c.toInt()].corner = true;
    QJsonArray const handles = o["handles"].toArray();
    for (int i = 0; i + 4 < handles.size(); i += 5) {
        int const index = handles[i].toInt(-1);
        if (index < 0 || index >= s.points.size())
            continue;
        s.points[index].handles = true;
        s.points[index].in = QPointF(handles[i + 1].toDouble(), handles[i + 2].toDouble());
        s.points[index].out = QPointF(handles[i + 3].toDouble(), handles[i + 4].toDouble());
    }
    return s;
}

// Where a file of the scene is now: where it was, or - when it is not there any more - at
// the same place from the folder of the scene.
static QString found(QString const& folder, QString const& file, QString const& relative)
{
    if (folder.isEmpty() || relative.isEmpty() || QFileInfo::exists(file))
        return file;
    QString const beside = QDir::cleanPath(QDir(folder).absoluteFilePath(relative));
    return QFileInfo::exists(beside) ? beside : file;
}

static QVector<QPair<QString, QString>> pairsFromJson(QJsonArray const& array)
{
    QVector<QPair<QString, QString>> pairs;
    for (QJsonValue const& value : array)
        if (value.toArray().size() == 2 && !value.toArray().at(0).toString().isEmpty())
            pairs.push_back({value.toArray().at(0).toString(), value.toArray().at(1).toString()});
    return pairs;
}

bool Scene::fromJson(QJsonObject const& json, Scene& out, QString* error, QString const& folder)
{
    if (json["format"].toString() != QLatin1String("eclipse-scene")) {
        if (error)
            *error = QStringLiteral("Not an Eclipse scene file.");
        return false;
    }
    if (json["version"].toInt() > 3) {
        if (error)
            *error = QStringLiteral("This scene was saved by a newer version of Eclipse.");
        return false;
    }
    Scene scene;
    scene.layers.clear();
    scene.width = std::max(1, json["width"].toInt(1920));
    scene.height = std::max(1, json["height"].toInt(1080));
    scene.fps = std::clamp(json["fps"].toDouble(24.0), 1.0, 240.0);
    scene.orthographic = json["orthographic"].toBool(false);
    scene.activeCamera = json["camera"].toInt(0);
    for (QJsonValue const& gv : json["guides"].toArray()) {
        if (gv.isArray() && gv.toArray().size() == 2) {
            scene.guides.push_back({gv.toArray().at(0).toBool(), gv.toArray().at(1).toDouble()});
        } else if (gv.isObject()) {
            // A guide to draw along: its kind and its points.
            QJsonObject const g = gv.toObject();
            Guide guide;
            guide.kind = Guide::Kind(std::clamp(g["kind"].toInt(1), 1, int(Guide::kLastKind)));
            for (QJsonValue const& pv : g["points"].toArray())
                guide.points.push_back(QPointF(pv.toArray().at(0).toDouble(), pv.toArray().at(1).toDouble()));
            scene.guides.push_back(guide);
        }
    }
    scene.metadata = pairsFromJson(json["metadata"].toArray());
    for (QJsonValue const& rv : json["references"].toArray())
        scene.references << found(folder, rv.toObject()["file"].toString(), rv.toObject()["relative"].toString());
    scene.m_frameCount = std::max(1, json["frames"].toInt(24));
    scene.background = QColor(json["background"].toString("#ffffff"));
    for (QJsonValue const& mv : json["markers"].toArray()) {
        QJsonObject const mo = mv.toObject();
        Marker m;
        m.frame = std::max(0, mo["frame"].toInt());
        m.length = std::max(1, mo["length"].toInt(1));
        m.name = mo["name"].toString();
        m.note = mo["note"].toString();
        if (QColor const c(mo["color"].toString()); c.isValid())
            m.color = c;
        scene.markers.push_back(m);
    }
    // A version 1 file has no palette: it keeps the default one, and its strokes their own colours.
    if (json.contains("paletteNames")) {
        QStringList names;
        for (QJsonValue const& nv : json["paletteNames"].toArray())
            names << nv.toString();
        if (!names.isEmpty())
            scene.paletteNames = names;
        for (QJsonValue const& lv : json["paletteLinks"].toArray())
            scene.paletteLinks.push_back(lv.toString());
        while (scene.paletteLinks.size() > scene.paletteNames.size())
            scene.paletteLinks.removeLast();
    }
    if (json.contains("palette")) {
        scene.palette.clear();
        scene.m_nextSwatchId = 1;
        for (QJsonValue const& sv : json["palette"].toArray()) {
            QJsonObject const so = sv.toObject();
            Swatch s;
            s.id = so["id"].toInt();
            s.name = so["name"].toString();
            s.color = QColor(so["color"].toString("#ff000000"));
            s.palette = std::clamp(so["palette"].toInt(0), 0, int(scene.paletteNames.size()) - 1);
            s.isProtected = so["protected"].toBool(false);
            s.setStopsJson(so["stops"].toArray());
            s.kind = s.stops.size() >= 2 ? Swatch::Kind(std::clamp(so["kind"].toInt(0), 0, 2)) : Swatch::Solid;
            if (s.kind == Swatch::Solid)
                s.stops.clear();
            s.setTextureText(so["texture"].toString());
            if (s.id > 0 && !scene.findSwatch(s.id)) {
                scene.palette.push_back(s);
                scene.m_nextSwatchId = std::max(scene.m_nextSwatchId, s.id + 1);
            }
        }
    }
    for (QJsonValue const& lv : json["layers"].toArray()) {
        QJsonObject const lo = lv.toObject();
        Layer layer;
        layer.name = lo["name"].toString("Layer");
        layer.metadata = pairsFromJson(lo["metadata"].toArray());
        layer.visible = lo["visible"].toBool(true);
        layer.locked = lo["locked"].toBool(false);
        layer.solo = lo["solo"].toBool(false);
        layer.onion = lo["onion"].toBool(false);
        layer.tagged = lo["tagged"].toBool(false);
        for (QJsonValue const& mv : lo["frameMarks"].toArray()) {
            QColor const colour(mv.toArray().at(1).toString());
            if (colour.isValid() && mv.toArray().at(0).toInt(-1) >= 0)
                layer.frameMarks.insert(mv.toArray().at(0).toInt(), colour);
        }
        for (QJsonValue const& kv : lo["keyExposures"].toArray())
            layer.keyExposures.push_back(kv.toInt());
        layer.symbolId = lo["symbol"].toInt(0);
        layer.collapsed = lo["collapsed"].toBool(false);
        if (QString const track = lo["track"].toString(); !track.isEmpty())
            layer.trackColor = QColor(track);
        layer.opacity = std::clamp(lo["opacity"].toDouble(1.0), 0.0, 1.0);
        for (QJsonValue const& dv : lo["drawings"].toArray()) {
            Drawing d;
            // Version 1 stored a drawing as a bare array of strokes.
            QJsonArray const strokes = dv.isArray() ? dv.toArray() : dv.toObject()["strokes"].toArray();
            for (QJsonValue const& sv : strokes)
                d.strokes.push_back(strokeFromJson(sv.toObject()));
            for (QJsonValue const& fv : dv.toObject()["fills"].toArray())
                d.fills.push_back(fillFromJson(fv.toObject()));
            d.pinned = dv.toObject()["pinned"].toBool(false);
            d.name = dv.toObject()["name"].toString();
            d.mark = std::clamp(dv.toObject()["mark"].toInt(0), 0, int(Drawing::InBetweenMark));
            for (QJsonValue const& pv : dv.toObject()["pictures"].toArray()) {
                QJsonObject const po = pv.toObject();
                Picture p;
                p.file = found(folder, po["file"].toString(), po["relative"].toString());
                p.linked = po["linked"].toBool(false);
                p.size = QSize(po["width"].toInt(), po["height"].toInt());
                QJsonArray const m = po["placement"].toArray();
                if (m.size() == 9)
                    p.placement = QTransform(m[0].toDouble(), m[1].toDouble(), m[2].toDouble(), m[3].toDouble(), m[4].toDouble(), m[5].toDouble(),
                                             m[6].toDouble(), m[7].toDouble(), m[8].toDouble());
                if (p.linked)
                    p.image = QImage(p.file);       // null when the file has gone: the place is kept
                else
                    p.image.loadFromData(QByteArray::fromBase64(po["png"].toString().toLatin1()), "PNG");
                if (!p.image.isNull() && p.size.isEmpty())
                    p.size = p.image.size();
                if (!p.size.isEmpty())
                    d.pictures.push_back(p);
            }
            for (QJsonValue const& sv : dv.toObject()["colourStrokes"].toArray())
                d.colourStrokes.push_back(strokeFromJson(sv.toObject()));
            for (QJsonValue const& fv : dv.toObject()["colourFills"].toArray())
                d.colourFills.push_back(fillFromJson(fv.toObject()));
            for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::OverlaySheet)}) {
                for (QJsonValue const& sv : dv.toObject()[sheet == Drawing::UnderlaySheet ? "underlayStrokes" : "overlayStrokes"].toArray())
                    d.strokesOf(sheet).push_back(strokeFromJson(sv.toObject()));
                for (QJsonValue const& fv : dv.toObject()[sheet == Drawing::UnderlaySheet ? "underlayFills" : "overlayFills"].toArray())
                    d.fillsOf(sheet).push_back(fillFromJson(fv.toObject()));
            }
            layer.drawings.push_back(d);
        }
        for (QJsonValue const& cv : lo["cells"].toArray()) {
            int const c = cv.toInt(Layer::kNoDrawing);
            layer.cells.push_back(c >= 0 && c < layer.drawings.size() ? c : Layer::kNoDrawing);
        }
        layer.cells.resize(scene.m_frameCount, Layer::kNoDrawing);
        layer.tidyKeyExposures();

        // Version 3: pegs, parents and keyframes.  Older files have none of it.
        layer.id = lo["id"].toInt(0);
        QString const kind = lo["kind"].toString();
        layer.kind = kind == QLatin1String("peg") ? Layer::Peg : kind == QLatin1String("camera") ? Layer::Camera
                     : kind == QLatin1String("sound") ? Layer::Sound : kind == QLatin1String("card") ? Layer::ColourCard
                     : kind == QLatin1String("group") ? Layer::Group : kind == QLatin1String("bone") ? Layer::Bone
                     : kind == QLatin1String("quadmap") ? Layer::Quadmap : Layer::DrawingLayer;
        if (QString const card = lo["cardColor"].toString(); !card.isEmpty())
            layer.cardColor = QColor(card);
        layer.boneLength = std::max(1.0, lo["boneLength"].toDouble(200.0));
        layer.fov = std::clamp(lo["fov"].toDouble(Layer::kDefaultFov), 1.0, 170.0);
        QJsonArray const quad = lo["quad"].toArray();
        for (int i = 0; i + 1 < quad.size(); i += 2)
            layer.quad.push_back(QPointF(quad[i].toDouble(), quad[i + 1].toDouble()));
        QJsonArray const quadSize = lo["quadSize"].toArray();
        if (quadSize.size() == 2)
            layer.quadSize = QSizeF(quadSize[0].toDouble(), quadSize[1].toDouble());
        layer.soundFile = found(folder, lo["soundFile"].toString(), lo["soundRelative"].toString());
        QJsonArray const warp = lo["warp"].toArray();
        for (int i = 0; i + 1 < warp.size(); i += 2)
            layer.warp.push_back(QPointF(warp[i].toDouble(), warp[i + 1].toDouble()));
        layer.warp.resize(layer.warp.size() / 3 * 3);
        layer.warpClosed = lo["warpClosed"].toBool(false);
        QJsonArray const bones = lo["bones"].toArray();
        for (int i = 0; i + 1 < bones.size(); i += 2)
            layer.bones.push_back(QPointF(bones[i].toDouble(), bones[i + 1].toDouble()));
        layer.soundStart = lo["soundStart"].toInt(0);
        for (QChar const shape : lo["lipsync"].toString())
            layer.lipsync += QStringLiteral("ABCDEFGX").contains(shape) ? shape : QLatin1Char('X');
        layer.parent = lo["parent"].toInt(0);
        layer.parentBone = lo["parentBone"].toInt(-1);
        for (QJsonValue const& ev : lo["effects"].toArray()) {
            QJsonObject const eo = ev.toObject();
            QString const type = eo["type"].toString();
            Effect e;
            if (type == QLatin1String("blur"))
                e.type = Effect::Blur;
            else if (type == QLatin1String("tint"))
                e.type = Effect::Tint;
            else if (type == QLatin1String("cutter"))
                e.type = Effect::Cutter;
            else if (type == QLatin1String("glow"))
                e.type = Effect::Glow;
            else if (type == QLatin1String("shadow"))
                e.type = Effect::Shadow;
            else
                continue;  // an effect of a newer version: left out rather than guessed at
            e.enabled = eo["enabled"].toBool(true);
            e.amount = std::max(0.0, eo["amount"].toDouble());
            e.animated = eo["animated"].toBool(false);
            e.offset = QPointF(eo["dx"].toDouble(), eo["dy"].toDouble());
            e.color = QColor(eo["color"].toString("#ff000000"));
            e.matte = eo["matte"].toInt(0);
            e.invert = eo["invert"].toBool(false);
            layer.effects.push_back(e);
        }
        QJsonArray const node = lo["node"].toArray();
        layer.nodePlaced = node.size() == 2;
        if (layer.nodePlaced)
            layer.nodePos = QPointF(node[0].toDouble(), node[1].toDouble());
        QJsonArray const pivot = lo["pivot"].toArray();
        layer.pivot = pivot.size() == 2 ? QPointF(pivot[0].toDouble(), pivot[1].toDouble())
                                        : QPointF(scene.width / 2.0, scene.height / 2.0);
        // A scale of nothing could not be undone with the transform tool.
        auto scale = [](double v) { return std::abs(v) < 0.001 ? (v < 0 ? -0.001 : 0.001) : v; };
        for (QJsonValue const& mv : lo["morphs"].toArray()) {
            QJsonArray const values = mv.toArray();
            Morph morph;
            morph.from = values.at(0).toInt(-1);
            morph.easeIn = std::clamp(values.at(1).toDouble(0.0), 0.0, 1.0);
            morph.easeOut = std::clamp(values.at(2).toDouble(0.0), 0.0, 1.0);
            for (QJsonValue const& hv : values.at(3).toArray()) {
                QJsonArray const hint = hv.toArray();
                if (hint.size() == 4)
                    morph.hints.push_back(QLineF(hint[0].toDouble(), hint[1].toDouble(), hint[2].toDouble(), hint[3].toDouble()));
            }
            if (morph.from >= 0)
                layer.morphs.push_back(morph);
        }
        for (QJsonValue const& kv : lo["keys"].toArray()) {
            QJsonObject const ko = kv.toObject();
            int const frame = ko["frame"].toInt(-1);
            if (frame < 0 || frame >= scene.m_frameCount)
                continue;
            Pose pose;
            pose.x = ko["x"].toDouble();
            pose.y = ko["y"].toDouble();
            pose.scaleX = scale(ko["sx"].toDouble(1.0));
            pose.scaleY = scale(ko["sy"].toDouble(1.0));
            pose.rotation = ko["rot"].toDouble();
            pose.skew = ko["skew"].toDouble(0.0);
            pose.z = ko["z"].toDouble(0.0);
            pose.rotX = ko["rx"].toDouble(0.0);
            pose.rotY = ko["ry"].toDouble(0.0);
            pose.fov = ko["fov"].toDouble(0.0) > 0.0 ? std::clamp(ko["fov"].toDouble(), 1.0, 170.0) : 0.0;
            pose.opacity = std::clamp(ko["op"].toDouble(1.0), 0.0, 1.0);
            for (QJsonValue const& v : ko["fx"].toArray())
                pose.fx.push_back(std::max(0.0, v.toDouble()));
            for (QJsonValue const& b : ko["bend"].toArray())
                pose.bend.push_back(b.toDouble());
            {
                QJsonArray const pulled = ko["quad"].toArray();
                for (int i = 0; i + 1 < pulled.size() && i < 8; i += 2)
                    pose.quad.push_back(QPointF(pulled[i].toDouble(), pulled[i + 1].toDouble()));
                QJsonArray const moved = ko["warp"].toArray();
                for (int i = 0; i + 1 < moved.size(); i += 2)
                    pose.warp.push_back(QPointF(moved[i].toDouble(), moved[i + 1].toDouble()));
            }
            layer.setKey(frame, pose);
            Keyframe& key = layer.keys[layer.keyIndex(frame)];
            key.tween = ko["tween"].toBool(true);
            key.easeIn = std::clamp(ko["ein"].toDouble(0.0), 0.0, 1.0);
            key.easeOut = std::clamp(ko["eout"].toDouble(0.0), 0.0, 1.0);
            key.liftIn = std::clamp(ko["lin"].toDouble(0.0), -2.0, 2.0);
            key.liftOut = std::clamp(ko["lout"].toDouble(0.0), -2.0, 2.0);
        }
        if (!layer.holdsDrawings()) {
            layer.drawings.clear();
            layer.cells.fill(Layer::kNoDrawing);
        }
        scene.layers.push_back(layer);
    }
    // Ids: keep those of the file where they are usable, hand out new ones for the rest.
    scene.m_nextLayerId = 1;
    for (int i = 0; i < scene.layers.size(); ++i) {
        Layer& layer = scene.layers[i];
        bool taken = layer.id <= 0;
        for (int j = 0; j < i && !taken; ++j)
            taken = scene.layers[j].id == layer.id;
        if (taken)
            layer.id = 0;
        scene.m_nextLayerId = std::max(scene.m_nextLayerId, layer.id + 1);
    }
    for (Layer& layer : scene.layers)
        if (layer.id == 0)
            layer.id = scene.m_nextLayerId++;
    // Parents: drop the ones that do not exist or that would go round in a circle.
    for (int i = 0; i < scene.layers.size(); ++i) {
        int const parent = scene.layerIndex(scene.layers[i].parent);
        int const bone = scene.layers[i].parentBone;
        scene.layers[i].parent = 0;
        scene.layers[i].parentBone = -1;
        if (parent >= 0 && scene.setParent(i, parent))
            scene.layers[i].parentBone = bone >= 0 && bone + 1 < scene.layers[parent].bones.size() ? bone : -1;
    }
    if (scene.layers.isEmpty())
        scene.addLayer(QStringLiteral("Drawing"));
    // The symbols, each a scene of its own; then the layers that show them get their drawings.
    for (QJsonValue const& sv : json["symbols"].toArray()) {
        QJsonObject const so = sv.toObject();
        Scene inner;
        QString why;
        if (so["id"].toInt(0) <= 0 || scene.findSymbol(so["id"].toInt()) || !fromJson(so["scene"].toObject(), inner, &why, folder))
            continue;
        Symbol symbol;
        symbol.id = so["id"].toInt();
        symbol.name = so["name"].toString();
        symbol.layers = inner.layers;
        symbol.frameCount = inner.frameCount();
        scene.symbols.push_back(symbol);
        scene.m_nextLayerId = std::max(scene.m_nextLayerId, inner.m_nextLayerId);
    }
    for (Layer& layer : scene.layers)
        if (layer.showsSymbol() && !scene.findSymbol(layer.symbolId))
            layer.symbolId = 0;         // a symbol that is not there: a plain layer again
    scene.refreshSymbolInstances();
    out = scene;
    return true;
}

bool Scene::load(QString const& path, Scene& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (doc.isNull()) {
        if (error)
            *error = parse.errorString();
        return false;
    }
    if (!fromJson(doc.object(), out, error, QFileInfo(path).absolutePath()))
        return false;
    // The palettes linked to a file take the colours the file has now (another scene may have changed them).
    for (int i = 0; i < out.paletteLinks.size() && i < out.paletteNames.size(); ++i)
        if (!out.paletteLinks[i].isEmpty() && QFileInfo::exists(out.paletteLinks[i]))
            out.readPaletteFile(i, out.paletteLinks[i]);
    return true;
}
