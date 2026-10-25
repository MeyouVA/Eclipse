// Eclipse - the Flash movie writer: each layer at depths of its own, its drawing a shape placed (and moved) frame by frame.
#include "formats/flash/SwfExport.h"

#include <QFile>
#include <QMap>
#include <QObject>

#include <algorithm>
#include <cmath>

#include "formats/flash/SwfExportPrivate.h"
#include "model/geometry/Deform.h"
#include "model/scene/Scene.h"
#include "render/effects/Effects.h"

using namespace swfout;

namespace {

// What stands at a depth on a frame.
struct Placement {
    int character = 0;
    QTransform matrix;
    ColourEffect colour;
    QVector<Flash::Filter> filters;
    int blend = 0;      // Flash's number (0: normal)
    int clip = 0;       // a mask: the depth it covers up to
    bool sameLook(Placement const& o) const
    {
        return matrix == o.matrix && colour == o.colour && blend == o.blend && filterList(filters) == filterList(o.filters);
    }
};

int flashBlend(int blend)
{
    switch (blend) {
    case Layer::BlendMultiply: return 3;
    case Layer::BlendScreen: return 4;
    case Layer::BlendLighten: return 5;
    case Layer::BlendDarken: return 6;
    case Layer::BlendDifference: return 7;
    case Layer::BlendAdd: return 8;
    case Layer::BlendOverlay: return 13;
    case Layer::BlendHardLight: return 14;
    default: return -1;
    }
}

QByteArray placeTag(int depth, Placement const& p, bool move, bool newCharacter, bool hadLook)
{
    bool const look = !p.filters.isEmpty() || p.blend > 1 || hadLook;
    QByteArray body;
    int flags = 0x04 | 0x08;       // a matrix and a colour effect, each time
    if (move)
        flags |= 0x01;
    if (newCharacter)
        flags |= 0x02;
    if (p.clip > 0)
        flags |= 0x40;
    u8(body, flags);
    if (look)
        u8(body, 0x01 | 0x02);     // filters and a blend mode
    u16(body, quint16(depth));
    if (newCharacter)
        u16(body, quint16(p.character));
    body += matrix(p.matrix);
    body += colourEffect(p.colour);
    if (p.clip > 0)
        u16(body, quint16(p.clip));
    if (look) {
        body += filterList(p.filters);
        u8(body, std::max(1, p.blend));
    }
    return tag(look ? 70 : 26, body);
}

// Can the layer's drawing on that frame be one shape, moved: nothing bends it, and the camera sees it flat?
bool rigid(Scene const& scene, int l, int frame, Pose const& pose, QTransform const& world)
{
    Layer const& layer = scene.layers[l];
    if (world.type() == QTransform::TxProject || layer.morphing(frame) || layer.bendOnBones)
        return false;
    if (!layer.showsSymbol()) {
        int const index = layer.drawingAt(frame);
        if (index < 0 || index >= layer.drawings.size() || !layer.drawings[index].model.isEmpty())
            return false;
        if (Deform::isWarped(layer.warp, layer.warpClosed, pose.warp) || Deform::isBent(layer.bones, pose.bend))
            return false;
    }
    for (int a = scene.parentIndex(l), guard = 0; a >= 0 && guard < 64; a = scene.parentIndex(a), ++guard)
        if (scene.layers[a].hasWarp())
            return false;
    return true;
}

}  // namespace

