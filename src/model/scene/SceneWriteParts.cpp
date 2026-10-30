// Eclipse - writing the parts of a scene to its file.
#include "model/scene/SceneWriteParts.h"

#include "model/scene/TextureBank.h"
#include "model/scene/Scene.h"
#include <QBuffer>
#include <QMutex>
#include <QFileInfo>
#include <QDir>
#include "model/geometry/Deform.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scene_write {

QJsonObject strokeToJson(Stroke const& s)
{
    QJsonArray pts;
    for (StrokePoint const& p : s.points) {
        pts.append(std::round(p.pos.x() * 100.0) / 100.0);
        pts.append(std::round(p.pos.y() * 100.0) / 100.0);
        pts.append(std::round(double(p.pressure) * 1000.0) / 1000.0);
    }
    QJsonObject o{{"color", s.color.name(QColor::HexArgb)}, {"swatch", s.swatch}, {"width", double(s.width)}, {"points", pts}};
    if (s.unpaintedWidth > 0.0f)
        o["unpaintedWidth"] = double(s.unpaintedWidth);
    if (s.group != 0)
        o["group"] = s.group;
    if (s.link != 0)
        o["link"] = s.link;
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
    // Leans and opacities where they are not the usual: index, value.
    QJsonArray leans, opacities;
    for (int i = 0; i < s.points.size(); ++i) {
        if (std::abs(s.points[i].lean) > 0.0005f)
            leans << i << std::round(double(s.points[i].lean) * 1000.0) / 1000.0;
        if (s.points[i].opacity < 0.9995f)
            opacities << i << std::round(double(s.points[i].opacity) * 1000.0) / 1000.0;
    }
    if (!leans.isEmpty())
        o["lean"] = leans;
    // Joints: index, joint number.
    QJsonArray joints;
    for (int i = 0; i < s.points.size(); ++i)
        if (s.points[i].joint != 0)
            joints << i << s.points[i].joint;
    if (!joints.isEmpty())
        o["joints"] = joints;
    // The thickness along the line, apart from its points: place, left, right.
    QJsonArray thick;
    for (ThicknessPoint const& key : s.thickness)
        thick << std::round(key.at * 10000.0) / 10000.0 << std::round(double(key.left) * 1000.0) / 1000.0 << std::round(double(key.right) * 1000.0) / 1000.0;
    if (!thick.isEmpty())
        o["thickness"] = thick;
    if (!opacities.isEmpty())
        o["opacity"] = opacities;
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

QJsonObject fillToJson(Fill const& f)
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
    if (f.follows)
        o["follows"] = QJsonArray{std::round(f.seed.x() * 100.0) / 100.0, std::round(f.seed.y() * 100.0) / 100.0};
    if (f.link != 0)
        o["link"] = f.link;
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
        QJsonArray contours;
        for (Stroke const& contour : f.contours)
            contours.append(strokeToJson(contour));
        o["contours"] = contours;
    }
    return o;
}

QString pathFrom(QString const& folder, QString const& file)
{
    return folder.isEmpty() || file.isEmpty() ? QString() : QDir(folder).relativeFilePath(file);
}

QJsonArray pairsJson(QVector<QPair<QString, QString>> const& pairs)
{
    QJsonArray array;
    for (QPair<QString, QString> const& pair : pairs)
        array.append(QJsonArray{pair.first, pair.second});
    return array;
}

char const* effectTypeName(Effect::Type type)
{
    static char const* const names[] = {"gaussianBlur", "boxBlur", "zoomBlur", "bokehBlur", "antiFlicker", "colourFade", "colourOverride", "channelSwap",
                                        "colourLevels", "colourScreen", "contrast", "gamma", "colourBanding", "dither", "shine", "lensFlare", "burnIn",
                                        "turbulentNoise", "grid", "refract", "shake", "quake", "unsharpMask", "increaseOpacity", "animatePencilTexture", "colourSpace", "lookUpTable",
                                        "holdTiming", "holdTransformationTiming", "retime", "retimeTransformation", "loop", "loopTransformation"};
    static_assert(sizeof(names) / sizeof(names[0]) == Effect::TypeCount - Effect::GaussianBlur);
    return type >= Effect::GaussianBlur && type < Effect::TypeCount ? names[type - Effect::GaussianBlur] : "";
}

