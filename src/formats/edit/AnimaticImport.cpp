// Eclipse - an animatic brought into a scene (see AnimaticImport.h).
#include "formats/edit/AnimaticImport.h"

#include "model/media/MediaFormats.h"
#include "model/scene/Scene.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace AnimaticImport {

using EditTimeline::Clip;
using EditTimeline::Sequence;

QStringList clipNames(Sequence const& sequence)
{
    QStringList names;
    for (Clip const& clip : sequence.videoClips())
        if (!clip.name.isEmpty() && !names.contains(clip.name, Qt::CaseInsensitive))
            names << clip.name;
    return names;
}

bool span(Sequence const& sequence, Options const& options, int* in, int* duration, QString* error)
{
    int from = 0, length = sequence.length();
    if (options.timespan == Options::SceneClip || options.timespan == Options::OtherClip) {
        QString const name = options.timespan == Options::SceneClip ? options.sceneName : options.clip;
        Clip const* clip = sequence.clipNamed(name);
        if (!clip) {
            if (error)
                *error = options.timespan == Options::SceneClip
                             ? QStringLiteral("There is no clip named %1 (as the scene is) in the sequence: choose another clip, or give the timespan.").arg(name)
                             : QStringLiteral("There is no clip named %1 in the sequence.").arg(name);
            return false;
        }
        from = clip->start;
        length = clip->length();
    } else if (options.timespan == Options::Custom) {
        from = std::max(0, options.in);
        length = options.duration;
    }
    if (length <= 0) {
        if (error)
            *error = QStringLiteral("The timespan is empty.");
        return false;
    }
    *in = from;
    *duration = length;
    return true;
}

bool isPicture(QString const& file)
{
    QByteArray const suffix = QFileInfo(file).suffix().toLower().toLatin1();
    static QList<QByteArray> const extra = {"tga", "exr", "psd", "dpx", "sgi", "tif", "tiff"};
    return QImageReader::supportedImageFormats().contains(suffix) || extra.contains(suffix);
}

QStringList imageSequence(QString const& folder)
{
    if (folder.isEmpty())
        return {};
    QDir const dir(folder);
    QStringList files;
    for (QFileInfo const& info : dir.entryInfoList(QDir::Files, QDir::NoSort))
        if (isPicture(info.fileName()))
            files << info.absoluteFilePath();
    // shot_2 before shot_10.
    QCollator order;
    order.setNumericMode(true);
    order.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&](QString const& a, QString const& b) { return order.compare(a, b) < 0; });
    return files;
}

QString locate(QString const& file, QStringList const& folders)
{
    if (file.isEmpty())
        return QString();
    if (QFileInfo::exists(file))
        return QFileInfo(file).absoluteFilePath();
    QString const name = QFileInfo(QDir::fromNativeSeparators(file)).fileName();
    for (QString const& folder : folders) {
        QString const there = QDir(folder).absoluteFilePath(name);
        if (QFileInfo::exists(there))
            return there;
    }
    return QString();
}

QStringList missingMedia(Sequence const& sequence, QStringList const& folders)
{
    QStringList missing;
    for (QString const& file : sequence.files())
        if (locate(file, folders).isEmpty())
            missing << file;
    return missing;
}

static Drawing pictureDrawing(QString const& file, Scene const& scene)
{
    Drawing drawing;
    QImage const image = readPictureFile(file, false);
    if (image.isNull())
        return drawing;
    Picture picture;
    picture.image = image;
    picture.file = QFileInfo(file).absoluteFilePath();
    picture.size = image.size();
    // Fitted to the frame, in the middle.
    double const scale = std::min(double(scene.width) / image.width(), double(scene.height) / image.height());
    picture.placement = QTransform::fromScale(scale, scale)
                        * QTransform::fromTranslate((scene.width - image.width() * scale) / 2.0, (scene.height - image.height() * scale) / 2.0);
    drawing.pictures << picture;
    drawing.name = QFileInfo(file).completeBaseName();
    return drawing;
}

