// Eclipse - the scene: read from a file.
#include "model/scene/TextureBank.h"
#include "model/scene/Scene.h"
#include "model/scene/SceneProject.h"
#include "model/scene/SceneWriteParts.h"

#include <QFileInfo>
#include <QDir>

#include "model/geometry/Deform.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "model/scene/SceneReadParts.h"

using namespace scene_read;

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
    scene.soloMode = json["soloMode"].toBool(true);
    scene.colourManaged = json["colourManaged"].toBool(false);
    scene.workingSpace = json["workingSpace"].toString(QStringLiteral("sRGB"));
    scene.textureSpace = json["textureSpace"].toString(QStringLiteral("sRGB"));
    scene.palettesInSrgb = json["palettesInSrgb"].toBool(true);
    scene.bitmapResolution = std::clamp(json["bitmapResolution"].toInt(100), 25, 400);
    scene.framesPerBeat = std::clamp(json["framesPerBeat"].toInt(0), 0, 1000);
    scene.beatsPerBar = std::clamp(json["beatsPerBar"].toInt(0), 0, 1000);
    for (QJsonValue const& mv : json["sheetMarkers"].toArray()) {
        QJsonObject const mo = mv.toObject();
        SheetMarker m;
        m.layer = mo["layer"].toInt(0);
        m.frame = std::max(0, mo["frame"].toInt(0));
        m.length = std::max(1, mo["length"].toInt(1));
        m.colour = std::clamp(mo["colour"].toInt(0), 0, 2);
        scene.sheetMarkers.push_back(m);
    }
    scene.fovAcross = json["fovAcross"].toBool(false);
    scene.fov = std::clamp(json["fov"].toDouble(Layer::kDefaultFov), Layer::kMinFov, Layer::kMaxFov);
    scene.activeCamera = json["camera"].toInt(0);
    scene.display = json["display"].toInt(0);
    for (QJsonValue const& gv : json["guides"].toArray()) {
        Guide guide;
        if (guideFromJson(gv, &guide))
            scene.guides.push_back(guide);
    }
    scene.metadata = pairsFromJson(json["metadata"].toArray());
    for (QJsonValue const& rv : json["references"].toArray())
        scene.references << found(folder, rv.toObject()["file"].toString(), rv.toObject()["relative"].toString());
    for (QJsonValue const& dv : json["desk"].toArray()) {
        QJsonObject const d = dv.toObject();
        scene.desk.push_back({d["layer"].toInt(), d["drawing"].toInt(), d["shown"].toBool(true)});
    }
    scene.m_frameCount = std::max(1, json["frames"].toInt(24));
    scene.background = QColor(json["background"].toString("#ffffff"));
    scene.uiState = json["uiState"].toObject();
    for (QJsonValue const& wv : json["writes"].toArray()) {
        QJsonObject const wo = wv.toObject();
        Write w;
        w.name = wo["name"].toString();
        w.movie = wo["movie"].toBool(false);
        w.path = wo["path"].toString();
        w.format = wo["format"].toString(QStringLiteral("png"));
        w.percent = std::clamp(wo["percent"].toInt(100), 1, 1000);
        w.enabled = wo["enabled"].toBool(true);
        w.layers = wo["layers"].toBool(false);
        w.colourSpace = wo["colourSpace"].toString();
        w.keepFrames = wo["keepFrames"].toBool(false);
        scene.writes.push_back(w);
    }
    for (QJsonValue const& cv : json["controllers"].toArray()) {
        QJsonObject const co = cv.toObject();
        Controller c;
        c.id = co["id"].toInt();
        c.name = co["name"].toString();
        c.value = co["value"].toDouble(0.0);
        c.point = co["point"].toBool(false);
        for (QJsonValue const& kv : co["keys"].toArray())
            if (QJsonArray const k = kv.toArray(); k.size() == 2)
                c.keys.insert(k[0].toInt(), k[1].toDouble());
        for (QJsonValue const& kv : co["pointKeys"].toArray())
            if (QJsonArray const k = kv.toArray(); k.size() == 3)
                c.pointKeys.insert(k[0].toInt(), QPointF(k[1].toDouble(), k[2].toDouble()));
        if (QJsonArray const where = co["where"].toArray(); where.size() == 2)
            c.where = QPointF(where[0].toDouble(), where[1].toDouble());
        for (QJsonValue const& id : co["layers"].toArray())
            c.layers.push_back(id.toInt());
        for (QJsonValue const& sv : co["states"].toArray()) {
            QJsonObject const so = sv.toObject();
            Controller::State s;
            s.name = so["name"].toString();
            for (QJsonValue const& pv : so["poses"].toArray()) {
                QJsonObject const po = pv.toObject();
                Pose p;
                p.x = po["x"].toDouble();
                p.y = po["y"].toDouble();
                p.scaleX = po["sx"].toDouble(1.0);
                p.scaleY = po["sy"].toDouble(1.0);
                p.rotation = po["rot"].toDouble();
                p.skew = po["skew"].toDouble();
                p.z = po["z"].toDouble();
                p.opacity = std::clamp(po["op"].toDouble(1.0), 0.0, 1.0);
                for (QJsonValue const& b : po["bend"].toArray())
                    p.bend.push_back(b.toDouble());
                p.rotX = po["rx"].toDouble();
                p.rotY = po["ry"].toDouble();
                p.fov = po["fov"].toDouble();
                for (QJsonValue const& f : po["fx"].toArray())
                    p.fx.push_back(f.toDouble());
                QJsonArray const warp = po["warp"].toArray(), quad = po["quad"].toArray();
                for (int i = 0; i + 1 < warp.size(); i += 2)
                    p.warp.push_back(QPointF(warp[i].toDouble(), warp[i + 1].toDouble()));
                for (int i = 0; i + 1 < quad.size(); i += 2)
                    p.quad.push_back(QPointF(quad[i].toDouble(), quad[i + 1].toDouble()));
                s.poses.push_back(p);
            }
            for (QJsonValue const& d : so["drawings"].toArray())
                s.drawings.push_back(d.toInt(-1));
            if (QJsonArray const place = so["at"].toArray(); place.size() == 2)
                s.at = QPointF(place[0].toDouble(), place[1].toDouble());
            if (s.poses.size() == c.layers.size() && s.drawings.size() == c.layers.size())
                c.states.push_back(s);
        }
        if (c.id > 0)
            scene.controllers.push_back(c);
    }
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
        for (QJsonValue const& ov : json["paletteOwners"].toArray())
            if (scene.paletteOwners.size() < scene.paletteNames.size())
                scene.paletteOwners.push_back(ov.toInt());
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
            s.cloneOf = so["clone"].toInt(0);
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
    // The textures of your own that the scene was drawn with come with it.
    for (QJsonValue const& tv : json["textures"].toArray()) {
        QJsonObject const to = tv.toObject();
        TextureBank::add(to["name"].toString(), TextureBank::grainFromText(to["png"].toString()), to["edge"].toDouble(0.5));
    }
    // The node graph, when wired by hand.
    {
        QJsonObject const g = json["graph"].toObject();
        static QStringList const kinds{QStringLiteral("layer"), QStringLiteral("composite"), QStringLiteral("effect"), QStringLiteral("group"),
                                       QStringLiteral("groupIn"), QStringLiteral("groupOut"), QStringLiteral("display"), QStringLiteral("note"),
                                       QStringLiteral("waypoint")};
        for (QJsonValue const& nv : g["nodes"].toArray()) {
            QJsonObject const no = nv.toObject();
            GraphNode n;
            int const kind = int(kinds.indexOf(no["kind"].toString()));
            if (kind < 0)
                continue;   // a kind of node of a newer version: left out
            n.kind = GraphNode::Kind(kind);
            n.id = no["id"].toInt();
            n.name = no["name"].toString();
            n.layer = no["layer"].toInt(0);
            n.symbolLayer = no["symbolLayer"].toInt(0);
            n.sheets = no["sheets"].toInt(0xf) & 0xf;
            n.ports = std::max(0, no["ports"].toInt(1));
            n.outPorts = std::max(0, no["outPorts"].toInt(1));
            n.group = no["group"].toInt(0);
            if (QJsonArray const at = no["at"].toArray(); at.size() == 2)
                n.at = QPointF(at[0].toDouble(), at[1].toDouble());
            n.enabled = no["enabled"].toBool(true);
            n.passThrough = no["passThrough"].toBool(false);
            n.asVector = no["asVector"].toBool(false);
            n.outputZ = std::clamp(no["outputZ"].toInt(0), 0, int(GraphNode::ZFromPort));
            n.zPort = std::max(0, no["zPort"].toInt(0));
            n.thumb = std::clamp(no["thumb"].toInt(0), 0, 2000);
            for (auto const& [key, map] : {std::pair<char const*, QMap<int, QString>*>{"portNames", &n.portNames}, {"portFilters", &n.portFilters}}) {
                QJsonObject const names = no[QLatin1String(key)].toObject();
                for (auto it = names.begin(); it != names.end(); ++it) {
                    bool okay = false;
                    int const port = it.key().toInt(&okay);
                    if (okay && port >= 0 && !it.value().toString().trimmed().isEmpty())
                        map->insert(port, it.value().toString());
                }
            }
            if (n.kind == GraphNode::EffectNode && !effectFromJson(no["effect"].toObject(), n.effect))
                continue;
            for (QJsonValue const& kv : no["amountKeys"].toArray())
                if (QJsonArray const k = kv.toArray(); k.size() == 2)
                    n.amountKeys.insert(k[0].toInt(), std::max(0.0, k[1].toDouble()));
            if (n.id > 0 && !scene.graph.node(n.id))
                scene.graph.nodes.push_back(n);
        }
        for (QJsonValue const& cv : g["cables"].toArray()) {
            QJsonArray const c = cv.toArray();
            if (c.size() == 4 && scene.graph.node(c[0].toInt()) && scene.graph.node(c[2].toInt()))
                scene.graph.cables.push_back({c[0].toInt(), c[1].toInt(), c[2].toInt(), c[3].toInt()});
        }
        scene.graph.backdrops = backdropsFromJson(g["backdrops"].toArray());
        scene.backdrops = backdropsFromJson(json["backdrops"].toArray());
    }
    // The pictures kept in the file itself, each image once (by the key the pictures name it by).
    QHash<QString, QImage> images;
    {
        QJsonObject const stored = json["images"].toObject();
        for (auto it = stored.begin(); it != stored.end(); ++it) {
            QImage image;
            QByteArray const png = QByteArray::fromBase64(it.value().toString().toLatin1());
            image.loadFromData(png, "PNG");
            scene_write::rememberPng(image, png);
            images.insert(it.key(), image);
        }
    }
    scene.nextFunctionId = 1;
    for (QJsonValue const& fv : json["functions"].toArray()) {
        QJsonObject const fo = fv.toObject();
        FunctionColumn c;
        c.id = fo["id"].toInt(0);
        c.name = fo["name"].toString();
        c.kind = std::clamp(fo["kind"].toInt(0), 0, int(FunctionColumn::KindCount) - 1);
        if (c.id <= 0 || scene.function(c.id) || c.name.isEmpty())
            continue;
        c.curve.cells.clear();
        c.velocity.cells.clear();
        c.extendedTiming = fo["extendedTiming"].toBool();
        c.expressionOffset = fo["expressionOffset"].toInt();
        c.expressionFrames = std::max(0, fo["expressionFrames"].toInt());
        QJsonArray const range = fo["velocityRange"].toArray();
        if (range.size() == 2) {
            c.velocityRangeSet = true;
            c.velocityStart = range[0].toInt();
            c.velocityEnd = range[1].toInt();
        }
        keysFromJson(fo["points"].toArray(), c.curve, scene.frameCount(), c.extendedTiming);
        keysFromJson(fo["velocity"].toArray(), c.velocity, scene.frameCount(), c.extendedTiming);
        c.expression = fo["expression"].toString();
        for (QJsonValue const& pv : fo["path"].toArray()) {
            QJsonArray const p = pv.toArray();
            if (p.size() < 7)
                continue;
            c.path.push_back({p[0].toDouble(), p[1].toDouble(), p[2].toDouble(), p[3].toInt(-1), std::clamp(p[4].toDouble(), -1.0, 1.0),
                              std::clamp(p[5].toDouble(), -1.0, 1.0), std::clamp(p[6].toDouble(), -1.0, 1.0), p.size() > 7 && p[7].toBool()});
        }
        c.constantZ = fo["constantZ"].toBool(false);
        c.shown = fo["shown"].toBool(true);
        scene.nextFunctionId = std::max(scene.nextFunctionId, c.id + 1);
        scene.functions.push_back(c);
    }
    for (QJsonValue const& lv : json["layers"].toArray())
        scene.layers.push_back(layerFromJson(lv.toObject(), folder, images, scene.m_frameCount, scene.width, scene.height));
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