QJsonObject effectToJson(Effect const& e)
{
    QJsonObject one{{"type", e.type == Effect::Blur ? "blur" : e.type == Effect::Tint ? "tint" : e.type == Effect::Glow ? "glow"
                             : e.type == Effect::Shadow ? "shadow" : e.type == Effect::Particles ? "particles"
                             : e.type == Effect::Transparency ? "transparency" : e.type == Effect::DirectionalBlur ? "directionalBlur"
                             : e.type == Effect::HueSaturation ? "hueSaturation" : e.type == Effect::BrightnessContrast ? "brightnessContrast"
                             : e.type == Effect::Grain ? "grain" : e.type == Effect::RadialBlur ? "radialBlur" : e.type == Effect::ColourScale ? "colourScale"
                             : e.type == Effect::Negate ? "negate" : e.type == Effect::Median ? "median" : e.type == Effect::Ripple ? "ripple"
                             : e.type == Effect::Tone ? "tone" : e.type == Effect::Highlight ? "highlight"
                             : e.type == Effect::Bloom ? "bloom" : e.type == Effect::Pixelate ? "pixelate" : e.type == Effect::Turbulence ? "turbulence"
                             : e.type == Effect::MatteGenerator ? "matteGenerator"
                             : e.type == Effect::BlackWhite ? "blackWhite" : e.type == Effect::MatteResize ? "matteResize"
                             : e.type == Effect::MatteBlur ? "matteBlur" : e.type == Effect::Gradient ? "gradient"
                             : e.type == Effect::ChannelSelector ? "channelSelector" : e.type == Effect::Crop ? "crop"
                             : e.type == Effect::RemoveTransparency ? "removeTransparency" : e.type == Effect::MatteComposite ? "matteComposite"
                             : e.type == Effect::Blending ? "blending" : e.type == Effect::VariableBlur ? "variableBlur"
                             : e.type == Effect::MotionBlur ? "motionBlur" : e.type == Effect::FieldChart ? "fieldChart"
                             : e.type == Effect::ColourCurves ? "colourCurves" : e.type == Effect::Focus ? "focus"
                             : e.type == Effect::ApplyPeg ? "applyPeg" : e.type == Effect::ScaleOutput ? "scaleOutput" : e.type == Effect::Glue ? "glue"
                             : e.type == Effect::Script ? "script" : e.type == Effect::External ? "external" : e.type == Effect::ApplyCamera ? "applyCamera"
                             : e.type == Effect::GenericComposite ? "genericComposite" : e.type == Effect::LayerSelector ? "layerSelector"
                             : e.type == Effect::ChromaKey ? "chromaKey" : e.type == Effect::DifferenceKeyer ? "differenceKeyer"
                             : e.type == Effect::LuminanceThreshold ? "luminanceThreshold" : e.type == Effect::ImageSwitch ? "imageSwitch"
                             : e.type == Effect::Visibility ? "visibility"
                             : e.type >= Effect::GaussianBlur && e.type < Effect::TypeCount ? effectTypeName(e.type)
                             : "cutter"},
                    {"enabled", e.enabled}, {"amount", e.amount}, {"animated", e.animated},
                    {"color", e.color.name(QColor::HexArgb)}, {"dx", e.offset.x()}, {"dy", e.offset.y()},
                    {"matte", e.matte}, {"invert", e.invert}};
    if (e.truckFactor)
        one["truckFactor"] = true;
    if (e.type == Effect::ApplyCamera) {
        if (!e.cameraViewpoint)
            one["cameraViewpoint"] = false;
        if (e.flattenDepth)
            one["flattenDepth"] = true;
    }
    if (e.type == Effect::Gradient)
        one["endColor"] = e.endColor.name(QColor::HexArgb);
    if (e.type == Effect::ChannelSelector)
        one["channel"] = e.channel;
    if (e.type == Effect::Crop)
        one["area"] = QJsonArray{e.area.x(), e.area.y(), e.area.width(), e.area.height()};
    if (e.type == Effect::Blending)
        one["blendMode"] = e.blendMode;
    if (e.type == Effect::GenericComposite) {
        one["colourOperation"] = e.colourOperation;
        one["alphaOperation"] = e.alphaOperation;
        one["intensity"] = QJsonArray{e.intensity[0], e.intensity[1], e.intensity[2]};
    }
    if (e.type == Effect::LayerSelector)
        one["sheets"] = e.sheets;
    if (e.type == Effect::ChromaKey || e.type == Effect::DifferenceKeyer || e.type == Effect::LuminanceThreshold) {
        one["softness"] = e.softness;
        one["clipBlack"] = e.clipBlack;
        one["clipWhite"] = e.clipWhite;
        one["spill"] = e.spill;
    }
    if (e.type == Effect::Visibility) {
        one["showInViews"] = e.showInViews;
        one["showInRender"] = e.showInRender;
    }
    if (e.type == Effect::Script || e.type == Effect::External || e.type == Effect::BurnIn)
        one["text"] = e.text;
    if (e.type == Effect::ColourSpace) {
        one["from"] = e.fromSpace;
        one["to"] = e.toSpace;
    }
    if (e.type == Effect::LookUpTable)
        one["file"] = e.file;
    if (!e.values.isEmpty()) {
        QJsonObject values;
        for (auto v = e.values.cbegin(); v != e.values.cend(); ++v)
            values[v.key()] = v.value();
        one["values"] = values;
    }
    if (!e.overrides.isEmpty()) {
        QJsonArray overrides;
        for (QPair<int, QColor> const& o : e.overrides)
            overrides.append(QJsonArray{o.first, o.second.name(QColor::HexArgb)});
        one["overrides"] = overrides;
    }
    if (!e.overrideImages.isEmpty()) {
        QJsonArray images;
        for (auto o = e.overrideImages.constBegin(); o != e.overrideImages.constEnd(); ++o)
            images.append(QJsonArray{o.key(), o.value()});
        one["overrideImages"] = images;
    }
    if (e.type == Effect::ColourCurves) {
        QJsonArray curves;
        for (QVector<QPointF> const& curve : e.curves) {
            QJsonArray points;
            for (QPointF const& p : curve)
                points.append(QJsonArray{p.x(), p.y()});
            curves.append(points);
        }
        one["curves"] = curves;
    }
    if (e.type == Effect::DirectionalBlur || e.type == Effect::Ripple || e.type == Effect::Turbulence || e.type == Effect::MatteGenerator
        || e.type == Effect::Gradient || e.type == Effect::Focus)
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
    return one;
}