namespace SwfExport {

QByteArray encode(Scene const& scene, Options const& options, Report* report)
{
    Report local;
    Report& said = report ? *report : local;
    said = Report();
    int const count = scene.frameCount();
    int const first = std::clamp(options.first, 0, std::max(0, count - 1));
    int const last = options.last < 0 ? count - 1 : std::clamp(options.last, first, count - 1);
    double const fps = std::clamp(options.fps > 0.0 ? options.fps : scene.fps, 1.0, 255.0);
    Shapes shapes(scene, options, said);
    auto leftOut = [&](QString const& what) { shapes.leftOut(what); };
    QTransform const twips = QTransform::fromScale(20.0, 20.0), fromTwips = QTransform::fromScale(1.0 / 20.0, 1.0 / 20.0);
    QRect const frameTwips(0, 0, scene.width * 20, scene.height * 20);

    QByteArray tags;
    {
        QByteArray attributes;
        u32(attributes, 0);
        tags += tag(69, attributes);
    }
    if (options.protect)
        tags += tag(24, QByteArray());
    {
        QByteArray background;
        u8(background, scene.background.red());
        u8(background, scene.background.green());
        u8(background, scene.background.blue());
        tags += tag(9, background);
    }
    SoundStream sound;
    QString soundProblem;
    if (options.sound && soundStream(scene, first, last, fps, sound, &soundProblem)) {
        tags += tag(45, sound.head);
        said.sound = true;
        if (!soundProblem.isEmpty())
            leftOut(soundProblem);
    }

    QHash<QByteArray, int> rigidShapes;     // a layer's drawing, as it is: its shape
    QMap<int, Placement> before;            // by depth, on the frame before
    for (int f = first; f <= last; ++f) {
        QTransform const camera = scene.cameraTransform(f);
        // The shape of what a layer shows on this frame, and where it goes.
        auto shapeOf = [&](int l, Placement& p) {
            Layer const& layer = scene.layers[l];
            int const index = layer.drawingAt(f);
            Pose const pose = scene.poseOf(l, f);
            QTransform const world = scene.worldTransform(l, f) * camera;
            if (rigid(scene, l, f, pose, world)) {
                double const grows = std::sqrt(std::abs(world.determinant()));
                QByteArray key = (layer.showsSymbol() ? "s" + QByteArray::number(layer.symbolId) : "l" + QByteArray::number(layer.id)) + ':'
                                 + QByteArray::number(index);
                if (options.fixedLineWidth)
                    key += ':' + QByteArray::number(grows, 'f', 3);
                auto found = rigidShapes.constFind(key);
                if (found == rigidShapes.cend()) {
                    Drawing const d = layer.showsSymbol() ? scene.symbolPicture(layer.symbolId, index) : layer.drawings[index];
                    found = rigidShapes.insert(key, shapes.drawing(d, twips, grows));
                }
                p.character = *found;
                p.matrix = fromTwips * world * twips;
            } else {
                // Bent, warped or turned in space: drawn as it shows on this frame, where it shows.
                Drawing const d = layer.showsSymbol() ? scene.symbolPicture(layer.symbolId, index)
                                                      : Deform::ridden(scene, l, f, Deform::shown(layer.morphing(f) ? layer.shownAt(f) : layer.drawings[index], layer, pose, index));
                p.character = shapes.drawing(d, world * twips, std::sqrt(std::abs(world.determinant())));
                p.matrix = QTransform();
            }
            return p.character != 0;
        };

        QMap<int, Placement> now;
        int rank = 0;
        for (int l : scene.drawOrder(f)) {
            if (!scene.enabled(l))
                continue;
            Layer const& layer = scene.layers[l];
            int const depth = 2 * rank + 2;     // (the depth under it for its mask)
            ++rank;
            if (layer.kind == Layer::ColourCard) {
                Placement p;
                p.character = shapes.card(layer.cardColor, frameTwips);
                if (options.transparency)
                    p.colour.mult[3] = int(std::lround(256.0 * std::clamp(layer.opacity, 0.0, 1.0)));
                now.insert(depth, p);
                continue;
            }
            if (!layer.holdsDrawings() || layer.drawingAt(f) == Layer::kNoDrawing)
                continue;
            Placement p;
            if (!shapeOf(l, p))
                continue;
            Pose const pose = scene.poseOf(l, f);
            double const opacity = std::clamp(layer.opacity * pose.opacity, 0.0, 1.0);
            if (opacity < 1.0 && options.transparency)
                p.colour.mult[3] = int(std::lround(256.0 * opacity));
            if (layer.blend != Layer::BlendNormal) {
                int const b = flashBlend(layer.blend);
                if (!options.blendModes)
                    leftOut(QObject::tr("blend modes (switched off)"));
                else if (b < 0)
                    leftOut(QObject::tr("blend modes Flash does not have"));
                else
                    p.blend = b;
            }
            bool cut = false;
            for (int i = 0; i < layer.effects.size(); ++i) {
                Effect const& e = layer.effects[i];
                if (!e.enabled)
                    continue;
                double const amount = layer.effectAmount(i, pose);
                auto off = [&](bool on, QString const& what) {
                    if (!on)
                        leftOut(QObject::tr("%1 (switched off)").arg(what));
                    return on;
                };
                if (e.type == Effect::Blur) {
                    if (off(options.blurs, QObject::tr("blurs")))
                        p.filters.push_back({QStringLiteral("BlurFilter"), amount * 2.0, amount * 2.0, Qt::black, 1.0, 0.0, 45.0, false});
                } else if (e.type == Effect::Glow) {
                    if (off(options.glows, QObject::tr("glows")))
                        p.filters.push_back({QStringLiteral("GlowFilter"), amount * 2.0, amount * 2.0, e.color, 1.0, 0.0, 45.0, false});
                } else if (e.type == Effect::Shadow) {
                    if (off(options.shadows, QObject::tr("shadows")))
                        p.filters.push_back({QStringLiteral("DropShadowFilter"), amount * 2.0, amount * 2.0, e.color, 1.0,
                                             std::hypot(e.offset.x(), e.offset.y()), std::atan2(e.offset.y(), e.offset.x()) * 180.0 / 3.14159265358979323846, false});
                } else if (e.type == Effect::Transparency) {
                    if (off(options.transparency, QObject::tr("transparency")))
                        p.colour.mult[3] = p.colour.mult[3] * int(std::lround(256.0 * (1.0 - std::clamp(amount, 0.0, 1.0)))) / 256;
                } else if (e.type == Effect::Tint || e.type == Effect::ColourScale) {
                    if (!off(options.colourEffects, QObject::tr("colour effects")))
                        continue;
                    double const a = std::clamp(amount, 0.0, 1.0);
                    int const c[3] = {e.color.red(), e.color.green(), e.color.blue()};
                    ColourEffect fx;
                    for (int k = 0; k < 3; ++k) {
                        if (e.type == Effect::Tint) {
                            // Towards the colour, so far.
                            fx.mult[k] = int(std::lround(256.0 * (1.0 - a)));
                            fx.add[k] = int(std::lround(c[k] * a));
                        } else {
                            // Each part times the colour's, so far.
                            fx.mult[k] = int(std::lround(256.0 * (1.0 - a + a * c[k] / 255.0)));
                        }
                    }
                    p.colour = fx.after(p.colour);
                } else if (e.type == Effect::Cutter && !cut) {
                    if (!off(options.masks, QObject::tr("cutters")))
                        continue;
                    int const matte = scene.layerIndex(e.matte);
                    if (e.invert) {
                        leftOut(QObject::tr("inverted cutters"));
                        continue;
                    }
                    // A mask under the layer, covering it alone: the matte's drawing where the matte is.
                    Placement mask;
                    if (matte >= 0 && scene.layers[matte].holdsDrawings() && scene.layers[matte].drawingAt(f) != Layer::kNoDrawing && shapeOf(matte, mask)) {
                        mask.clip = depth;
                        now.insert(depth - 1, mask);
                    } else {
                        p.character = 0;    // cut by nothing: nothing of it shows
                    }
                    cut = true;
                } else if (e.type == Effect::Cutter) {
                    leftOut(QObject::tr("more than one cutter on a layer"));
                } else {
                    leftOut(Effects::name(e, scene));
                }
            }
            if (p.character != 0)
                now.insert(depth, p);
        }
        tags += shapes.take();
        // The frame's labels.
        for (Scene::Marker const& m : scene.markers)
            if (m.frame == f && !m.name.isEmpty())
                tags += tag(43, m.name.toUtf8() + QByteArray(1, '\0'));
        // What changed at each depth since the frame before.
        QList<int> depths = before.keys();
        for (int d : now.keys())
            if (!before.contains(d))
                depths.push_back(d);
        std::sort(depths.begin(), depths.end());
        for (int d : std::as_const(depths)) {
            auto const was = before.constFind(d);
            auto const is = now.constFind(d);
            QByteArray remove;
            u16(remove, quint16(d));
            if (is == now.cend()) {
                tags += tag(28, remove);
            } else if (was == before.cend()) {
                tags += placeTag(d, *is, false, true, false);
            } else if (was->clip != is->clip) {
                tags += tag(28, remove);
                tags += placeTag(d, *is, false, true, false);
            } else if (was->character != is->character || !was->sameLook(*is)) {
                bool const hadLook = !was->filters.isEmpty() || was->blend > 1;
                tags += placeTag(d, *is, true, was->character != is->character, hadLook);
            }
        }
        before = now;
        if (said.sound && f - first < sound.blocks.size() && !sound.blocks[f - first].isEmpty())
            tags += tag(19, sound.blocks[f - first]);
        tags += tag(1, QByteArray());
        ++said.frames;
    }
    tags += tag(0, QByteArray());
    said.skippedEffects = !said.leftOut.isEmpty();

    QByteArray movie;
    movie += rect(0, scene.width * 20, 0, scene.height * 20);
    u16(movie, quint16(std::clamp(int(std::lround(fps * 256.0)), 256, 65535)));
    u16(movie, quint16(std::min(said.frames, 65535)));
    movie += tags;
    QByteArray file(options.compress ? "CWS" : "FWS");
    u8(file, 10);
    u32(file, quint32(8 + movie.size()));
    return file + (options.compress ? qCompress(movie, 9).mid(4) : movie);
}

QByteArray encode(Scene const& scene, int first, int last, Report* report)
{
    Options options;
    options.first = first;
    options.last = last;
    return encode(scene, options, report);
}

bool write(Scene const& scene, QString const& path, Options const& options, QString* error, Report* report)
{
    QByteArray const bytes = encode(scene, options, report);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool write(Scene const& scene, QString const& path, QString* error, Report* report)
{
    return write(scene, path, Options(), error, report);
}

}  // namespace SwfExport
