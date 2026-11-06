// Eclipse - the document: adding, grouping, merging, moving, renaming layers; symbols and clones.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"
#include "model/media/AudioDuration.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

int Document::addCamera()
{
    // The first camera is the one the scene is seen through; a further one waits to be chosen.  A symbol has none.
    if (insideSymbol())
        return -1;
    int at = -1;
    apply(tr("Add Camera"), [&](Scene& s) { at = s.addLayer(s.uniqueLayerName(QStringLiteral("Camera")), -1, Layer::Camera); });
    return at;
}

int Document::addSound(QString const& file, int startFrame)
{
    int at = -1;
    apply(tr("Import Sound"), [&](Scene& s) {
        QString name = file.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
        if (name.contains(QLatin1Char('.')))
            name = name.section(QLatin1Char('.'), 0, -2);
        at = s.addLayer(name.isEmpty() ? QStringLiteral("Sound") : name, -1, Layer::Sound);
        s.layers[at].soundFile = file;
        s.layers[at].soundStart = startFrame;
    });
    return at;
}

void Document::setSoundStart(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].soundStart == frame)
        return;
    apply(tr("Move Sound"), [&](Scene& s) { s.layers[layer].soundStart = frame; });
}

void Document::setSoundClips(int layer, QVector<SoundClip> const& clips, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].clipsOfSound() == clips)
        return;
    apply(text, [&](Scene& s) {
        s.layers[layer].soundClips = clips;
        s.layers[layer].soundFile=clips.isEmpty()?QString():clips.first().file;
    });
}

void Document::setSoundVolume(int layer, double volume)
{
    volume = std::clamp(volume, 0.0, 16.0);
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].soundVolume == volume)
        return;
    apply(tr("Change Sound Sequence Volume"), [&](Scene& s) { s.layers[layer].soundVolume = volume; });
}

void Document::setSoundEffects(int layer, QVector<SoundEffect> const& effects, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].soundEffects == effects)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].soundEffects = effects; });
}

int Document::addSoundClip(int layer, QString const& file, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || file.isEmpty())
        return -1;
    QVector<SoundClip> clips = m_scene.layers[layer].clipsOfSound();
    SoundClip clip;
    clip.file = file;
    clip.start = frame - m_scene.layers[layer].soundStart;
    clips.push_back(clip);
    std::stable_sort(clips.begin(), clips.end(), [](SoundClip const& a, SoundClip const& b) { return a.start < b.start; });
    int const at = int(std::find(clips.begin(), clips.end(), clip) - clips.begin());
    setSoundClips(layer, clips, tr("Import Sound Clip"));
    return at;
}

// The clip `clip` cut at `at` frames from its start: the part before, and the part after (Split Sound Clip).
static QPair<SoundClip, SoundClip> cutClip(SoundClip const& clip, int at, double fps)
{
    SoundClip before = clip, after = clip;
    before.stop = clip.start + at;
    before.length = clip.length < 0.0 ? at / fps : std::min(clip.length, at / fps);
    after.start = clip.start + at;
    after.offset = clip.offset + at / fps;
    if (clip.length >= 0.0)
        after.length = std::max(0.0, clip.length - at / fps);
    // The envelope: each part keeps its own points, with the gain at the cut at their ends.
    before.envelope.clear();
    after.envelope.clear();
    if (!clip.envelope.isEmpty()) {
        double const there = clip.envelopeAt(at);
        for (QPointF const& p : clip.envelope) {
            if (p.x() < at)
                before.envelope.push_back(p);
            else if (p.x() > at)
                after.envelope.push_back(QPointF(p.x() - at, p.y()));
        }
        before.envelope.push_back(QPointF(at, there));
        after.envelope.push_front(QPointF(0.0, there));
    }
    return {before, after};
}

