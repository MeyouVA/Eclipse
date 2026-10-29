// Eclipse - the document: motion-path control points, controllers and their states, the node graph, offsets.
#include "model/document/Commands.h"

#include "model/document/CommandsPrivate.h"

#include <algorithm>
#include <cmath>
#include <numbers>

int Document::insertControlPoint(int layer, int frame)
{
    Scene probe = m_scene;
    if (probe.insertControlPoint(layer, frame) < 0)
        return -1;
    int at = -1;
    apply(tr("Insert Control Point"), [&](Scene& s) { at = s.insertControlPoint(layer, frame); });
    return at;
}

static bool hasControl(Scene const& scene, int layer, int key, int control)
{
    return layer >= 0 && layer < scene.layers.size() && key >= 0 && key < scene.layers[layer].keys.size() && control >= 0
           && control < scene.layers[layer].keys[key].controls.size();
}

void Document::moveControlPoint(int layer, int key, int control, QPointF to)
{
    if (!hasControl(m_scene, layer, key, control) || m_scene.layers[layer].keys[key].controls[control] == to)
        return;
    apply(tr("Move Control Point"), [&](Scene& s) { s.layers[layer].keys[key].controls[control] = to; });
}

void Document::moveControlPointInSpace(int layer, int key, int control, QPointF to, double depth)
{
    if (!hasControl(m_scene, layer, key, control) || key + 1 >= m_scene.layers[layer].keys.size()
        || !std::isfinite(to.x()) || !std::isfinite(to.y()) || !std::isfinite(depth))
        return;
    Keyframe const& a = m_scene.layers[layer].keys[key];
    Keyframe const& b = m_scene.layers[layer].keys[key + 1];
    QVector<double> before;
    a.pathLine(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), &before);
    double const oldDepth = control < a.controlsZ.size() ? a.controlsZ[control]
                                                        : a.pose.z + (b.pose.z - a.pose.z) * before.value(control, 0.0);
    if (a.controls[control] == to && std::abs(oldDepth - depth) < 1e-9)
        return;
    apply(tr("Move Control Point"), [&](Scene& s) {
        Keyframe& into = s.layers[layer].keys[key];
        Keyframe const& next = s.layers[layer].keys[key + 1];
        into.controls[control] = to;
        QVector<double> after;
        into.pathLine(QPointF(into.pose.x, into.pose.y), QPointF(next.pose.x, next.pose.y), &after);
        double const natural = into.pose.z + (next.pose.z - into.pose.z) * after.value(control, 0.0);
        if (control < into.controlsZ.size() || std::abs(natural - depth) > 1e-9) {
            while (into.controlsZ.size() <= control)
                into.controlsZ.push_back(into.pose.z + (next.pose.z - into.pose.z) * before.value(into.controlsZ.size(), 0.0));
            into.controlsZ[control] = depth;
        }
    });
}

void Document::removeControlPoint(int layer, int key, int control)
{
    if (!hasControl(m_scene, layer, key, control))
        return;
    apply(tr("Remove Control Point"), [&](Scene& s) {
        Keyframe& k = s.layers[layer].keys[key];
        k.controls.removeAt(control);
        if (control < k.controlsZ.size())
            k.controlsZ.removeAt(control);
    });
}

void Document::setControlPointDepth(int layer, int key, int control, double z)
{
    if (!hasControl(m_scene, layer, key, control) || key + 1 >= m_scene.layers[layer].keys.size())
        return;
    Keyframe const& k = m_scene.layers[layer].keys[key];
    if (control < k.controlsZ.size() && k.controlsZ[control] == z)
        return;
    apply(tr("Control Point Depth"), [&](Scene& s) {
        Keyframe& a = s.layers[layer].keys[key];
        Keyframe const& b = s.layers[layer].keys[key + 1];
        // The points before it that had no depth of their own keep the one they had (on the straight way).
        QVector<double> placed;
        a.pathLine(QPointF(a.pose.x, a.pose.y), QPointF(b.pose.x, b.pose.y), &placed);
        while (a.controlsZ.size() <= control)
            a.controlsZ.push_back(a.pose.z + (b.pose.z - a.pose.z) * placed.value(a.controlsZ.size(), 0.0));
        a.controlsZ[control] = z;
    });
}