namespace {
QMutex gPngLock;
QHash<qint64, QByteArray> gPngs;
qint64 gPngBytes = 0;
}

void rememberPng(QImage const& image, QByteArray const& png)
{
    if (image.isNull() || png.isEmpty())
        return;
    QMutexLocker const locked(&gPngLock);
    if (gPngBytes + png.size() > 512LL * 1024 * 1024) {
        gPngs.clear();
        gPngBytes = 0;
    }
    if (!gPngs.contains(image.cacheKey())) {
        gPngs.insert(image.cacheKey(), png);
        gPngBytes += png.size();
    }
}

// A picture as a PNG; kept from one save to the next while the picture is unchanged (its cache key changes when it is drawn on).
static QByteArray pngOf(QImage const& image)
{
    {
        QMutexLocker const locked(&gPngLock);
        if (auto const found = gPngs.constFind(image.cacheKey()); found != gPngs.cend())
            return *found;
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    rememberPng(image, bytes);
    return bytes;
}

QJsonObject drawingToJson(Drawing const& d, QString const& folder, QJsonObject& images, QHash<qint64, QString>& imageKeys)
{
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
        if (p.order >= 0)
            picture["order"] = p.order;
        if (!p.clip.isEmpty()) {
            QJsonArray clip;
            for (QPolygonF const& ring : p.clip) {
                QJsonArray points;
                for (QPointF const& at : ring) {
                    points.append(at.x());
                    points.append(at.y());
                }
                clip.append(points);
            }
            picture["clip"] = clip;
        }
        if (!p.colourSpace.isEmpty())
            picture["colourSpace"] = p.colourSpace;
        if (p.linked) {
            picture["linked"] = true;
            if (p.ownProfile)
                picture["ownProfile"] = true;
            if (QString const relative = pathFrom(folder, p.file); !relative.isEmpty())
                picture["relative"] = relative;
        } else {
            // Kept in the scene file itself, as a PNG (once, however many pictures show it).
            QString key = imageKeys.value(p.image.cacheKey());
            if (key.isEmpty()) {
                key = QString::number(imageKeys.size() + 1);
                imageKeys.insert(p.image.cacheKey(), key);
                images[key] = QString::fromLatin1(pngOf(p.image).toBase64());
            }
            picture["image"] = key;
        }
        pictures.append(picture);
    }
    drawing["pictures"] = pictures;
}
if (d.pinned)
    drawing["pinned"] = true;
