// Eclipse - reading the parts of a scene from its file.
#include "model/scene/SceneReadParts.h"

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

namespace scene_read {

int textureRead(int texture)
{
    return texture >= TextureBank::kFirst || (texture >= 0 && texture <= Stroke::kTextureCount) ? texture : 0;
}

Fill fillFromJson(QJsonObject const& o)
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
    if (QJsonArray const seed = o["follows"].toArray(); seed.size() == 2) {
        f.follows = true;
        f.seed = QPointF(seed[0].toDouble(), seed[1].toDouble());
    }
    f.link = std::max(0, o["link"].toInt(0));
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

Stroke strokeFromJson(QJsonObject const& o)
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
    QJsonArray const thick = o["thickness"].toArray();
    double const most = std::max(0, s.pieces());
    for (int i = 0; i + 2 < thick.size(); i += 3) {
        ThicknessPoint key;
        key.at = std::clamp(thick[i].toDouble(), 0.0, most);
        key.left = float(std::clamp(thick[i + 1].toDouble(1.0), 0.0, 100.0));
        key.right = float(std::clamp(thick[i + 2].toDouble(1.0), 0.0, 100.0));
        if (s.thickness.isEmpty() || key.at > s.thickness.last().at)
            s.thickness.push_back(key);
    }
    s.link = std::max(0, o["link"].toInt(0));
    return s;
}

QString found(QString const& folder, QString const& file, QString const& relative)
{
    if (folder.isEmpty() || relative.isEmpty() || QFileInfo::exists(file))
        return file;
    QString const beside = QDir::cleanPath(QDir(folder).absoluteFilePath(relative));
    return QFileInfo::exists(beside) ? beside : file;
}

QVector<QPair<QString, QString>> pairsFromJson(QJsonArray const& array)
{
    QVector<QPair<QString, QString>> pairs;
    for (QJsonValue const& value : array)
        if (value.toArray().size() == 2 && !value.toArray().at(0).toString().isEmpty())
            pairs.push_back({value.toArray().at(0).toString(), value.toArray().at(1).toString()});
    return pairs;
}

bool effectFromJson(QJsonObject const& eo, Effect& e)
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

Drawing drawingFromJson(QJsonValue const& dv, QString const& folder, QHash<QString, QImage> const& images)
{
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
    if (QJsonArray const t = dv.toObject()["trace"].toArray(); t.size() == 6)
        d.trace = QTransform(t[0].toDouble(), t[1].toDouble(), t[2].toDouble(), t[3].toDouble(), t[4].toDouble(), t[5].toDouble());
    if (QJsonArray const p = dv.toObject()["pivot"].toArray(); p.size() == 2) {
        d.pivotSet = true;
        d.pivot = QPointF(p[0].toDouble(), p[1].toDouble());
    }
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
    return d;
}

