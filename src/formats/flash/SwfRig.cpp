// Eclipse - a Flash movie read in tracks, made into a template: a peg over all of it, and each track a drawing layer on a peg.
#include "formats/flash/SwfImport.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

#include "formats/flash/FlashFilter.h"
#include "model/scene/Scene.h"

namespace SwfImport {

namespace {

QString tr(char const* text)
{
    return QCoreApplication::translate("SwfImport", text);
}

bool samePlace(Pose const& a, Pose const& b)
{
    return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01 && std::abs(a.scaleX - b.scaleX) < 1e-4 && std::abs(a.scaleY - b.scaleY) < 1e-4
           && std::abs(a.rotation - b.rotation) < 0.01 && std::abs(a.skew - b.skew) < 0.01;
}

bool sameOpacity(Pose const& a, Pose const& b)
{
    return std::abs(a.opacity - b.opacity) < 0.002;
}

// As few keyframes as give each shown frame its pose: one where the poses stop going in a straight line (hidden frames: any).
QVector<Keyframe> keysFor(QVector<Pose> const& poses, QVector<bool> const& shown, bool (*same)(Pose const&, Pose const&))
{
    QVector<Keyframe> keys;
    int const n = int(poses.size());
    int a = 0;
    while (a < n && !shown[a])
        ++a;
    while (a < n) {
        Keyframe key;
        key.frame = a;
        key.pose = poses[a];
        keys.push_back(key);
        int b = -1;
        for (int c = a + 1; c < n && c - a <= 256; ++c) {
            if (!shown[c])
                continue;
            bool straight = true;
            for (int g = a + 1; g < c && straight; ++g)
                straight = !shown[g] || same(poses[g], Pose::lerp(poses[a], poses[c], double(g - a) / (c - a)));
            if (!straight)
                break;
            b = c;
        }
        if (b < 0)
            break;
        a = b;
    }
    return keys;
}

}  // namespace

Scene rig(Movie const& movie, QString const& name, QSize frame, bool groups)
{
    Scene piece;
    piece.layers.clear();       // (a new scene starts with a drawing layer)
    piece.width = frame.width();
    piece.height = frame.height();
    int const frames = std::max(1, movie.frameCount);
    piece.setFrameCount(frames);
    double const scale = std::min(double(frame.width()) / movie.size.width(), double(frame.height()) / movie.size.height());
    // Its gradients as swatches.
    QVector<int> swatches;
    for (Swatch const& gradient : movie.gradients) {
        int const id = piece.addSwatch(gradient.name, gradient.color);
        for (Swatch& s : piece.palette)
            if (s.id == id) {
                s.kind = gradient.kind;
                s.stops = gradient.stops;
            }
        swatches.push_back(id);
    }
    if (movie.background.isValid()) {
        int const card = piece.addLayer(tr("%1 Background").arg(name), -1, Layer::ColourCard);
        piece.layers[card].cardColor = movie.background;
    }
    QVector<int> drawingIds, pegIds;
    for (Track const& track : movie.tracks) {
        int const at = piece.addLayer(track.name, -1, Layer::DrawingLayer);
        {
            Layer& layer = piece.layers[at];
            layer.pivot = QPointF();
            layer.drawings = track.drawings;
            for (Drawing& d : layer.drawings) {
                for (Fill& fill : d.fills)
                    fill.swatch = fill.swatch >= 0 && fill.swatch < swatches.size() ? swatches[fill.swatch] : Scene::kNoSwatch;
                d.pinned = true;
            }
            for (int f = 0; f < std::min(frames, int(track.cells.size())); ++f)
                layer.cells[f] = track.cells[f];
            layer.visible = !track.mask;
            Flash::applyLook(layer, track.blend, track.filters, scale);
            // A fade: keyframes of how opaque it is, on the drawing layer itself.
            QVector<Pose> poses(frames);
            QVector<bool> shown(frames, false);
            bool fades = false;
            for (int f = 0; f < std::min(frames, int(track.cells.size())); ++f) {
                shown[f] = track.cells[f] >= 0;
                poses[f].opacity = track.opacity.value(f, 1.0);
                fades = fades || (shown[f] && poses[f].opacity < 0.999);
            }
            if (fades)
                layer.keys = keysFor(poses, shown, sameOpacity);
            drawingIds.push_back(layer.id);
        }
        // Where it is on each frame, on a peg of its own (a turn kept going the way it went, not jumping back by a whole turn).
        int const pegAt = piece.addLayer(track.name + QStringLiteral("-P"), -1, Layer::Peg);
        Layer& peg = piece.layers[pegAt];
        peg.pivot = QPointF();
        QVector<Pose> poses(frames);
        QVector<bool> shown(frames, false);
        double lastTurn = 0.0;
        bool turned = false;
        for (int f = 0; f < std::min(frames, int(track.places.size())); ++f) {
            if (f >= track.cells.size() || track.cells[f] < 0 || !Layer::poseOf(track.places[f], poses[f]))
                continue;
            if (turned)
                poses[f].rotation += 360.0 * std::round((lastTurn - poses[f].rotation) / 360.0);
            lastTurn = poses[f].rotation;
            turned = true;
            shown[f] = true;
        }
        peg.keys = keysFor(poses, shown, samePlace);
        int const pegId = peg.id;
        piece.layers[at].parent = pegId;
        if (groups) {
            int const group = piece.addLayer(track.name + QStringLiteral("-G"), -1, Layer::Group);
            piece.layers[piece.layerIndex(pegId)].parent = piece.layers[group].id;
            pegIds.push_back(piece.layers[group].id);
        } else {
            pegIds.push_back(pegId);
        }
    }
    // The masks: each layer cut by the drawings of the masks over it.
    for (int i = 0; i < movie.tracks.size(); ++i)
        for (int m : movie.tracks[i].maskedBy) {
            if (m < 0 || m >= drawingIds.size())
                continue;
            Effect cut = Effect::made(Effect::Cutter);
            cut.matte = drawingIds[m];
            piece.layers[piece.layerIndex(drawingIds[i])].effects.push_back(cut);
        }
    // Over everything, a peg placing the movie in the middle of the frame, as big as fits.
    int const top = piece.addLayer(name, -1, Layer::Peg);
    Layer& all = piece.layers[top];
    all.pivot = QPointF();
    Keyframe key;
    key.pose.scaleX = key.pose.scaleY = scale;
    key.pose.x = (frame.width() - movie.size.width() * scale) / 2.0;
    key.pose.y = (frame.height() - movie.size.height() * scale) / 2.0;
    all.keys = {key};
    int const topId = all.id;
    for (int id : std::as_const(pegIds))
        piece.layers[piece.layerIndex(id)].parent = topId;
    return piece;
}

}  // namespace SwfImport