if (!d.name.isEmpty())
    drawing["name"] = d.name;
if (!d.model.isEmpty()) {
    drawing["model"] = d.model;
    drawing["modelCentre"] = QJsonArray{d.modelCentre.x(), d.modelCentre.y()};
    if (d.modelScale != 1.0)
        drawing["modelScale"] = d.modelScale;
    if (QString const relative = pathFrom(folder, d.model); !relative.isEmpty())
        drawing["modelRelative"] = relative;
}
if (!d.set.isEmpty())
    drawing["set"] = d.set;
if (d.mark != Drawing::NoMark)
    drawing["mark"] = d.mark;
if (!d.trace.isIdentity())
    drawing["trace"] = QJsonArray{d.trace.m11(), d.trace.m12(), d.trace.m21(), d.trace.m22(), d.trace.dx(), d.trace.dy()};
if (d.pivotSet)
    drawing["pivot"] = QJsonArray{d.pivot.x(), d.pivot.y()};
    return drawing;
}

QJsonObject keyToJson(Keyframe const& k)
{
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
if (!k.curves.isEmpty()) {
    QJsonArray curves;
    for (ValueCurve const& c : k.curves)
        curves.append(QJsonArray{c.value, c.in.x(), c.in.y(), c.out.x(), c.out.y(), c.corner, c.after});
    key["curves"] = curves;
}
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
if (!k.pose.points.isEmpty()) {
    QJsonArray points;
    for (PointMove const& m : k.pose.points)
        points.append(QJsonArray{m.drawing, m.sheet, m.fill, m.line, m.point, m.by.x(), m.by.y(), m.in.x(), m.in.y(), m.out.x(), m.out.y()});
    key["points"] = points;
}
    return key;
}