bool Document::splitSoundClip(int layer, int frame, QString* why,int selected)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound)
        return false;
    Layer const& sound = m_scene.layers[layer];
    int const at = frame - sound.soundStart;
    QVector<SoundClip> clips = sound.clipsOfSound();
    for (int c = 0; c < clips.size(); ++c) {
        if(selected>=0 && c!=selected) continue;
        SoundClip const& clip = clips[c];
        if(at<=clip.start || (clip.stop>=0 && at>=clip.stop)) continue;
        double const source=MediaFormats::audioDuration(clip.file);
        double const available=source>=0?std::max(0.0,source-clip.offset):-1;
        double const length=available<0?clip.length:clip.length<0?available:std::min(available,clip.length);
        if(length<0) { if(why) *why=tr("Cannot determine this sound clip's length."); return false; }
        bool const ended = clip.stop >= 0 && at >= clip.stop;
        if (at <= clip.start || ended || at >= clip.start+length*clip.loops*m_scene.fps)
            continue;
        if (clip.loops > 1) {
            if (why)
                *why = tr("Looped sound clips cannot be dragged or copied");
            return false;
        }
        auto const [before, after] = cutClip(clip, at - clip.start, m_scene.fps);
        clips[c] = before;
        clips.insert(c + 1, after);
        setSoundClips(layer, clips, tr("Split Sound Clip"));
        return true;
    }
    if (why)
        *why = tr("No Sound Available");
    return false;
}

bool Document::insertSilentCells(int layer, int frame, int count, QString* why)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || count < 1) {
        if (why)
            *why = tr("Please select inside a sound column.");
        return false;
    }
    int const at = frame - m_scene.layers[layer].soundStart;
    QVector<SoundClip> clips;
    for(SoundClip clip : m_scene.layers[layer].clipsOfSound()) {
        if(clip.start<at && (clip.stop<0 || at<clip.stop)) {
            double const source=MediaFormats::audioDuration(clip.file);
            double const available=source>=0?std::max(0.0,source-clip.offset):-1;
            double const length=available<0?clip.length:clip.length<0?available:std::min(available,clip.length);
            if(length<0) { if(why) *why=tr("Cannot determine this sound clip's length."); return false; }
            if(at<clip.start+length*clip.loops*m_scene.fps) {
                if(clip.loops>1) { if(why) *why=tr("Looped sound clips cannot be split to insert silence."); return false; }
                auto parts=cutClip(clip,at-clip.start,m_scene.fps);
                clips.push_back(parts.first); clip=parts.second;
            }
        }
        if(clip.start>=at) {
            if(clip.start>std::numeric_limits<int>::max()-count || clip.stop>std::numeric_limits<int>::max()-count) { if(why) *why=tr("The inserted silence exceeds the sound sequence limit."); return false; }
            clip.start+=count;
            if(clip.stop>=0) clip.stop+=count;
        }
        clips.push_back(clip);
    }
    setSoundClips(layer, clips, tr("Insert Silent Cells in Sound"));
    return true;
}

void Document::setEffects(int layer, QVector<Effect> const& effects, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].effects == effects)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].effects = effects; });
}

void Document::removeEffect(int layer, int index)
{
    if (layer < 0 || layer >= m_scene.layers.size() || index < 0 || index >= m_scene.layers[layer].effects.size())
        return;
    apply(tr("Remove Effect"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.effects.removeAt(index);
        // The amounts in the keyframes go by position: close the gap.
        for (Keyframe& k : l.keys)
            if (index < k.pose.fx.size())
                k.pose.fx.removeAt(index);
    });
}

void Document::setNodePos(int layer, QPointF pos)
{
    if (layer < 0 || layer >= m_scene.layers.size() || (m_scene.layers[layer].nodePlaced && m_scene.layers[layer].nodePos == pos))
        return;
    apply(tr("Move Node"), [&](Scene& s) {
        s.layers[layer].nodePos = pos;
        s.layers[layer].nodePlaced = true;
    });
}

void Document::setActiveCamera(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Camera || m_scene.cameraIndex() == layer)
        return;
    apply(tr("Choose Camera"), [&](Scene& s) { s.activeCamera = s.layers[layer].id; });
}

