#include "board/ui/export/BoardExport.h"
#include "board/ui/export/BoardExportFiles.h"
#include "board/model/BoardProject.h"
#include "board/ui/BoardCameraCanvas.h"
#include "model/scene/Scene.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <tuple>

namespace {
using Track = std::tuple<int, QString, BoardLayer::Kind, bool, bool>;

Track trackFor(BoardLayer const& layer, int row)
{
    return {-row, layer.name, layer.kind, layer.visible, layer.locked};
}

QString tr(char const* message) { return QCoreApplication::translate("BoardExport", message); }

bool addCamera(Scene& scene, BoardProject const& project, QVector<BoardSpan> const& spans)
{
    if (std::none_of(spans.cbegin(), spans.cend(), [&](BoardSpan const& span) {
            return !project.shots[span.shot].panels[span.panel].camera.isEmpty();
        }))
        return true;
    Layer& lens = scene.layers[scene.addLayer(QStringLiteral("Camera"), -1, Layer::Camera)];
    scene.activeCamera = lens.id;
    for (auto const& span : spans) {
        auto const& keys = project.shots[span.shot].panels[span.panel].camera;
        int previous = -1;
        auto push = [&](int local) {
            if (previous == local)
                return true;
            if (lens.keys.size() >= 200000)
                return false;
            Keyframe key;
            key.frame = span.start + local;
            key.pose = boardCameraLayerPose(BoardCamera::poseAt(keys, local));
            key.tween = local < span.length - 1;
            lens.keys.append(std::move(key));
            previous = local;
            return true;
        };
        if (!push(0))
            return false;
        for (qsizetype i = 0; i < keys.size(); ++i) {
            if (i > 0 && keys[i - 1].pose.zoom != keys[i].pose.zoom)
                for (int at = keys[i - 1].frame + 1; at < keys[i].frame; ++at)
                    if (!push(at))
                        return false;
            if (!push(keys[i].frame))
                return false;
        }
        if (!push(span.length - 1))
            return false;
    }
    return true;
}
}

bool BoardExport::animation(BoardProject const& project, QString const& projectFile, QString const& folder, QString* error)
{
    auto const spans = project.spans();
    if (spans.isEmpty())
        return fail(error, tr("The board has no panels."));
    QMap<Track, int> tracks;
    for (auto const& span : spans) {
        auto const& layers = project.shots[span.shot].panels[span.panel].layers;
        for (int l = 0; l < layers.size(); ++l)
            tracks.insert(trackFor(layers[l], l), -1);
    }
    int const frames = spans.last().start + spans.last().length;
    if (frames > 1000000 || qint64(frames) * (tracks.size() + 2) > 20000000)
        return fail(error, tr("The board has too many exposures for one Animation scene."));
    Scene scene;
    scene.layers.clear();
    scene.width = project.width;
    scene.height = project.height;
    scene.fps = project.fps;
    scene.background = QColor(238, 238, 238);
    scene.setFrameCount(frames);
    scene.storyboard = project.timeline();
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        auto const& [row, name, kind, visible, locked] = it.key();
        it.value() = scene.addLayer(name);
        auto& layer = scene.layers[it.value()];
        layer.bitmap = kind == BoardLayer::Kind::Bitmap;
        if (layer.bitmap)
            layer.bitmapArt = (1 << Drawing::SheetCount) - 1;
        layer.visible = visible;
        layer.locked = locked;
    }
    for (auto const& span : spans) {
        auto const& layers = project.shots[span.shot].panels[span.panel].layers;
        for (int l = 0; l < layers.size(); ++l) {
            auto& track = scene.layers[tracks.value(trackFor(layers[l], l))];
            int const drawing = int(track.drawings.size());
            track.drawings.append(layers[l].drawing);
            std::fill_n(track.cells.begin() + span.start, span.length, drawing);
        }
    }
    if (!addCamera(scene, project, spans))
        return fail(error, tr("The board has too many camera keys for one Animation scene."));
    if (!project.sound.asset.isEmpty()) {
        QString const path = project.audioPath(projectFile);
        if (!QFileInfo(path).isFile())
            return fail(error, tr("The sound file is missing: %1").arg(project.sound.name));
        auto& sound = scene.layers[scene.addLayer(project.sound.name, -1, Layer::Sound)];
        sound.visible = !project.sound.muted;
        SoundClip clip;
        clip.file = path;
        clip.start = project.sound.start;
        clip.stop = project.sound.start + project.sound.length;
        sound.soundClips.append(std::move(clip));
    }
    return publishFolder(folder, [&](QDir const& staging) {
        return scene.saveProject(staging.filePath(QFileInfo(folder).fileName() + QStringLiteral(".ecl")), error);
    }, error);
}