QJsonObject layerToJson(Layer const& layer, QString const& folder, QJsonObject& images, QHash<qint64, QString>& imageKeys)
{
    QJsonArray drawings;
    for (Drawing const& real : layer.drawings) {
        // (The drawings of a layer that shows a symbol are made from the symbol: only their names are kept.)
        Drawing blank;
        blank.name = real.name;
        blank.pinned = true;
        Drawing const& d = layer.showsSymbol() ? blank : real;
        drawings.append(drawingToJson(d, folder, images, imageKeys));
    }
    QJsonArray cells;
    for (int c : layer.cells)
        cells.append(c);
    QJsonArray morphs;
    for (Morph const& morph : layer.morphs) {
        QJsonArray entry{morph.from, morph.easeIn, morph.easeOut};
        if (!morph.hints.isEmpty() || !morph.velocity.isEmpty() || !morph.groups.isEmpty()) {
            QJsonArray hints;
            for (QLineF const& hint : morph.hints)
                hints.append(QJsonArray{hint.x1(), hint.y1(), hint.x2(), hint.y2()});
            entry.append(hints);
        }
        if (!morph.velocity.isEmpty() || !morph.groups.isEmpty()) {
            QJsonArray curve;
            for (QPointF const& v : morph.velocity)
                curve.append(QJsonArray{v.x(), v.y()});
            entry.append(curve);
        }
        if (!morph.groups.isEmpty()) {
            // Its morphing layers: a name, the shapes of the first drawing, the shapes of the second.
            QJsonArray groups;
            for (Morph::Group const& g : morph.groups) {
                QJsonArray first, second;
                for (int k : g.first)
                    first.append(k);
                for (int k : g.second)
                    second.append(k);
                groups.append(QJsonArray{g.name, first, second});
            }
            entry.append(groups);
        }
        morphs.append(entry);
    }
    QJsonArray keys;
    for (Keyframe const& k : layer.keys) {
        keys.append(keyToJson(k));
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
    static char const* const kKinds[] = {"drawing", "peg", "camera", "sound", "card", "group", "bone", "quadmap", "annotation"};
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
                                  {"nearPlane", layer.nearPlane},
                                  {"farPlane", layer.farPlane},
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
                                  {"thumbnails", layer.thumbnails},
                                  {"collapsed", layer.collapsed},
                                  {"track", layer.trackColor.isValid() ? layer.trackColor.name() : QString()},
                                  {"opacity", layer.opacity},
                                  {"drawings", drawings},
                                  {"cells", cells}};
    if (!layer.effects.isEmpty()) {
        QJsonArray effects;
        for (Effect const& e : layer.effects) {
            effects.append(effectToJson(e));
        }
        layerObject["effects"] = effects;
    }
    if (!layer.paletteOverrides.isEmpty()) {
        QJsonArray overrides;
        for (int palette : layer.paletteOverrides)
            overrides.append(palette);
        layerObject["paletteOverrides"] = overrides;
    }
    if (layer.nodePlaced)
        layerObject["node"] = QJsonArray{layer.nodePos.x(), layer.nodePos.y()};
    if (layer.nodeThumb > 0)
        layerObject["nodeThumb"] = layer.nodeThumb;
    if (!layer.customAttributes.isEmpty()) {
        QJsonArray custom;
        for (CustomAttribute const& a : layer.customAttributes)
            custom.append(QJsonObject{{"name", a.name}, {"value", a.value}, {"function", a.function}});
        layerObject["customAttributes"] = custom;
    }
    if (!layer.published.isEmpty()) {
        QJsonArray published;
        for (PublishedAttribute const& p : layer.published)
            published.append(QJsonArray{p.layer, p.value});
        layerObject["published"] = published;
    }
    if (!layer.enable3d)
        layerObject["enable3d"] = false;
    if (layer.alignment != 0)
        layerObject["alignment"] = layer.alignment;
    if (layer.turnBeforeAlignment)
        layerObject["turnBeforeAlignment"] = true;
    if (layer.flipHorizontal)
        layerObject["flipHorizontal"] = true;
    if (layer.lineMultiply != 1.0)
        layerObject["lineMultiply"] = layer.lineMultiply;
    if (layer.lineAdd != 0.0)
        layerObject["lineAdd"] = layer.lineAdd;
    if (layer.lineMin > 0.0)
        layerObject["lineMin"] = layer.lineMin;
    if (layer.lineMax > 0.0)
        layerObject["lineMax"] = layer.lineMax;
    if (layer.lineZoomIndependent)
        layerObject["lineZoomIndependent"] = true;
    if (!layer.readColour)
        layerObject["readColour"] = false;
    if (layer.tileHorizontal)
        layerObject["tileHorizontal"] = true;
    if (layer.tileVertical)
        layerObject["tileVertical"] = true;
    if (layer.bitmapMultiplier != 1.0)
        layerObject["bitmapMultiplier"] = layer.bitmapMultiplier;
    if (layer.flipVertical)
        layerObject["flipVertical"] = true;
    if (layer.clipping)
        layerObject["clip"] = QJsonArray{layer.clipX, layer.clipY};
    if (layer.antialiasing != Layer::AntialiasHigh)
        layerObject["antialiasing"] = layer.antialiasing;
    if (layer.antialiasExponent != 1.0)
        layerObject["antialiasExponent"] = layer.antialiasExponent;
    if (layer.textureFilter != Layer::FilterSmooth)
        layerObject["textureFilter"] = layer.textureFilter;
    if (!layer.colourSpace.isEmpty())
        layerObject["colourSpace"] = layer.colourSpace;
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
    if (!layer.soundFrames.isEmpty()) {
        QJsonArray frames;
        for (int frame : layer.soundFrames)
            frames.append(frame);
        layerObject["soundFrames"] = frames;
    }
    if (layer.embeddedPivot != Layer::PivotNotUsed)
        layerObject["embeddedPivot"] = layer.embeddedPivot;
    if (layer.separatePosition)
        layerObject["separatePosition"] = true;
    if (!layer.separateScale)
        layerObject["separateScale"] = false;
    if (layer.ignoreParentScale)
        layerObject["ignoreParentScale"] = true;
    if (layer.cameraAlignment != Layer::AlignNormal)
        layerObject["cameraAlignment"] = layer.cameraAlignment;
    if (!layer.cameraAvailable)
        layerObject["cameraAvailable"] = false;
    if (layer.angleLimited)
        layerObject["angleLimits"] = QJsonArray{layer.minAngle, layer.maxAngle};
    if (!layer.animatable)
        layerObject["animatable"] = false;
    if (layer.columnHidden)
        layerObject["columnHidden"] = true;
    if (layer.columnWidth > 0)
        layerObject["columnWidth"] = layer.columnWidth;
    if (layer.columnCollapsed)
        layerObject["columnCollapsed"] = true;
    if (!layer.notes.isEmpty()) {
        QJsonArray notes;
        for (auto it = layer.notes.constBegin(); it != layer.notes.constEnd(); ++it)
            notes.append(QJsonArray{it.key(), it.value()});
        layerObject["notes"] = notes;
    }
    if (!layer.ink.isEmpty()) {
        QJsonArray ink;
        for (AnnotationInk const& mark : layer.ink) {
            QJsonArray points;
            for (QPointF const& p : mark.points) {
                points.append(p.x());
                points.append(p.y());
            }
            ink.append(QJsonObject{{"colour", mark.colour.name(QColor::HexArgb)}, {"width", mark.width}, {"points", points}});
        }
        layerObject["ink"] = ink;
    }
    if (!layer.pictures.isEmpty()) {
        QJsonArray pictures;
        for (AnnotationPicture const& picture : layer.pictures)
            pictures.append(QJsonObject{{"frame", picture.frame}, {"frames", picture.frames}, {"png", QString::fromLatin1(picture.png.toBase64())}});
        layerObject["pictures"] = pictures;
    }
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
    if (layer.jointSmoothness > 0.0)
        layerObject["jointSmooth"] = layer.jointSmoothness;
    if (layer.hiddenFunctions != 0)
        layerObject["hiddenFunctions"] = layer.hiddenFunctions;
    if (!layer.links.isEmpty()) {
        QJsonArray links;
        for (ValueLink const& link : layer.links)
            links.append(QJsonArray{link.value, link.function});
        layerObject["links"] = links;
    }
    if (!layer.drivers.isEmpty()) {
        QJsonArray drivers;
        for (Driver const& d : layer.drivers)
            drivers.append(QJsonObject{{"value", d.value}, {"source", d.source}, {"sourceValue", d.sourceValue}, {"factor", d.factor}, {"offset", d.offset}});
        layerObject["drivers"] = drivers;
    }
    if (!layer.skin.isEmpty()) {
        QJsonArray dabs;
        for (SkinDab const& d : layer.skin)
            dabs.append(QJsonArray{d.bone, d.at.x(), d.at.y(), d.radius, d.strength});
        layerObject["skin"] = dabs;
    }
    if (layer.blend > Layer::BlendNormal && layer.blend < Layer::BlendCount) {
        static char const* const kBlends[Layer::BlendCount] = {"normal", "multiply", "screen", "overlay", "darken", "lighten", "colourDodge",
                                                               "colourBurn", "hardLight", "softLight", "difference", "exclusion", "add"};
        layerObject["blend"] = QLatin1String(kBlends[layer.blend]);
    }
    if (layer.bitmapResolution != 100)
        layerObject["bitmapResolution"] = layer.bitmapResolution;
    if (layer.hiddenSheets != 0)
        layerObject["hiddenSheets"] = layer.hiddenSheets;
    if (layer.ikExcluded)
        layerObject["ikExcluded"] = true;
    if (layer.ikNail)
        layerObject["ikNail"] = true;
    if (layer.ikHoldAngle)
        layerObject["ikHoldAngle"] = true;
    if (layer.ikLimited)
        layerObject["ikLimits"] = QJsonArray{layer.ikMin, layer.ikMax};
    return layerObject;
}