int Document::addAnnotation(int above)
{
    int at = -1;
    apply(tr("Add Annotation Column"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Annotation"), above + 1, Layer::Annotation); });
    return at;
}

void Document::editScene(QString const& text, std::function<void(Scene&)> const& change)
{
    apply(text, [&](Scene& s) { change(s); });
}

int Document::addPeg(int above)
{
    int at = -1;
    apply(tr("Add Peg"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Peg"), above + 1, Layer::Peg); });
    return at;
}

int Document::addColourCard()
{
    int at = -1;
    apply(tr("Add Colour-Card"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Colour-Card"), 0, Layer::ColourCard); });
    return at;
}

int Document::addGroup(int above)
{
    int at = -1;
    apply(tr("Add Group"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Group"), above + 1, Layer::Group); });
    return at;
}

int Document::groupLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int at = -1;
    apply(tr("Group Selection"), [&](Scene& s) {
        int const up = s.parentIndex(layer);
        at = s.addLayer(s.layers[layer].name + QStringLiteral("-G"), layer + 1, Layer::Group);
        // The group takes the place of the layer under what it was attached to.
        s.layers[at].parent = up >= 0 ? s.layers[up].id : 0;
        s.layers[at].parentBone = s.layers[layer].parentBone;
        s.layers[layer].parent = s.layers[at].id;
        s.layers[layer].parentBone = -1;
    });
    return at;
}

int Document::groupLayers(QVector<int> const& layers)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !in.contains(l))
            in.push_back(l);
    if (in.isEmpty())
        return -1;
    if (in.size() == 1)
        return groupLayer(in.first());
    std::sort(in.begin(), in.end());
    int at = -1;
    apply(tr("Group Selection"), [&](Scene& s) {
        int const front = in.last();
        int const up = s.parentIndex(front);
        at = s.addLayer(s.layers[front].name + QStringLiteral("-G"), front + 1, Layer::Group);
        s.layers[at].parent = up >= 0 && !in.contains(up) ? s.layers[up].id : 0;
        for (int l : in) {
            // A layer attached to another of the selection stays attached to it.
            if (in.contains(s.parentIndex(l)))
                continue;
            s.layers[l].parent = s.layers[at].id;
            s.layers[l].parentBone = -1;
        }
    });
    return at;
}

int Document::insertTemplate(Scene const& piece, int frame)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    int at = -1;
    apply(tr("Insert Template"), [&](Scene& s) { at = s.insert(piece, frame); });
    return at;
}

int Document::pasteTemplateOnto(Scene const& piece, int frame, bool drawings, bool keys)
{
    if (piece.layers.isEmpty() || frame < 0)
        return -1;
    Scene trial = m_scene;
    if (trial.pasteOnto(piece, frame, drawings, keys) < 0)
        return -1;      // not every layer of it is here: nothing is done
    int at = -1;
    apply(tr("Paste Template"), [&](Scene& s) { at = s.pasteOnto(piece, frame, drawings, keys); });
    return at;
}

int Document::mergeLayers(QVector<int> const& layers)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && m_scene.layers[l].holdsDrawings() && !in.contains(l))
            in.push_back(l);
    if (in.size() < 2)
        return -1;
    std::sort(in.begin(), in.end());    // back to front: the front layer is painted last
    int at = -1;
    apply(tr("Merge Layers"), [&](Scene& s) {
        QVector<Drawing> drawings;
        QVector<QVector<double>> made;  // what each of them was made from
        QVector<int> cells(s.frameCount(), Layer::kNoDrawing);
        for (int f = 0; f < s.frameCount(); ++f) {
            Drawing all;
            QVector<double> from;
            bool any = false;
            for (int l : in) {
                int const index = s.layers[l].drawingAt(f);
                QTransform const t = s.worldTransform(l, f);
                from << index << t.m11() << t.m12() << t.m13() << t.m21() << t.m22() << t.m23() << t.m31() << t.m32() << t.m33();
                if (index == Layer::kNoDrawing)
                    continue;
                any = true;
                Drawing part = s.layers[l].drawings[index];
                for (Picture& picture : part.pictures)
                    picture.transform(t);
                all.pictures += part.pictures;
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    for (Stroke& stroke : part.strokesOf(sheet))
                        stroke.transform(t);
                    for (Fill& fill : part.fillsOf(sheet))
                        fill.transform(t);
                    all.strokesOf(sheet) += part.strokesOf(sheet);
                    all.fillsOf(sheet) += part.fillsOf(sheet);
                }
            }
            if (!any)
                continue;
            // The same drawings standing the same way: the same merged drawing, held.
            int found = int(made.indexOf(from));
            if (found < 0) {
                found = int(drawings.size());
                drawings.push_back(all);
                made.push_back(from);
            }
            cells[f] = found;
        }
        at = s.addLayer(s.layers[in.last()].name + QStringLiteral("_Merged"), in.last() + 1, Layer::DrawingLayer);
        s.layers[at].drawings = drawings;
        s.layers[at].cells = cells;
        for (int i = int(in.size()) - 1; i >= 0; --i)
            s.removeLayer(in[i]);
        at -= int(in.size());
    });
    return at;
}

int Document::mergeDrawings(QVector<int> const& layers, int first, int last, bool* moved)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && m_scene.layers[l].holdsDrawings() && !m_scene.layers[l].bitmap && !in.contains(l))
            in.push_back(l);
    if (moved)
        *moved = false;
    if (in.size() < 2 || first > last)
        return 0;
    std::sort(in.begin(), in.end());    // back to front: the front layer is painted last
    int const target = in.last();
    int merged = 0;
    bool repositioned = false;
    apply(tr("Merge Selected Drawings"), [&](Scene& s) {
        for (int f = std::max(0, first); f <= std::min(last, s.frameCount() - 1); ++f) {
            QTransform const toTarget = s.worldTransform(target, f).isInvertible() ? s.worldTransform(target, f).inverted() : QTransform();
            Drawing all;
            int sources = 0;
            for (int l : std::as_const(in)) {
                int const index = s.layers[l].drawingAt(f);
                if (index == Layer::kNoDrawing)
                    continue;
                ++sources;
                QTransform const t = s.worldTransform(l, f) * toTarget;
                repositioned = repositioned || (l != target && !t.isIdentity());
                Drawing part = s.layers[l].drawings[index];
                for (Picture& picture : part.pictures)
                    picture.transform(t);
                all.pictures += part.pictures;
                for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                    for (Stroke& stroke : part.strokesOf(sheet))
                        stroke.transform(t);
                    for (Fill& fill : part.fillsOf(sheet))
                        fill.transform(t);
                    all.strokesOf(sheet) += part.strokesOf(sheet);
                    all.fillsOf(sheet) += part.fillsOf(sheet);
                }
            }
            if (sources < 2)
                continue;
            Layer& front = s.layers[target];
            front.drawings.push_back(all);
            if (front.cells.size() <= f)
                front.cells.resize(f + 1, Layer::kNoDrawing);
            front.cells[f] = int(front.drawings.size()) - 1;
            for (int l : std::as_const(in))
                if (l != target && f < s.layers[l].cells.size())
                    s.layers[l].cells[f] = Layer::kNoDrawing;
            ++merged;
        }
    });
    if (moved)
        *moved = repositioned;
    return merged;
}