bool apply(Scene& scene, Sequence const& sequence, Options const& options, Result* result, QString* error)
{
    Result r;
    if (!span(sequence, options, &r.in, &r.duration, error))
        return false;
    // Frames of the sequence to frames of the scene.
    if (options.rate && sequence.fps > 0.0)
        scene.fps = sequence.fps;
    double const ratio = options.rate || sequence.fps <= 0.0 ? 1.0 : scene.fps / sequence.fps;
    auto toScene = [&](int frame) { return int(std::lround((frame - r.in) * ratio)); };
    int const frames = std::max(1, int(std::lround(r.duration * ratio)));
    if (options.length || scene.frameCount() < frames)
        scene.setFrameCount(frames);
    auto inSpan = [&](int frame) { return frame >= r.in && frame < r.in + r.duration; };

    // ---- the reference images ----
    QVector<QPair<int, QString>> pictures;     // (the scene's frame it starts on, the file)
    if (options.images == Options::ClipPictures || (options.images != Options::NoImages && options.imageFolder.isEmpty())) {
        for (Clip const& clip : sequence.videoClips()) {
            if (clip.end <= r.in || clip.start >= r.in + r.duration)
                continue;
            QString const file = locate(clip.file, options.searchFolders);
            if (!clip.file.isEmpty() && file.isEmpty() && !r.missing.contains(clip.file))
                r.missing << clip.file;
            if (!file.isEmpty() && isPicture(file))
                pictures << qMakePair(std::max(0, toScene(clip.start)), file);
        }
    } else if (options.images != Options::NoImages) {
        QStringList const files = imageSequence(options.imageFolder);
        QVector<int> starts;
        if (options.images == Options::AllImages) {
            for (int f = r.in; f < r.in + r.duration; ++f)
                starts << f;
        } else if (options.images == Options::Markers) {
            for (EditTimeline::Marker const& m : sequence.markers)
                if (inSpan(m.frame))
                    starts << m.frame;
            for (Clip const& clip : sequence.videoClips())
                for (EditTimeline::Marker const& m : clip.markers) {
                    int const at = clip.start + (m.frame - clip.in);
                    if (inSpan(at))
                        starts << at;
                }
        } else {
            for (Clip const& clip : sequence.videoClips())
                if (clip.end > r.in && clip.start < r.in + r.duration)
                    starts << std::max(r.in, clip.start);
        }
        if (starts.isEmpty() || starts.first() != r.in)
            starts.prepend(r.in);
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (int f : std::as_const(starts))
            if (f >= 0 && f < files.size())
                pictures << qMakePair(toScene(f), files[f]);
    }
    std::sort(pictures.begin(), pictures.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    if (!pictures.isEmpty()) {
        QString const name = QStringLiteral("Ref_Images");
        int at = -1;
        for (int l = 0; l < scene.layers.size(); ++l)
            if (scene.layers[l].name == name && scene.layers[l].kind == Layer::DrawingLayer)
                at = l;
        if (at >= 0 && options.replaceRefImages) {
            Layer& old = scene.layers[at];
            old.drawings.clear();
            old.cells.fill(Layer::kNoDrawing, scene.frameCount());
        } else {
            // (At the back, under the artwork; a second one is named Ref_Images_1 ...)
            at = scene.addLayer(name, 0, Layer::DrawingLayer);
        }
        Layer& layer = scene.layers[at];
        for (int p = 0; p < pictures.size(); ++p) {
            Drawing drawing = pictureDrawing(pictures[p].second, scene);
            if (drawing.pictures.isEmpty())
                continue;
            int const index = int(layer.drawings.size());
            layer.drawings << drawing;
            int const from = std::clamp(pictures[p].first, 0, scene.frameCount());
            int const to = p + 1 < pictures.size() ? std::clamp(pictures[p + 1].first, 0, scene.frameCount()) : scene.frameCount();
            for (int f = from; f < to; ++f)
                layer.cells[f] = index;
            ++r.images;
        }
        r.imageLayer = at;
    }

    // ---- the sound ----
    if (options.sound) {
        QVector<QVector<SoundClip>> tracks(sequence.tracks(true));
        QSet<QString> taken;     // (a stereo pair is two clips of one file: one is enough)
        for (Clip const& clip : sequence.audioClips()) {
            if (!clip.enabled || clip.end <= r.in || clip.start >= r.in + r.duration)
                continue;
            QString const file = locate(clip.file, options.searchFolders);
            if (file.isEmpty()) {
                if (!clip.file.isEmpty() && !r.missing.contains(clip.file))
                    r.missing << clip.file;
                continue;
            }
            QString const key = QStringLiteral("%1|%2|%3").arg(file.toLower()).arg(clip.start).arg(clip.in);
            if (taken.contains(key))
                continue;
            taken.insert(key);
            double const fps = sequence.fps > 0.0 ? sequence.fps : 24.0;
            // Cut at the span's start: it starts further into its file.
            int const start = std::max(clip.start, r.in);
            SoundClip sound;
            sound.file = file;
            sound.start = toScene(start);
            sound.stop = std::min(frames, toScene(std::min(clip.end, r.in + r.duration)));
            sound.offset = (clip.in + (start - clip.start)) / fps;
            sound.length = (std::min(clip.end, r.in + r.duration) - start) / fps;
            tracks[clip.track] << sound;
        }
        for (int t = 0; t < tracks.size(); ++t) {
            if (tracks[t].isEmpty())
                continue;
            int const at = scene.addLayer(tracks.size() > 1 ? QStringLiteral("Sound_A%1").arg(t + 1) : QStringLiteral("Sound"), -1, Layer::Sound);
            scene.layers[at].soundFile = tracks[t].first().file;
            scene.layers[at].soundStart = 0;
            scene.layers[at].soundClips = tracks[t];
            r.soundLayers << at;
        }
    }

    // ---- the storyboard ----
    if (options.shots) {
        Storyboard board;
        board.title = sequence.name;
        board.subtitle = scene.storyboard.subtitle;
        board.captionFields = scene.storyboard.captionFields;
        int track0 = 0;     // (shots from the lowest video track that has clips in the span)
        for (int t = sequence.tracks(false) - 1; t >= 0; --t)
            for (Clip const& clip : sequence.clips)
                if (!clip.audio && clip.track == t && clip.end > r.in && clip.start < r.in + r.duration)
                    track0 = t;
        int at = 0;
        for (Clip const& clip : sequence.videoClips()) {
            if (clip.track != track0 || clip.end <= r.in || clip.start >= r.in + r.duration)
                continue;
            int const from = std::max(0, toScene(std::max(clip.start, r.in)));
            int const to = std::min(frames, toScene(std::min(clip.end, r.in + r.duration)));
            if (to <= at)
                continue;
            if (from > at && !board.shots.isEmpty())
                board.shots.last().panels.last().length += from - at;   // (a gap: the shot before held over it)
            else if (from > at)
                board.start = from;
            Storyboard::Shot shot;
            shot.name = QFileInfo(clip.name).completeBaseName().isEmpty() ? clip.name : QFileInfo(clip.name).completeBaseName();
            shot.clip = clip.name;
            // Its panels: one from each of its markers on.
            QVector<int> cuts{std::max(from, at)};
            for (EditTimeline::Marker const& m : clip.markers) {
                int const cut = toScene(clip.start + (m.frame - clip.in));
                if (cut > cuts.first() && cut < to)
                    cuts << cut;
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
            for (int c = 0; c < cuts.size(); ++c) {
                Storyboard::Panel panel;
                panel.length = (c + 1 < cuts.size() ? cuts[c + 1] : to) - cuts[c];
                shot.panels << panel;
            }
            for (EditTimeline::Transition const& tr : sequence.transitions)
                if (!tr.audio && tr.track == clip.track && tr.start <= clip.start && tr.end > clip.start) {
                    shot.transition = tr.name;
                    shot.transitionLength = int(std::lround((tr.end - tr.start) * ratio));
                }
            board.shots << shot;
            at = to;
        }
        if (!board.shots.isEmpty()) {
            scene.storyboard = board;
            r.shots = int(board.shots.size());
        }
    }

    // ---- the markers ----
    if (options.markers)
        for (EditTimeline::Marker const& m : sequence.markers)
            if (inSpan(m.frame)) {
                Scene::Marker marker;
                marker.frame = toScene(m.frame);
                marker.length = std::max(1, int(std::lround(m.length * ratio)));
                marker.name = m.name;
                marker.note = m.comment;
                scene.markers.erase(std::remove_if(scene.markers.begin(), scene.markers.end(), [&](Scene::Marker const& old) { return old.frame == marker.frame; }),
                                    scene.markers.end());
                scene.markers << marker;
            }
    std::sort(scene.markers.begin(), scene.markers.end(), [](Scene::Marker const& a, Scene::Marker const& b) { return a.frame < b.frame; });

    if (r.images == 0 && r.soundLayers.isEmpty() && r.shots == 0 && sequence.markers.isEmpty()) {
        if (error)
            *error = r.missing.isEmpty() ? QStringLiteral("Nothing in that timespan could be brought in.")
                                         : QStringLiteral("Nothing in that timespan could be brought in: its media cannot be found.");
        if (result)
            *result = r;
        return false;
    }
    if (result)
        *result = r;
    return true;
}

Sequence toSequence(Scene const& scene, QString const& movie, QString const& name)
{
    Sequence sequence;
    sequence.name = name;
    sequence.fps = scene.fps;
    sequence.ntsc = std::abs(scene.fps - std::round(scene.fps)) > 0.01;
    sequence.width = scene.width;
    sequence.height = scene.height;
    sequence.duration = scene.frameCount();
    Storyboard const& board = scene.storyboard;
    if (board.isEmpty()) {
        Clip clip;
        clip.name = name;
        clip.file = movie;
        clip.end = scene.frameCount();
        sequence.clips << clip;
    }
    for (int s = 0; s < board.shots.size(); ++s) {
        Storyboard::Shot const& shot = board.shots[s];
        Clip clip;
        clip.name = shot.name;
        clip.file = movie;
        clip.start = board.shotStart(s);
        clip.end = std::min(scene.frameCount(), clip.start + shot.length());
        clip.in = clip.start;
        if (clip.end <= clip.start)
            continue;
        // Its panels after the first: markers on the clip (on its file's frames).
        for (int p = 1; p < shot.panels.size(); ++p)
            clip.markers << EditTimeline::Marker{board.panelStart(s, p), 0, QStringLiteral("Panel %1").arg(p + 1), QString()};
        if (!shot.transition.isEmpty() && shot.transitionLength > 0 && s > 0) {
            EditTimeline::Transition tr;
            tr.start = clip.start;
            tr.end = clip.start + shot.transitionLength;
            tr.name = shot.transition;
            sequence.transitions << tr;
        }
        sequence.clips << clip;
    }
    int track = 0;
    for (Layer const& layer : scene.layers) {
        if (layer.kind != Layer::Sound)
            continue;
        bool any = false;
        for (SoundClip const& sound : layer.clipsOfSound()) {
            if (sound.file.isEmpty())
                continue;
            Clip clip;
            clip.audio = true;
            clip.track = track;
            clip.name = QFileInfo(sound.file).fileName();
            clip.file = sound.file;
            clip.start = layer.soundClips.isEmpty() ? layer.soundStart : sound.start;
            int length = sound.stop > sound.start ? sound.stop - sound.start : 0;
            if (length <= 0)
                length = sound.length > 0.0 ? int(std::lround(sound.length * scene.fps)) : scene.frameCount() - clip.start;
            clip.end = std::min(scene.frameCount(), clip.start + std::max(1, length));
            clip.in = int(std::lround(sound.offset * scene.fps));
            if (clip.end > clip.start) {
                sequence.clips << clip;
                any = true;
            }
        }
        if (any)
            ++track;
    }
    for (Scene::Marker const& m : scene.markers)
        sequence.markers << EditTimeline::Marker{m.frame, m.length > 1 ? m.length : 0, m.name, m.note};
    return sequence;
}

QStringList splitIntoScenes(Scene const& base, Sequence const& sequence, Options const& options, QString const& folder, QString* error)
{
    QStringList written;
    if (!QDir().mkpath(folder)) {
        if (error)
            *error = QStringLiteral("%1 cannot be made.").arg(QDir::toNativeSeparators(folder));
        return written;
    }
    QStringList const names = clipNames(sequence);
    for (QString const& name : names) {
        Scene scene = base;
        Options each = options;
        each.timespan = Options::OtherClip;
        each.clip = name;
        QString why;
        if (!apply(scene, sequence, each, nullptr, &why))
            continue;
        QString safe = QFileInfo(name).completeBaseName().isEmpty() ? name : QFileInfo(name).completeBaseName();
        safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        QString const path = QDir(folder).absoluteFilePath(safe + QStringLiteral(".ecl"));
        if (!scene.save(path, &why)) {
            if (error)
                *error = why;
            return written;
        }
        written << path;
    }
    if (written.isEmpty() && error && error->isEmpty())
        *error = QStringLiteral("No clip of the sequence could be made a scene.");
    return written;
}

}