int Document::lockControlPointInTime(int layer, int key, int control)
{
    if (!hasControl(m_scene, layer, key, control) || m_scene.controlPointFrame(layer, key, control) < 0)
        return -1;
    int frame = -1;
    apply(tr("Lock in Time"), [&](Scene& s) { frame = s.lockControlPointInTime(layer, key, control); });
    return frame;
}

void Document::setPathShape(int layer, int key, double tension, double continuity, double bias)
{
    if (layer < 0 || layer >= m_scene.layers.size() || key < 0 || key >= m_scene.layers[layer].keys.size())
        return;
    Keyframe const& k = m_scene.layers[layer].keys[key];
    tension = std::clamp(tension, -1.0, 1.0);
    continuity = std::clamp(continuity, -1.0, 1.0);
    bias = std::clamp(bias, -1.0, 1.0);
    if (k.tension == tension && k.continuity == continuity && k.bias == bias)
        return;
    apply(tr("Shape Path"), [&](Scene& s) {
        Keyframe& into = s.layers[layer].keys[key];
        into.tension = tension;
        into.continuity = continuity;
        into.bias = bias;
    });
}

int Document::addController(QString const& name, QVector<int> const& layers)
{
    QVector<int> ids;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !ids.contains(m_scene.layers[l].id))
            ids.push_back(m_scene.layers[l].id);
    if (ids.isEmpty())
        return -1;
    int at = -1;
    apply(tr("New Controller"), [&](Scene& s) {
        Scene::Controller c;
        c.id = 1;
        for (Scene::Controller const& other : std::as_const(s.controllers))
            c.id = std::max(c.id, other.id + 1);
        c.name = name.trimmed().isEmpty() ? tr("Controller %1").arg(c.id) : name.trimmed();
        c.layers = ids;
        at = int(s.controllers.size());
        s.controllers.push_back(c);
    });
    return at;
}

void Document::removeController(int controller)
{
    if (controller < 0 || controller >= m_scene.controllers.size())
        return;
    apply(tr("Delete Controller"), [&](Scene& s) { s.controllers.removeAt(controller); });
}

void Document::renameController(int controller, QString const& name)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || name.trimmed().isEmpty() || m_scene.controllers[controller].name == name.trimmed())
        return;
    apply(tr("Rename Controller"), [&](Scene& s) { s.controllers[controller].name = name.trimmed(); });
}

// What the controller's layers are on `frame`.
static Scene::Controller::State stateAt(Scene const& scene, Scene::Controller const& c, int frame)
{
    Scene::Controller::State state;
    for (int id : c.layers) {
        int const l = scene.layerIndex(id);
        state.poses.push_back(l >= 0 ? scene.poseOf(l, frame) : Pose());
        state.drawings.push_back(l >= 0 && scene.layers[l].holdsDrawings() ? scene.layers[l].drawingAt(frame) : -1);
    }
    return state;
}

int Document::addControllerState(int controller, int frame, QString const& name)
{
    if (controller < 0 || controller >= m_scene.controllers.size())
        return -1;
    int at = -1;
    apply(tr("Add State"), [&](Scene& s) {
        Scene::Controller& c = s.controllers[controller];
        Scene::Controller::State state = stateAt(s, c, frame);
        state.name = name.trimmed().isEmpty() ? tr("State %1").arg(c.states.size() + 1) : name.trimmed();
        state.at = c.where;     // on the pad of a 2D point: where the point is now
        at = int(c.states.size());
        c.states.push_back(state);
    });
    return at;
}

void Document::replaceControllerState(int controller, int state, int frame)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || state < 0 || state >= m_scene.controllers[controller].states.size())
        return;
    apply(tr("Replace State"), [&](Scene& s) {
        Scene::Controller& c = s.controllers[controller];
        QString const name = c.states[state].name;
        QPointF const place = c.states[state].at;
        c.states[state] = stateAt(s, c, frame);
        c.states[state].name = name;
        c.states[state].at = place;
    });
}