int Document::addBone(int current)
{
    int at = -1;
    bool const chained = current >= 0 && current < m_scene.layers.size() && m_scene.layers[current].kind == Layer::Bone;
    apply(tr("Add Bone"), [&](Scene& s) {
        at = s.addLayer(QStringLiteral("Bone"), current + 1, Layer::Bone);
        if (chained) {
            // It hangs on the bone it was added to, and begins where that one ends.
            s.layers[at].parent = s.layers[current].id;
            s.layers[at].pivot = s.layers[current].pivot + QPointF(s.layers[current].boneLength, 0.0);
            s.layers[at].boneLength = s.layers[current].boneLength;
        }
    });
    return at;
}

int Document::addQuadmap(int above)
{
    int at = -1;
    apply(tr("Add Quadmap"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Quadmap"), above + 1, Layer::Quadmap); });
    return at;
}

void Document::setCardColor(int layer, QColor const& color)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !color.isValid() || m_scene.layers[layer].cardColor == color)
        return;
    apply(tr("Change Colour-Card"), [&](Scene& s) { s.layers[layer].cardColor = color; });
}

void Document::setIkConstraints(int layer, bool excluded, bool limited, double minimum, double maximum)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    if (minimum > maximum)
        std::swap(minimum, maximum);
    Layer const& now = m_scene.layers[layer];
    if (now.ikExcluded == excluded && now.ikLimited == limited && now.ikMin == minimum && now.ikMax == maximum)
        return;
    apply(tr("IK Constraints"), [&](Scene& s) {
        s.layers[layer].ikExcluded = excluded;
        s.layers[layer].ikLimited = limited;
        s.layers[layer].ikMin = minimum;
        s.layers[layer].ikMax = maximum;
    });
}

