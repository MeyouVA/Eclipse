// Eclipse - the scene: read from a file.
#include "model/scene/TextureBank.h"
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

// A texture as a file names it: none, one of the program's, or one of your own.
static int textureRead(int texture)
{
    return texture >= TextureBank::kFirst || (texture >= 0 && texture <= Stroke::kTextureCount) ? texture : 0;
}

static Fill fillFromJson(QJsonObject const& o)
{
    Fill f;
    f.color = QColor(o["color"].toString("#ff000000"));
    f.swatch = o["swatch"].toInt(-1);
    f.group = o["group"].toInt(0);
    f.order = o["order"].toInt(0);
    if (QJsonArray const grad = o["grad"].toArray(); grad.size() == 4) {
        f.gradPlaced = true;
        f.gradFrom = QPointF(grad[0].toDouble(), grad[1].toDouble());
        f.gradTo = QPointF(grad[2].toDouble(), grad[3].toDouble());
    }
    f.texture = textureRead(o["texture"].toInt(0));
    f.textureAmount = float(std::clamp(o["textureAmount"].toDouble(1.0), 0.0, 1.0));
    f.textureCrisp = o["textureCrisp"].toBool(false);
    if (QJsonArray const ramp = o["textureRamp"].toArray(); ramp.size() == 2) {
        f.textureLow = float(std::clamp(ramp[0].toDouble(0.0), 0.0, 1.0));
        f.textureHigh = float(std::clamp(ramp[1].toDouble(1.0), double(f.textureLow), 1.0));
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
    s.order = o["order"].toInt(0);
    s.closed = o["closed"].toBool(false);
    if (QJsonArray const grad = o["grad"].toArray(); grad.size() == 4) {
        s.gradPlaced = true;
        s.gradFrom = QPointF(grad[0].toDouble(), grad[1].toDouble());
        s.gradTo = QPointF(grad[2].toDouble(), grad[3].toDouble());
    }
    s.texture = textureRead(o["texture"].toInt(0));
    s.textureAmount = float(std::clamp(o["textureAmount"].toDouble(1.0), 0.0, 1.0));
    s.textureCrisp = o["textureCrisp"].toBool(false);
    if (QJsonArray const ramp = o["textureRamp"].toArray(); ramp.size() == 2) {
        s.textureLow = float(std::clamp(ramp[0].toDouble(0.0), 0.0, 1.0));
        s.textureHigh = float(std::clamp(ramp[1].toDouble(1.0), double(s.textureLow), 1.0));
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
    QJsonArray const leans = o["lean"].toArray(), opacities = o["opacity"].toArray();
    for (int i = 0; i + 1 < leans.size(); i += 2)
        if (int const index = leans[i].toInt(-1); index >= 0 && index < s.points.size())
            s.points[index].lean = float(std::clamp(leans[i + 1].toDouble(), -1.0, 1.0));
    QJsonArray const joints = o["joints"].toArray();
    for (int i = 0; i + 1 < joints.size(); i += 2)
        if (int const index = joints[i].toInt(-1); index >= 0 && index < s.points.size())
            s.points[index].joint = std::max(0, joints[i + 1].toInt());
    for (int i = 0; i + 1 < opacities.size(); i += 2)
        if (int const index = opacities[i].toInt(-1); index >= 0 && index < s.points.size())
            s.points[index].opacity = float(std::clamp(opacities[i + 1].toDouble(1.0), 0.0, 1.0));
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

// An effect from the scene file (false: one of a kind this version does not know).
static bool effectFromJson(QJsonObject const& eo, Effect& e)
{
    QString const type = eo["type"].toString();
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
    else if (type == QLatin1String("particles"))
        e.type = Effect::Particles;
    else if (type == QLatin1String("transparency"))
        e.type = Effect::Transparency;
    else if (type == QLatin1String("directionalBlur"))
        e.type = Effect::DirectionalBlur;
    else if (type == QLatin1String("hueSaturation"))
        e.type = Effect::HueSaturation;
    else if (type == QLatin1String("brightnessContrast"))
        e.type = Effect::BrightnessContrast;
    else if (type == QLatin1String("grain"))
        e.type = Effect::Grain;
    else if (type == QLatin1String("radialBlur"))
        e.type = Effect::RadialBlur;
    else if (type == QLatin1String("colourScale"))
        e.type = Effect::ColourScale;
    else if (type == QLatin1String("negate"))
        e.type = Effect::Negate;
    else if (type == QLatin1String("median"))
        e.type = Effect::Median;
    else if (type == QLatin1String("ripple"))
        e.type = Effect::Ripple;
    else if (type == QLatin1String("tone"))
        e.type = Effect::Tone;
    else if (type == QLatin1String("highlight"))
        e.type = Effect::Highlight;
    else if (type == QLatin1String("bloom"))
        e.type = Effect::Bloom;
    else if (type == QLatin1String("pixelate"))
        e.type = Effect::Pixelate;
    else if (type == QLatin1String("turbulence"))
        e.type = Effect::Turbulence;
    else if (type == QLatin1String("matteGenerator"))
        e.type = Effect::MatteGenerator;
    else if (type == QLatin1String("blackWhite"))
        e.type = Effect::BlackWhite;
    else if (type == QLatin1String("matteResize"))
        e.type = Effect::MatteResize;
    else if (type == QLatin1String("matteBlur"))
        e.type = Effect::MatteBlur;
    else if (type == QLatin1String("gradient"))
        e.type = Effect::Gradient;
    else if (type == QLatin1String("channelSelector"))
        e.type = Effect::ChannelSelector;
    else if (type == QLatin1String("crop"))
        e.type = Effect::Crop;
    else if (type == QLatin1String("removeTransparency"))
        e.type = Effect::RemoveTransparency;
    else if (type == QLatin1String("matteComposite"))
        e.type = Effect::MatteComposite;
    else if (type == QLatin1String("blending"))
        e.type = Effect::Blending;
    else if (type == QLatin1String("variableBlur"))
        e.type = Effect::VariableBlur;
    else if (type == QLatin1String("motionBlur"))
        e.type = Effect::MotionBlur;
    else if (type == QLatin1String("fieldChart"))
        e.type = Effect::FieldChart;
    else if (type == QLatin1String("colourCurves"))
        e.type = Effect::ColourCurves;
    else if (type == QLatin1String("focus"))
        e.type = Effect::Focus;
    else if (type == QLatin1String("applyPeg"))
        e.type = Effect::ApplyPeg;
    else if (type == QLatin1String("scaleOutput"))
        e.type = Effect::ScaleOutput;
    else if (type == QLatin1String("glue"))
        e.type = Effect::Glue;
    else if (type == QLatin1String("script"))
        e.type = Effect::Script;
    else if (type == QLatin1String("external"))
        e.type = Effect::External;
    else
        return false;  // an effect of a newer version: left out rather than guessed at
    e.enabled = eo["enabled"].toBool(true);
    e.amount = std::max(0.0, eo["amount"].toDouble());
    e.animated = eo["animated"].toBool(false);
    if (e.type == Effect::Particles) {
        e.life = std::clamp(eo["life"].toInt(24), 1, 600);
        e.speed = eo["speed"].toDouble(6.0);
        e.spread = std::clamp(eo["spread"].toDouble(60.0), 0.0, 360.0);
        e.direction = eo["direction"].toDouble(90.0);
        e.gravity = eo["gravity"].toDouble(0.3);
        e.seed = eo["seed"].toInt(1);
        e.endSize = std::clamp(eo["endSize"].toDouble(1.0), 0.0, 20.0);
        e.followLayer = eo["follow"].toBool(false);
        e.endColor = eo.contains("endColor") ? QColor(eo["endColor"].toString()) : QColor();
        e.wind = eo["wind"].toDouble(0.0);
        e.spin = std::clamp(eo["spin"].toDouble(0.0), 0.0, 360.0);
        e.regionWidth = std::max(0.0, eo["regionW"].toDouble(0.0));
        e.regionHeight = std::max(0.0, eo["regionH"].toDouble(0.0));
        e.regionShape = std::clamp(eo["regionShape"].toInt(0), 0, 3);
        e.floor = std::max(0.0, eo["floor"].toDouble(0.0));
        e.bounce = std::clamp(eo["bounce"].toDouble(0.5), 0.0, 1.0);
        e.walls = std::max(0.0, eo["walls"].toDouble(0.0));
    }
    if (e.type == Effect::DirectionalBlur || e.type == Effect::Focus)
        e.direction = eo["direction"].toDouble(0.0);
    if (e.type == Effect::Ripple)
        e.direction = std::max(2.0, eo["direction"].toDouble(60.0));
    if (e.type == Effect::Turbulence)
        e.direction = std::max(2.0, eo["direction"].toDouble(40.0));
    if (e.type == Effect::MatteGenerator)
        e.direction = std::max(0.0, eo["direction"].toDouble(0.0));
    if (e.type == Effect::Gradient) {
        e.direction = eo["direction"].toDouble(90.0);
        e.endColor = QColor(eo["endColor"].toString(QStringLiteral("#ffc83c8c")));
    }
    if (e.type == Effect::Grain)
        e.seed = eo["seed"].toInt(1);
    e.hue = std::clamp(eo["hue"].toDouble(0.0), -180.0, 180.0);
    e.saturation = std::clamp(eo["saturation"].toDouble(0.0), -100.0, 100.0);
    e.lightness = std::clamp(eo["lightness"].toDouble(0.0), -100.0, 100.0);
    e.brightness = std::clamp(eo["brightness"].toDouble(0.0), -100.0, 100.0);
    e.contrast = std::clamp(eo["contrast"].toDouble(0.0), -100.0, 100.0);
    e.offset = QPointF(eo["dx"].toDouble(), eo["dy"].toDouble());
    e.color = QColor(eo["color"].toString("#ff000000"));
    e.matte = eo["matte"].toInt(0);
    e.invert = eo["invert"].toBool(false);
    e.channel = std::clamp(eo["channel"].toInt(0), 0, 3);
    if (QJsonArray const area = eo["area"].toArray(); area.size() == 4)
        e.area = QRectF(area[0].toDouble(), area[1].toDouble(), std::max(0.0, area[2].toDouble()), std::max(0.0, area[3].toDouble()));
    e.blendMode = std::clamp(eo["blendMode"].toInt(1), 0, int(Layer::BlendCount) - 1);
    if (e.type == Effect::Script || e.type == Effect::External)
        e.text = eo["text"].toString();
    QJsonArray const curves = eo["curves"].toArray();
    for (int c = 0; c < 4 && c < curves.size(); ++c)
        for (QJsonValue const& v : curves[c].toArray())
            if (QJsonArray const p = v.toArray(); p.size() == 2)
                e.curves[c].push_back(QPointF(std::clamp(p[0].toDouble(), 0.0, 1.0), std::clamp(p[1].toDouble(), 0.0, 1.0)));
    return true;
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
    scene.fovAcross = json["fovAcross"].toBool(false);
    scene.fov = std::clamp(json["fov"].toDouble(Layer::kDefaultFov), 1.0, 170.0);
    scene.activeCamera = json["camera"].toInt(0);
    scene.display = json["display"].toInt(0);
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
    for (QJsonValue const& wv : json["writes"].toArray()) {
        QJsonObject const wo = wv.toObject();
        Write w;
        w.name = wo["name"].toString();
        w.movie = wo["movie"].toBool(false);
        w.path = wo["path"].toString();
        w.format = wo["format"].toString(QStringLiteral("png"));
        w.percent = std::clamp(wo["percent"].toInt(100), 1, 1000);
        w.enabled = wo["enabled"].toBool(true);
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
    // The textures of your own that the scene was drawn with come with it.
    for (QJsonValue const& tv : json["textures"].toArray()) {
        QJsonObject const to = tv.toObject();
        TextureBank::add(to["name"].toString(), TextureBank::grainFromText(to["png"].toString()), to["edge"].toDouble(0.5));
    }
    // The node graph, when wired by hand.
    {
        QJsonObject const g = json["graph"].toObject();
        static QStringList const kinds{QStringLiteral("layer"), QStringLiteral("composite"), QStringLiteral("effect"), QStringLiteral("group"),
                                       QStringLiteral("groupIn"), QStringLiteral("groupOut"), QStringLiteral("display"), QStringLiteral("note")};
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
            n.sheets = no["sheets"].toInt(0xf) & 0xf;
            n.ports = std::max(0, no["ports"].toInt(1));
            n.outPorts = std::max(0, no["outPorts"].toInt(1));
            n.group = no["group"].toInt(0);
            if (QJsonArray const at = no["at"].toArray(); at.size() == 2)
                n.at = QPointF(at[0].toDouble(), at[1].toDouble());
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
    }
    // The pictures kept in the file itself, each image once (by the key the pictures name it by).
    QHash<QString, QImage> images;
    {
        QJsonObject const stored = json["images"].toObject();
        for (auto it = stored.begin(); it != stored.end(); ++it) {
            QImage image;
            image.loadFromData(QByteArray::fromBase64(it.value().toString().toLatin1()), "PNG");
            images.insert(it.key(), image);
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
        layer.syncGroup = std::max(0, lo["sync"].toInt(0));
        layer.hiddenSheets = lo["hiddenSheets"].toInt(0) & 15;
        layer.cloneGroup = std::max(0, lo["clone"].toInt(0));
        layer.cloneTiming = lo["cloneTiming"].toBool(true);
        layer.bitmap = lo["bitmap"].toBool(false);
        layer.bitmapArt = lo["bitmapArt"].toInt(0) & 11;
        layer.bendOnBones = lo["bendOnBones"].toBool(false);
        layer.jointSmoothness = std::clamp(lo["jointSmooth"].toDouble(0.0), 0.0, 1000.0);
        for (QJsonValue const& dv : lo["drivers"].toArray()) {
            QJsonObject const d = dv.toObject();
            Driver driver;
            driver.value = d["value"].toInt(-1);
            driver.source = d["source"].toInt(0);
            driver.sourceValue = d["sourceValue"].toInt(-1);
            driver.factor = d["factor"].toDouble(1.0);
            driver.offset = d["offset"].toDouble(0.0);
            if (driver.value >= 0 && driver.value < Driver::ValueCount && driver.sourceValue >= 0 && driver.sourceValue < Driver::ValueCount)
                layer.drivers.push_back(driver);
        }
        for (QJsonValue const& dv : lo["skin"].toArray()) {
            QJsonArray const d = dv.toArray();
            if (d.size() != 5 || d[0].toInt(-1) < 0 || d[3].toDouble() <= 0.0)
                continue;
            layer.skin.push_back({d[0].toInt(), QPointF(d[1].toDouble(), d[2].toDouble()), d[3].toDouble(), std::clamp(d[4].toDouble(), -1.0, 1.0)});
        }
        {
            // (A mode of a newer version: laid over as usual.)
            static char const* const kBlends[Layer::BlendCount] = {"normal", "multiply", "screen", "overlay", "darken", "lighten", "colourDodge",
                                                                   "colourBurn", "hardLight", "softLight", "difference", "exclusion", "add"};
            QString const blend = lo["blend"].toString();
            for (int b = 0; b < Layer::BlendCount; ++b)
                if (blend == QLatin1String(kBlends[b]))
                    layer.blend = b;
        }
        layer.bitmapResolution = std::clamp(lo["bitmapResolution"].toInt(100), 25, 400);
        layer.ikExcluded = lo["ikExcluded"].toBool(false);
        layer.ikNail = lo["ikNail"].toBool(false);
        layer.ikHoldAngle = lo["ikHoldAngle"].toBool(false);
        if (QJsonArray const limits = lo["ikLimits"].toArray(); limits.size() == 2) {
            layer.ikLimited = true;
            layer.ikMin = std::min(limits[0].toDouble(), limits[1].toDouble());
            layer.ikMax = std::max(limits[0].toDouble(), limits[1].toDouble());
        }
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
            if (QString const model = dv.toObject()["model"].toString(); !model.isEmpty())
                d.model = found(folder, model, dv.toObject()["modelRelative"].toString());
            if (QJsonArray const centre = dv.toObject()["modelCentre"].toArray(); centre.size() == 2)
                d.modelCentre = QPointF(centre[0].toDouble(), centre[1].toDouble());
            d.modelScale = std::clamp(dv.toObject()["modelScale"].toDouble(1.0), 0.01, 100.0);
            d.set = dv.toObject()["set"].toString();
            d.mark = std::clamp(dv.toObject()["mark"].toInt(0), 0, int(Drawing::InBetweenMark));
            for (QJsonValue const& pv : dv.toObject()["pictures"].toArray()) {
                QJsonObject const po = pv.toObject();
                Picture p;
                p.file = found(folder, po["file"].toString(), po["relative"].toString());
                p.linked = po["linked"].toBool(false);
                p.painted = po["painted"].toBool(false);
                p.sheet = std::clamp(po["sheet"].toInt(Drawing::LineSheet), 0, Drawing::SheetCount - 1);
                p.order = po["order"].toInt(-1);
                for (QJsonValue const& rv : po["clip"].toArray()) {
                    QJsonArray const points = rv.toArray();
                    QPolygonF ring;
                    for (int i = 0; i + 1 < points.size(); i += 2)
                        ring << QPointF(points[i].toDouble(), points[i + 1].toDouble());
                    if (ring.size() >= 3)
                        p.clip.push_back(ring);
                }
                p.size = QSize(po["width"].toInt(), po["height"].toInt());
                QJsonArray const m = po["placement"].toArray();
                if (m.size() == 9)
                    p.placement = QTransform(m[0].toDouble(), m[1].toDouble(), m[2].toDouble(), m[3].toDouble(), m[4].toDouble(), m[5].toDouble(),
                                             m[6].toDouble(), m[7].toDouble(), m[8].toDouble());
                if (p.linked)
                    p.image = QImage(p.file);       // null when the file has gone: the place is kept
                else if (po.contains("image"))
                    p.image = images.value(po["image"].toString());
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
            Effect e;
            if (effectFromJson(ev.toObject(), e))
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
            for (QJsonValue const& vv : values.at(4).toArray()) {
                QJsonArray const point = vv.toArray();
                double const time = point.at(0).toDouble(-1.0);
                if (point.size() == 2 && time > 0.0 && time < 1.0 && (morph.velocity.isEmpty() || time > morph.velocity.last().x()))
                    morph.velocity.push_back(QPointF(time, std::clamp(point.at(1).toDouble(), -0.5, 1.5)));
            }
            for (QJsonValue const& gv : values.at(5).toArray()) {
                QJsonArray const group = gv.toArray();
                Morph::Group g;
                g.name = group.at(0).toString();
                for (QJsonValue const& k : group.at(1).toArray())
                    g.first.push_back(k.toInt());
                for (QJsonValue const& k : group.at(2).toArray())
                    g.second.push_back(k.toInt());
                morph.groups.push_back(g);
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
            key.parts = ko["parts"].toInt(Keyframe::AllParts) & Keyframe::AllParts;
            if (key.parts == 0)
                key.parts = Keyframe::AllParts;
            key.easeIn = std::clamp(ko["ein"].toDouble(0.0), 0.0, 1.0);
            key.easeOut = std::clamp(ko["eout"].toDouble(0.0), 0.0, 1.0);
            key.liftIn = std::clamp(ko["lin"].toDouble(0.0), -2.0, 2.0);
            key.liftOut = std::clamp(ko["lout"].toDouble(0.0), -2.0, 2.0);
            QJsonArray const controls = ko["controls"].toArray();
            for (int i = 0; i + 1 < controls.size(); i += 2)
                key.controls.push_back(QPointF(controls[i].toDouble(), controls[i + 1].toDouble()));
            for (QJsonValue const z : ko["controlsZ"].toArray())
                if (key.controlsZ.size() < key.controls.size())
                    key.controlsZ.push_back(z.toDouble());
            QJsonArray const tcb = ko["tcb"].toArray();
            if (tcb.size() == 3) {
                key.tension = std::clamp(tcb[0].toDouble(), -1.0, 1.0);
                key.continuity = std::clamp(tcb[1].toDouble(), -1.0, 1.0);
                key.bias = std::clamp(tcb[2].toDouble(), -1.0, 1.0);
            }
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
    if (!fromJson(doc.object(), out, error, QFileInfo(path).absolutePath()))
        return false;
    // The palettes linked to a file take the colours the file has now (another scene may have changed them).
    for (int i = 0; i < out.paletteLinks.size() && i < out.paletteNames.size(); ++i)
        if (!out.paletteLinks[i].isEmpty() && QFileInfo::exists(out.paletteLinks[i]))
            out.readPaletteFile(i, out.paletteLinks[i]);
    return true;
}