void Document::removeControllerState(int controller, int state)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || state < 0 || state >= m_scene.controllers[controller].states.size())
        return;
    apply(tr("Remove State"), [&](Scene& s) {
        Scene::Controller& c = s.controllers[controller];
        c.states.removeAt(state);
        c.value = std::clamp(c.value, 0.0, std::max(0.0, double(c.states.size()) - 1.0));
    });
}

void Document::setGraph(NodeGraph const& graph, QString const& text)
{
    if (graph == m_scene.graph)
        return;
    apply(text, [&](Scene& s) { s.graph = graph; });
}

void Document::applyController(int controller, int frame, double value)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || frame < 0 || frame >= m_scene.frameCount())
        return;
    Scene::Controller const& c = m_scene.controllers[controller];
    if (c.states.isEmpty())
        return;
    value = std::clamp(value, 0.0, double(c.states.size()) - 1.0);
    int const a = int(std::floor(value)), b = std::min(a + 1, int(c.states.size()) - 1);
    double const t = value - a;
    apply(tr("Set Controller"), [&](Scene& s) {
        Scene::Controller& into = s.controllers[controller];
        into.value = value;
        into.keys.insert(frame, value);
        Scene::Controller::State const& from = into.states[a];
        Scene::Controller::State const& to = into.states[b];
        Scene::Controller::State const& nearer = t < 0.5 ? from : to;
        for (int i = 0; i < into.layers.size(); ++i) {
            int const l = s.layerIndex(into.layers[i]);
            if (l < 0)
                continue;
            Layer& layer = s.layers[l];
            keyOn(layer, frame, Pose::lerp(from.poses[i], to.poses[i], t));
            int const drawing = nearer.drawings[i];
            if (layer.holdsDrawings() && drawing >= 0 && drawing < layer.drawings.size() && frame < layer.cells.size())
                layer.cells[frame] = drawing;
        }
    });
}

void Document::setControllerPoint(int controller, bool on)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || m_scene.controllers[controller].point == on)
        return;
    apply(on ? tr("2D Point Controller") : tr("Slider Controller"), [&](Scene& s) {
        Scene::Controller& c = s.controllers[controller];
        c.point = on;
        if (!on)
            return;
        bool together = true;
        for (Scene::Controller::State const& state : std::as_const(c.states))
            together = together && state.at == c.states.first().at;
        if (together && c.states.size() > 1) {
            // Round the middle, the first at the top, the others on clockwise.
            for (int i = 0; i < c.states.size(); ++i) {
                double const angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * i / double(c.states.size());
                c.states[i].at = QPointF(0.5 + 0.4 * std::cos(angle), 0.5 + 0.4 * std::sin(angle));
            }
        }
        c.where = c.states.isEmpty() ? QPointF(0.5, 0.5) : c.states.first().at;
    });
}

void Document::moveControllerState(int controller, int state, QPointF at)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || state < 0 || state >= m_scene.controllers[controller].states.size())
        return;
    at = QPointF(std::clamp(at.x(), 0.0, 1.0), std::clamp(at.y(), 0.0, 1.0));
    if (m_scene.controllers[controller].states[state].at == at)
        return;
    apply(tr("Move State"), [&](Scene& s) { s.controllers[controller].states[state].at = at; });
}

void Document::applyControllerPoint(int controller, int frame, QPointF where)
{
    if (controller < 0 || controller >= m_scene.controllers.size() || frame < 0 || frame >= m_scene.frameCount())
        return;
    Scene::Controller const& c = m_scene.controllers[controller];
    if (c.states.isEmpty())
        return;
    where = QPointF(std::clamp(where.x(), 0.0, 1.0), std::clamp(where.y(), 0.0, 1.0));
    QVector<double> const weights = c.weightsAt(where);
    int const nearest = int(std::max_element(weights.begin(), weights.end()) - weights.begin());
    apply(tr("Set Controller"), [&](Scene& s) {
        Scene::Controller& into = s.controllers[controller];
        into.where = where;
        into.pointKeys.insert(frame, where);
        for (int i = 0; i < into.layers.size(); ++i) {
            int const l = s.layerIndex(into.layers[i]);
            if (l < 0)
                continue;
            Pose blended = into.states[0].poses[i];
            double taken = weights[0];
            for (int k = 1; k < into.states.size(); ++k) {
                taken += weights[k];
                if (taken > 1e-12)
                    blended = Pose::lerp(blended, into.states[k].poses[i], weights[k] / taken);
            }
            Layer& layer = s.layers[l];
            keyOn(layer, frame, blended);
            int const drawing = into.states[nearest].drawings[i];
            if (layer.holdsDrawings() && drawing >= 0 && drawing < layer.drawings.size() && frame < layer.cells.size())
                layer.cells[frame] = drawing;
        }
    });
}

