// Eclipse - the document: Master Controllers (their script and data), and positions split into x, y and z or put back on a path.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

#include <algorithm>

int Document::addMasterController(QString const& name, QVector<int> const& layers, QString const& script)
{
    QVector<int> ids;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !ids.contains(m_scene.layers[l].id))
            ids.push_back(m_scene.layers[l].id);
    int at = -1;
    apply(tr("New Master Controller"), [&](Scene& s) {
        Scene::Controller c;
        c.id = 1;
        for (Scene::Controller const& other : std::as_const(s.controllers))
            c.id = std::max(c.id, other.id + 1);
        c.name = name.trimmed().isEmpty() ? tr("Master Controller %1").arg(c.id) : name.trimmed();
        c.layers = ids;
        c.master = true;
        c.script = script;
        at = int(s.controllers.size());
        s.controllers.push_back(c);
    });
    return at;
}

void Document::setControllerScript(int controller, QString const& script)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || m_scene.controllers[controller].script == script)
        return;
    apply(tr("Master Controller Script"), [&](Scene& s) {
        s.controllers[controller].script = script;
        s.controllers[controller].master = true;
    });
}

void Document::setControllerData(int controller, QString const& data)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || m_scene.controllers[controller].data == data)
        return;
    apply(tr("Master Controller - Set data"), [&](Scene& s) { s.controllers[controller].data = data; });
}

int Document::convertToSplitPosition(QVector<int> const& layers, bool keysOnly)
{
    QVector<int> chosen;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !chosen.contains(l) && !m_scene.layers[l].separatePosition)
            chosen.push_back(l);
    if (chosen.isEmpty())
        return 0;
    apply(tr("Convert to Split Position"), [&](Scene& s) {
        for (int l : chosen) {
            Layer& layer = s.layers[l];
            // Where the path takes it on each frame of a span it bends along, read before anything changes.
            QVector<QPair<int, Pose>> baked;
            if (!keysOnly)
                for (int k = 0; k + 1 < layer.keys.size(); ++k) {
                    Keyframe const& a = layer.keys[k];
                    if (a.controls.isEmpty() || !a.tween)
                        continue;
                    for (int f = a.frame + 1; f < layer.keys[k + 1].frame; ++f)
                        baked.push_back({f, layer.poseAt(f)});
                }
            for (Keyframe& k : layer.keys)
                k.controls.clear(), k.controlsZ.clear();
            layer.separatePosition = true;
            for (auto const& [f, pose] : std::as_const(baked)) {
                int i = 0;
                while (i < layer.keys.size() && layer.keys[i].frame < f)
                    ++i;
                Keyframe k;
                k.frame = f;
                k.pose = layer.poseAt(f);
                k.pose.x = pose.x;
                k.pose.y = pose.y;
                k.pose.z = pose.z;
                k.parts = Keyframe::PartX | Keyframe::PartY | Keyframe::PartZ;
                k.tween = true;
                layer.keys.insert(i, k);
            }
        }
    });
    return int(chosen.size());
}

int Document::convertToPathPosition(QVector<int> const& layers)
{
    QVector<int> chosen;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !chosen.contains(l) && m_scene.layers[l].separatePosition)
            chosen.push_back(l);
    if (chosen.isEmpty())
        return 0;
    apply(tr("Removing Split Position"), [&](Scene& s) {
        for (int l : chosen)
            s.layers[l].separatePosition = false;
    });
    return int(chosen.size());
}