void Document::setBoneLength(int layer, double length)
{
    length = std::max(1.0, length);
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].boneLength == length)
        return;
    apply(tr("Change Bone Length"), [&](Scene& s) {
        // The bones that begin at its tip stay at its tip.
        double const delta = length - s.layers[layer].boneLength;
        for (Layer& child : s.layers)
            if (child.kind == Layer::Bone && child.parent == s.layers[layer].id)
                child.pivot += QPointF(delta, 0.0);
        s.layers[layer].boneLength = length;
    });
}

int Document::addParentPeg(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return -1;
    int at = -1;
    apply(tr("Add Parent Peg"), [&](Scene& s) {
        int const up = s.parentIndex(layer);
        int const upId = up >= 0 ? s.layers[up].id : 0;
        at = s.addLayer(s.layers[layer].name + QStringLiteral("-P"), layer + 1, Layer::Peg);
        s.layers[at].parent = upId;
        s.layers[at].pivot = s.layers[layer].pivot;
        s.layers[layer].parent = s.layers[at].id;
    });
    return at;
}

bool Document::setParent(int layer, int parent)
{
    Scene probe = m_scene;
    if (!probe.setParent(layer, parent))
        return false;
    if (probe.layers[layer].parent != m_scene.layers[layer].parent || probe.layers[layer].parentBone != m_scene.layers[layer].parentBone)
        apply(parent >= 0 ? tr("Attach Layer") : tr("Detach Layer"), [&](Scene& s) { s.setParent(layer, parent); });
    return true;
}

int Document::moveLayerBeside(int layer, int target, bool behind)
{
    int const count = int(m_scene.layers.size());
    if (layer < 0 || layer >= count || target < 0 || target >= count || layer == target)
        return layer;
    // Taking the layer out shifts everything in front of it down by one.
    int const to = behind ? (layer < target ? target - 1 : target) : (layer < target ? target : target + 1);
    if (to != layer)
        apply(tr("Move Layer"), [&](Scene& s) { s.layers.move(layer, to); });
    return to;
}

int Document::addLayer(int above)
{
    int at = -1;
    apply(tr("Add Layer"), [&](Scene& s) { at = s.addLayer(QStringLiteral("Drawing"), above + 1); });
    return at;
}

void Document::removeLayer(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers.size() == 1)
        return;
    apply(tr("Delete Layer"), [&](Scene& s) { s.removeLayer(layer); });
}

void Document::moveLayer(int layer, int delta)
{
    int const to = layer + delta;
    if (layer < 0 || layer >= m_scene.layers.size() || to < 0 || to >= m_scene.layers.size() || delta == 0)
        return;
    apply(tr("Move Layer"), [&](Scene& s) { s.layers.move(layer, to); });
}

void Document::renameLayer(int layer, QString const& name)
{
    if (layer < 0 || layer >= m_scene.layers.size() || name.trimmed().isEmpty() || m_scene.layers[layer].name == name)
        return;
    apply(tr("Rename Layer"), [&](Scene& s) {
        s.layers[layer].name.clear();  // so that its own name does not count as taken
        s.layers[layer].name = s.uniqueLayerName(name.trimmed());
    });
}

void Document::setLayerVisible(int layer, bool visible)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].visible == visible)
        return;
    Scene after = m_scene;
    after.layers[layer].visible = visible;
    pushUndo(new SceneStateCommand(this, m_scene, std::move(after), visible ? tr("Show Layer") : tr("Hide Layer")));
}

void Document::setLayerOnion(int layer, bool onion)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].onion == onion)
        return;
    apply(onion ? tr("Onion Skin On") : tr("Onion Skin Off"), [&](Scene& s) { s.layers[layer].onion = onion; });
}

void Document::editLayers(QVector<int> const& layers, QString const& text, std::function<void(Layer&)> const& change)
{
    if (layers.isEmpty())
        return;
    apply(text, [&](Scene& s) {
        for (int l : layers)
            if (l >= 0 && l < s.layers.size())
                change(s.layers[l]);
    });
}