bool& Scene::lastLoadUsedFolder()
{
    static thread_local bool used = false;
    return used;
}

QStringList& Scene::lastMissingFiles()
{
    static thread_local QStringList missing;
    return missing;
}

bool Scene::load(QString const& path, Scene& out, QString* error)
{
    lastMissingFiles().clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray bytes = file.readAll();
    // A packed scene file (see Scene::s_compressFiles).
    if (bytes.startsWith("ECLZ")) {
        bytes = qUncompress(bytes.mid(4));
        if (bytes.isEmpty()) {
            if (error)
                *error = QObject::tr("The packed scene file is damaged.");
            return false;
        }
    }
    QJsonParseError parse;
    QJsonDocument const doc = QJsonDocument::fromJson(bytes, &parse);
    if (doc.isNull()) {
        if (error)
            *error = parse.errorString();
        return false;
    }
    // A scene saved as a folder: its drawings, pictures and palettes read back from their files.
    QJsonObject whole = doc.object();
    QStringList missing;
    lastLoadUsedFolder() = SceneProject::usesFolder(whole);
    SceneProject::join(whole, QFileInfo(path).absolutePath(), &missing);
    lastMissingFiles() = missing;
    if (!fromJson(whole, out, error, QFileInfo(path).absolutePath()))
        return false;
    // The palettes linked to a file take the colours the file has now (another scene may have changed them).
    for (int i = 0; i < out.paletteLinks.size() && i < out.paletteNames.size(); ++i)
        if (!out.paletteLinks[i].isEmpty() && QFileInfo::exists(out.paletteLinks[i]))
            out.readPaletteFile(i, out.paletteLinks[i]);
    return true;
}