QJsonObject graphToJson(NodeGraph const& graph)
{
    static char const* const kinds[GraphNode::KindCount] = {"layer", "composite", "effect", "group", "groupIn", "groupOut", "display", "note", "waypoint"};
    QJsonArray nodes, cables;
    for (GraphNode const& n : graph.nodes) {
        QJsonObject node{{"id", n.id}, {"kind", kinds[n.kind]}, {"name", n.name}, {"ports", n.ports}, {"outPorts", n.outPorts},
                         {"group", n.group}, {"at", QJsonArray{n.at.x(), n.at.y()}}};
        if (n.kind == GraphNode::LayerNode)
            node["layer"] = n.layer;
        if (n.kind == GraphNode::LayerNode && n.symbolLayer)
            node["symbolLayer"] = n.symbolLayer;
        if (n.kind == GraphNode::LayerNode && n.sheets != 0xf)
            node["sheets"] = n.sheets;
        if (!n.enabled)
            node["enabled"] = false;
        if (n.passThrough)
            node["passThrough"] = true;
        if (n.asVector)
            node["asVector"] = true;
        if (n.outputZ != GraphNode::ZLeftmost)
            node["outputZ"] = n.outputZ;
        if (n.zPort != 0)
            node["zPort"] = n.zPort;
        if (n.thumb > 0)
            node["thumb"] = n.thumb;
        auto names = [](QMap<int, QString> const& map) {
            QJsonObject out;
            for (auto it = map.cbegin(); it != map.cend(); ++it)
                if (!it.value().trimmed().isEmpty())
                    out[QString::number(it.key())] = it.value();
            return out;
        };
        if (!names(n.portNames).isEmpty())
            node["portNames"] = names(n.portNames);
        if (!names(n.portFilters).isEmpty())
            node["portFilters"] = names(n.portFilters);
        if (n.kind == GraphNode::EffectNode) {
            node["effect"] = effectToJson(n.effect);
            if (!n.amountKeys.isEmpty()) {
                QJsonArray keys;
                for (auto it = n.amountKeys.cbegin(); it != n.amountKeys.cend(); ++it)
                    keys.append(QJsonArray{it.key(), it.value()});
                node["amountKeys"] = keys;
            }
        }
        nodes.append(node);
    }
    for (GraphCable const& c : graph.cables)
        cables.append(QJsonArray{c.from, c.fromPort, c.to, c.toPort});
    QJsonObject out{{"nodes", nodes}, {"cables", cables}};
    if (!graph.backdrops.isEmpty())
        out["backdrops"] = backdropsToJson(graph.backdrops);
    return out;
}

QJsonArray backdropsToJson(QVector<Backdrop> const& backdrops)
{
    QJsonArray out;
    for (Backdrop const& b : backdrops)
        out.append(QJsonObject{{"group", b.group}, {"rect", QJsonArray{b.rect.x(), b.rect.y(), b.rect.width(), b.rect.height()}}, {"title", b.title},
                               {"text", b.text}, {"color", b.color.name(QColor::HexArgb)}, {"titleSize", b.titleSize}});
    return out;
}

}  // namespace scene_write