int Document::createSymbol(QVector<int> const& layers, QString const& name)
{
    {
        Scene trial = m_scene;
        if (trial.makeSymbol(layers, name) < 0)
            return -1;
    }
    int at = -1;
    apply(tr("Create Symbol"), [&](Scene& s) { at = s.makeSymbol(layers, name); });
    return at;
}

int Document::insertSymbol(int symbolId, int above)
{
    if (!m_scene.findSymbol(symbolId))
        return -1;
    int at = -1;
    apply(tr("Insert Symbol"), [&](Scene& s) { at = s.addSymbolInstance(symbolId, above >= 0 && above < s.layers.size() ? above + 1 : -1); });
    return at;
}

int Document::insertSymbol(int symbolId, int above, bool reuse, bool allFrames, int frame)
{
    if (reuse && allFrames)
        return insertSymbol(symbolId, above);
    if (!m_scene.findSymbol(symbolId))
        return -1;
    int at = -1;
    apply(reuse ? tr("Insert Symbol") : tr("Duplicate elements"), [&](Scene& s) {
        int const shown = reuse ? symbolId : s.copySymbol(symbolId);
        if (shown <= 0)
            return;
        at = s.addSymbolInstance(shown, above >= 0 && above < s.layers.size() ? above + 1 : -1);
        if (at >= 0 && !allFrames) {
            // Its first frame only, exposed on the frame pasted on.
            QVector<int>& cells = s.layers[at].cells;
            std::fill(cells.begin(), cells.end(), int(Layer::kNoDrawing));
            if (frame >= 0 && frame < cells.size())
                cells[frame] = 0;
        }
    });
    return at;
}

int Document::insertEmptySymbol(QString const& name, int above)
{
    int at = -1;
    apply(tr("Create Empty Symbol"), [&](Scene& s) {
        int const id = s.addEmptySymbol(name);
        at = s.addSymbolInstance(id, above >= 0 && above < s.layers.size() ? above + 1 : -1);
    });
    return at;
}

bool Document::duplicateSymbol(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].showsSymbol() || !m_scene.findSymbol(m_scene.layers[layer].symbolId))
        return false;
    apply(tr("Duplicate Symbol"), [&](Scene& s) { s.duplicateSymbol(layer); });
    return true;
}

int Document::addLibrarySymbol(QString const& name)
{
    int id = 0;
    apply(tr("Create Empty Symbol in Library"), [&](Scene& s) { id = s.addEmptySymbol(name); });
    return id;
}

int Document::copySymbol(int id)
{
    if (!m_scene.findSymbol(id))
        return 0;
    int made = 0;
    apply(tr("Duplicate symbols"), [&](Scene& s) { made = s.copySymbol(id); });
    return made;
}

bool Document::renameSymbol(int id, QString const& name)
{
    Scene trial = m_scene;
    Scene::Symbol const* const symbol = m_scene.findSymbol(id);
    if (!symbol || symbol->name == name.trimmed() || !trial.renameSymbol(id, name))
        return false;
    apply(tr("Rename Symbol"), [&](Scene& s) { s.renameSymbol(id, name); });
    return true;
}

int Document::deleteSymbol(int id)
{
    if (insideSymbol() || !m_scene.findSymbol(id))
        return -1;
    int removed = -1;
    apply(tr("Symbol Deletion"), [&](Scene& s) { removed = s.removeSymbol(id); });
    return removed;
}

int Document::createSymbolFromArt(int layer, int drawing, Drawing const& piece, QString const& name)
{
    if (piece.strokes.isEmpty() && piece.fills.isEmpty() && piece.pictures.isEmpty())
        return -1;
    int at = -1;
    apply(tr("Create Symbol from Drawing Selection"), [&](Scene& s) { at = s.makeSymbolFromArt(layer, drawing, piece, name); });
    return at;
}

int Document::addCloneLayer(int layer, bool timing)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].showsSymbol())
        return -1;
    int at = -1;
    apply(tr("Clone Layer"), [&](Scene& s) { at = s.addCloneLayer(layer, timing); });
    return at;
}

