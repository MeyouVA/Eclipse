// Eclipse - the document: bones, deformers, lip-sync.
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

#include "model/document/CommandsPrivate.h"

void Document::setParentBone(int layer, int bone)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return;
    int const parent = m_scene.parentIndex(layer);
    if (parent < 0 || bone + 1 >= m_scene.layers[parent].bones.size())
        bone = -1;
    bone = std::max(bone, -1);
    if (m_scene.layers[layer].parentBone != bone)
        apply(tr("Attach to Bone"), [&](Scene& s) { s.layers[layer].parentBone = bone; });
}

void Document::setBones(int layer, QVector<QPointF> const& joints, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].bones == joints)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.bones = joints;
        // A bone that is gone takes its angles with it.
        int const count = std::max(0, int(joints.size()) - 1);
        for (Keyframe& k : l.keys)
            if (k.pose.bend.size() > count)
                k.pose.bend.resize(count);
        // What rode on a bone that is gone follows the layer only.
        for (Layer& child : s.layers)
            if (child.parent == l.id && child.parentBone >= count)
                child.parentBone = -1;
    });
}

void Document::setWarp(int layer, QVector<QPointF> const& points, bool closed, QString const& text)
{
    // (On a peg, a deformer above all the layers attached to it.)
    if (layer < 0 || layer >= m_scene.layers.size() || !(m_scene.layers[layer].holdsDrawings() || m_scene.layers[layer].kind == Layer::Peg)
        || points.size() % 3 != 0)
        return;
    if (m_scene.layers[layer].warp == points && m_scene.layers[layer].warpClosed == closed)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        bool const another = l.warpClosed != closed && !l.warp.isEmpty();
        l.warp = points;
        l.warpClosed = closed;
        // A point that is gone takes with it what the keyframes did to it.
        for (Keyframe& k : l.keys) {
            if (another)
                k.pose.warp.clear();
            else if (k.pose.warp.size() > points.size())
                k.pose.warp.resize(points.size());
        }
    });
}

void Document::setLipSync(int layer, QString const& shapes, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || m_scene.layers[layer].lipsync == shapes)
        return;
    apply(text, [&](Scene& s) { s.layers[layer].lipsync = shapes; });
}

void Document::setMouth(int layer, int frame, QChar shape)
{
    static QString const kShapes = QStringLiteral("ABCDEFGX");
    shape = shape.toUpper();
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].kind != Layer::Sound || !kShapes.contains(shape))
        return;
    int const at = frame - m_scene.layers[layer].soundStart;
    if (at < 0 || frame >= m_scene.frameCount() || m_scene.layers[layer].mouthAt(frame) == shape)
        return;
    apply(tr("Change Mouth Shape"), [&](Scene& s) {
        QString& shapes = s.layers[layer].lipsync;
        // The frames before it that had no shape yet are at rest.
        while (shapes.size() <= at)
            shapes += QLatin1Char('X');
        shapes[at] = shape;
    });
}

bool Document::mapLipSync(int soundLayer, int target, QVector<int> const& drawingFor)
{
    static QString const kShapes = QStringLiteral("ABCDEFGX");
    if (soundLayer < 0 || soundLayer >= m_scene.layers.size() || target < 0 || target >= m_scene.layers.size())
        return false;
    Layer const& sound = m_scene.layers[soundLayer];
    Layer const& drawn = m_scene.layers[target];
    if (sound.kind != Layer::Sound || sound.lipsync.isEmpty() || !drawn.holdsDrawings() || drawn.drawings.isEmpty())
        return false;
    QVector<int> cells = drawn.cells;
    bool any = false;
    for (int f = 0; f < m_scene.frameCount() && f < cells.size(); ++f) {
        int const shape = kShapes.indexOf(sound.mouthAt(f));
        int const drawing = shape >= 0 && shape < drawingFor.size() ? drawingFor[shape] : -1;
        if (drawing < 0 || drawing >= drawn.drawings.size())
            continue;
        any = true;
        cells[f] = drawing;
    }
    if (!any || cells == drawn.cells)
        return any;
    apply(tr("Map Lip-Sync"), [&](Scene& s) {
        Layer& l = s.layers[target];
        l.cells = cells;
        // The mouths are a bank to choose from: none is thrown away for not being shown.
        for (Drawing& drawing : l.drawings)
            drawing.pinned = true;
        // A morphing between two exposures that are no longer there has nothing to go between.
        for (int m = int(l.morphs.size()) - 1; m >= 0; --m)
            if (l.morphEnd(m) < 0)
                l.morphs.removeAt(m);
    });
    return true;
}

void Document::insertWarpPoint(int layer, QVector<QPointF> const& points, int index, double along)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].holdsDrawings())
        return;
    int const before = m_scene.layers[layer].warpPoints();
    if (points.size() != (before + 1) * 3 || index < 1 || index > before)
        return;
    apply(tr("Insert Deformer Point"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.warp = points;
        for (Keyframe& k : l.keys) {
            if (k.pose.warp.isEmpty())
                continue;
            k.pose.warp.resize(before * 3);
            // The new point goes along with its two neighbours, as far as it lies between them.
            QPointF const a = k.pose.warp[(index - 1) * 3], b = k.pose.warp[(index % before) * 3];
            k.pose.warp.insert(index * 3, 3, QPointF());
            k.pose.warp[index * 3] = a + (b - a) * along;
        }
    });
}

bool Document::convertDeformed(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size() || !m_scene.layers[layer].hasWarp())
        return false;
    apply(tr("Convert Deformed Drawings"), [&](Scene& s) {
        Layer& l = s.layers[layer];
        struct Made {
            int source;
            QVector<QPointF> moved;
            int index;
        };
        QVector<Made> made;
        QVector<Drawing> drawings;
        QVector<int> cells(l.cells.size(), Layer::kNoDrawing);
        QVector<bool> used(l.drawings.size(), false);
        for (int f = 0; f < l.cells.size(); ++f) {
            int const source = l.cells[f];
            if (source < 0 || source >= l.drawings.size())
                continue;
            used[source] = true;
            Pose const pose = l.poseAt(f);
            QVector<QPointF> const moved = Deform::isWarped(l.warp, l.warpClosed, pose.warp) ? pose.warp : QVector<QPointF>();
            int found = -1;
            for (Made const& m : made)
                if (m.source == source && m.moved == moved)
                    found = m.index;
            if (found < 0) {
                found = int(drawings.size());
                drawings.push_back(Deform::warp(l.drawings[source], l.warp, l.warpClosed, moved));
                made.push_back({source, moved, found});
            }
            cells[f] = found;
        }
        // The drawings kept in the bank though no frame shows them stay as they are.
        for (int i = 0; i < l.drawings.size(); ++i)
            if (!used[i] && l.drawings[i].pinned)
                drawings.push_back(l.drawings[i]);
        l.drawings = drawings;
        l.cells = cells;
        l.warp.clear();
        l.warpClosed = false;
        for (Keyframe& k : l.keys)
            k.pose.warp.clear();
    });
    return true;
}