bool Document::offsetPath(int layer, QPointF by)
{
    if (layer < 0 || layer >= m_scene.layers.size() || m_scene.layers[layer].keys.isEmpty() || by.isNull())
        return false;
    apply(tr("Spline Offset"), [&](Scene& s) {
        for (Keyframe& key : s.layers[layer].keys) {
            key.pose.x += by.x();
            key.pose.y += by.y();
            for (QPointF& control : key.controls)
                control += by;
        }
    });
    return true;
}

bool Document::offsetCamera(int camera, QPointF by, double depth)
{
    if (camera < 0 || camera >= m_scene.layers.size() || m_scene.layers[camera].kind != Layer::Camera || (by.isNull() && depth == 0.0))
        return false;
    apply(tr("Static Camera Transform"), [&](Scene& s) {
        Layer& l = s.layers[camera];
        if (l.keys.isEmpty()) {
            Keyframe held;
            held.frame = 0;
            held.pose = l.poseAt(0);
            held.tween = false;
            l.keys.push_back(held);
        }
        for (Keyframe& key : l.keys) {
            key.pose.x += by.x();
            key.pose.y += by.y();
            key.pose.z += depth;
            for (QPointF& control : key.controls)
                control += by;
            for (double& z : key.controlsZ)
                z += depth;
        }
        // A value that follows a function column of its own goes with it; a column others follow too is left alone.
        for (int value : {int(Driver::X), int(Driver::Y), int(Driver::Z)}) {
            int const id = l.linkOf(value);
            int const index = s.functionIndex(id);
            double const amount = value == Driver::X ? by.x() : value == Driver::Y ? by.y() : depth;
            if (index < 0 || amount == 0.0)
                continue;
            int users = 0;
            for (Layer const& other : s.layers)
                for (ValueLink const& link : other.links)
                    users += link.function == id;
            FunctionColumn& column = s.functions[index];
            if (users == 1 && column.hasPoints())
                for (Keyframe& key : column.curve.keys)
                    key.pose.x += amount;
        }
    });
    return true;
}

void Document::setWrites(QVector<Scene::Write> const& writes)
{
    if (writes == m_scene.writes)
        return;
    apply(tr("Write Outputs"), [&](Scene& s) { s.writes = writes; });
}

void Document::setDisplay(int layer)
{
    int const id = layer >= 0 && layer < m_scene.layers.size() ? m_scene.layers[layer].id : 0;
    if (id == m_scene.display)
        return;
    apply(tr("Set Default Display"), [&](Scene& s) { s.display = id; });
}

bool Document::offsetValue(int layer, int value, double by)
{
    if (layer < 0 || layer >= m_scene.layers.size() || value < 0 || value >= Driver::ValueCount || by == 0.0)
        return false;
    Layer const& l = m_scene.layers[layer];
    // (A value a function column or another layer gives is offset there, not here.)
    if (l.linkOf(value) != 0 || std::any_of(l.drivers.cbegin(), l.drivers.cend(), [value](Driver const& d) { return d.value == value; }))
        return false;
    apply(tr("Offset Attribute Value"), [&](Scene& s) {
        Layer& target = s.layers[layer];
        if (target.keys.isEmpty()) {
            Keyframe held;
            held.frame = 0;
            held.pose = target.poseAt(0);
            held.tween = false;
            target.keys.push_back(held);
        }
        for (Keyframe& key : target.keys) {
            double v = Driver::read(key.pose, value) + by;
            if (value == Driver::Opacity)
                v = std::clamp(v, 0.0, 1.0);
            Driver::write(key.pose, value, v);
        }
    });
    return true;
}