int Document::clearIkKeyframes(int layer, int frame)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return 0;
    QVector<int> chain = m_scene.ikChain(layer);
    if (!chain.contains(layer))
        chain.push_back(layer);
    int cleared = 0;
    for (int l : std::as_const(chain))
        cleared += l >= 0 && l < m_scene.layers.size() && m_scene.layers[l].keyIndex(frame) >= 0;
    if (cleared == 0)
        return 0;
    apply(tr("IK Clear All Affected Keyframes"), [&](Scene& s) {
        for (int l : std::as_const(chain))
            if (l >= 0 && l < s.layers.size())
                if (int const k = s.layers[l].keyIndex(frame); k >= 0)
                    s.layers[l].keys.removeAt(k);
    });
    return cleared;
}

void Document::makeCloneIndependent(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].cloneGroup == 0)
        return;
    apply(tr("Make Clone Independent"), [&](Scene& s) { s.makeCloneIndependent(layer); });
}

bool Document::useDrawingsOf(int layer, int source)
{
    Scene probe = m_scene;
    if (!probe.useDrawingsOf(layer, source))
        return false;
    apply(tr("Change Drawing Element"), [&](Scene& s) { s.useDrawingsOf(layer, source); });
    return true;
}

bool Document::setIkConstraint(QVector<int> const& layers, QString const& which, int frame)
{
    QVector<int> in;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !in.contains(l))
            in.push_back(l);
    if (in.isEmpty())
        return false;
    QString const text = which == QLatin1String("nail")          ? tr("IK Set Nail")
                         : which == QLatin1String("orientation") ? tr("IK Set Hold Orientation")
                         : which == QLatin1String("x")           ? tr("IK Set Hold X")
                         : which == QLatin1String("y")           ? tr("IK Set Hold Y")
                         : which == QLatin1String("min")         ? tr("Set IK/Min Angle")
                         : which == QLatin1String("max")         ? tr("Set IK/Max Angle")
                         : which == QLatin1String("exclude")     ? tr("IK Set Exclusion Flag")
                         : which == QLatin1String("resetAll")    ? tr("Reset ALL IK Constraints")
                         : which == QLatin1String("resetSelected") ? tr("Reset ALL IK Constraints for Selected Joints")
                                                                 : tr("IK Remove All Constraints");
    apply(text, [&](Scene& s) {
        for (int l : in) {
            Layer& layer = s.layers[l];
            double const angle = layer.poseAt(frame).rotation;
            if (which == QLatin1String("nail"))
                layer.ikNail = true;
            else if (which == QLatin1String("orientation"))
                layer.ikHoldAngle = true;
            else if (which == QLatin1String("x"))
                layer.ikHoldX = true;
            else if (which == QLatin1String("y"))
                layer.ikHoldY = true;
            else if (which == QLatin1String("exclude"))
                layer.ikExcluded = true;
            else if (which == QLatin1String("min")) {
                layer.ikLimited = true;
                layer.ikMin = angle;
                layer.ikMax = std::max(layer.ikMax, angle);
            } else if (which == QLatin1String("max")) {
                layer.ikLimited = true;
                layer.ikMax = angle;
                layer.ikMin = std::min(layer.ikMin, angle);
            } else {
                layer.ikNail = layer.ikHoldAngle = layer.ikHoldX = layer.ikHoldY = layer.ikLimited = layer.ikExcluded = false;
            }
        }
    });
    return true;
}

int Document::applyIkConstraints(int from, int to)
{
    if (from < 0 || to <= from)
        return 0;
    Scene trial = m_scene;
    int const fixed = trial.applyIkConstraints(from, to);
    if (fixed == 0)
        return 0;
    apply(tr("Apply Inverse Kinematics Constraints"), [&](Scene& s) { s.layers = trial.layers; });
    return fixed;
}

void Document::setConstraint(int layer, Constraint const& constraint, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].constraint == constraint)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].constraint = constraint; });
}

void Document::setLoop(int layer, int type, int start, int end)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    Layer const& l = m_scene.layers[layer];
    if (l.loopType == type && l.loopStart == start && l.loopEnd == end)
        return;
    apply(tr("Loop Type"), [&](Scene& s) {
        s.layers[layer].loopType = std::clamp(type, 0, 2);
        s.layers[layer].loopStart = start;
        s.layers[layer].loopEnd = end;
    });
}

void Document::setConstraintValue(int layer, int frame, double value)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    Pose pose = m_scene.layers[layer].poseAt(frame);
    pose.constraint = value;
    setKeyframe(layer, frame, pose, tr("Active Value"));
}