Layer layerFromJson(QJsonObject const& lo, QString const& folder, QHash<QString, QImage> const& images, int frameCount, double width, double height)
{
    Layer layer;
    layer.name = lo["name"].toString("Layer");
    layer.metadata = pairsFromJson(lo["metadata"].toArray());
    layer.visible = lo["visible"].toBool(true);
    layer.locked = lo["locked"].toBool(false);
    layer.solo = lo["solo"].toBool(false);
    layer.onion = lo["onion"].toBool(false);
    for (QJsonValue const& pv : lo["paletteOverrides"].toArray())
        layer.paletteOverrides.push_back(pv.toInt());
    layer.tagged = lo["tagged"].toBool(false);
    layer.thumbnails = lo["thumbnails"].toBool(false);
    layer.embeddedPivot = std::clamp(lo["embeddedPivot"].toInt(0), 0, int(Layer::PivotOnParentPeg));
    layer.separatePosition = lo["separatePosition"].toBool(false);
    layer.separateScale = lo["separateScale"].toBool(true);
    layer.ignoreParentScale = lo["ignoreParentScale"].toBool(false);
    if (QJsonArray const limits = lo["angleLimits"].toArray(); limits.size() == 2) {
        layer.angleLimited = true;
        layer.minAngle = limits[0].toDouble();
        layer.maxAngle = limits[1].toDouble();
    }
    layer.animatable = lo["animatable"].toBool(true);
    layer.columnHidden = lo["columnHidden"].toBool(false);
    layer.columnWidth = std::clamp(lo["columnWidth"].toInt(0), 0, 1000);
    layer.columnCollapsed = lo["columnCollapsed"].toBool(false);
    for (QJsonValue const& nv : lo["notes"].toArray())
        if (QJsonArray const note = nv.toArray(); note.size() == 2 && note.at(0).toInt(-1) >= 0)
            layer.notes.insert(note.at(0).toInt(), note.at(1).toString());
    for (QJsonValue const& iv : lo["ink"].toArray()) {
        QJsonObject const io = iv.toObject();
        AnnotationInk mark;
        mark.colour = QColor(io["colour"].toString("#ff000000"));
        mark.width = std::clamp(io["width"].toDouble(2.0), 0.5, 50.0);
        QJsonArray const points = io["points"].toArray();
        for (int i = 0; i + 1 < points.size(); i += 2)
            mark.points.push_back(QPointF(points.at(i).toDouble(), points.at(i + 1).toDouble()));
        if (!mark.points.isEmpty())
            layer.ink.push_back(mark);
    }
    for (QJsonValue const& pv : lo["pictures"].toArray()) {
        QJsonObject const po = pv.toObject();
        AnnotationPicture picture;
        picture.frame = std::max(0, po["frame"].toInt(0));
        picture.frames = std::clamp(po["frames"].toInt(4), 1, 100000);
        picture.png = QByteArray::fromBase64(po["png"].toString().toLatin1());
        if (!picture.png.isEmpty())
            layer.pictures.push_back(picture);
    }
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
    layer.hiddenFunctions = lo["hiddenFunctions"].toInt(0) & 511;
    for (QJsonValue const& lv : lo["links"].toArray()) {
        QJsonArray const link = lv.toArray();
        int const value = link.at(0).toInt(-1), function = link.at(1).toInt(0);
        if (value >= 0 && value < Driver::ValueCount && function > 0 && layer.linkOf(value) == 0)
            layer.links.push_back({value, function});
    }
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
    for (QJsonValue const& dv : lo["drawings"].toArray())
        layer.drawings.push_back(drawingFromJson(dv, folder, images));
    for (QJsonValue const& cv : lo["cells"].toArray()) {
        int const c = cv.toInt(Layer::kNoDrawing);
        layer.cells.push_back(c >= 0 && c < layer.drawings.size() ? c : Layer::kNoDrawing);
    }
    layer.cells.resize(frameCount, Layer::kNoDrawing);
    layer.tidyKeyExposures();

    // Version 3: pegs, parents and keyframes.  Older files have none of it.
    layer.id = lo["id"].toInt(0);
    QString const kind = lo["kind"].toString();
    layer.kind = kind == QLatin1String("peg") ? Layer::Peg : kind == QLatin1String("camera") ? Layer::Camera
                 : kind == QLatin1String("sound") ? Layer::Sound : kind == QLatin1String("card") ? Layer::ColourCard
                 : kind == QLatin1String("group") ? Layer::Group : kind == QLatin1String("bone") ? Layer::Bone
                 : kind == QLatin1String("quadmap") ? Layer::Quadmap : kind == QLatin1String("annotation") ? Layer::Annotation : Layer::DrawingLayer;
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
                                    : QPointF(width / 2.0, height / 2.0);
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
    keysFromJson(lo["keys"].toArray(), layer, frameCount);
    if (!layer.holdsDrawings()) {
        layer.drawings.clear();
        layer.cells.fill(Layer::kNoDrawing);
    }
    return layer;
}

void keysFromJson(QJsonArray const& array, Layer& layer, int frameCount)
{
    auto scale = [](double v) { return std::abs(v) < 0.001 ? (v < 0 ? -0.001 : 0.001) : v; };
    for (QJsonValue const& kv : array) {
        QJsonObject const ko = kv.toObject();
        int const frame = ko["frame"].toInt(-1);
        if (frame < 0 || frame >= frameCount)
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
        for (QJsonValue const& pv : ko["points"].toArray()) {
            QJsonArray const v = pv.toArray();
            if (v.size() < 11)
                continue;
            PointMove m;
            m.drawing = v[0].toInt();
            m.sheet = std::clamp(v[1].toInt(), 0, int(Drawing::SheetCount) - 1);
            m.fill = v[2].toInt(-1);
            m.line = v[3].toInt();
            m.point = v[4].toInt();
            m.by = QPointF(v[5].toDouble(), v[6].toDouble());
            m.in = QPointF(v[7].toDouble(), v[8].toDouble());
            m.out = QPointF(v[9].toDouble(), v[10].toDouble());
            pose.points.push_back(m);
        }
        std::sort(pose.points.begin(), pose.points.end(), [](PointMove const& a, PointMove const& b) { return a.comesBefore(b); });
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
        for (QJsonValue const& cv : ko["curves"].toArray()) {
            QJsonArray const c = cv.toArray();
            if (c.size() < 7 || c[0].toInt(-1) < 0 || c[0].toInt() >= Driver::ValueCount)
                continue;
            ValueCurve curve;
            curve.value = c[0].toInt();
            curve.in = QPointF(std::min(0.0, c[1].toDouble()), c[2].toDouble());
            curve.out = QPointF(std::max(0.0, c[3].toDouble()), c[4].toDouble());
            curve.corner = c[5].toBool();
            curve.after = std::clamp(c[6].toInt(), 0, int(ValueCurve::Held));
            key.setCurve(curve);
        }
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
        } else {
            key.tension = key.continuity = key.bias = 0.0;     // (not the defaults for new keyframes)
        }
    }
}

}  // namespace scene_read
