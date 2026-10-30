// Eclipse - the document: function columns - made, changed, taken away, linked to layers' values, converted.
#include "model/document/Commands.h"

#include <algorithm>

#include "model/document/CommandsPrivate.h"

#include <cmath>
#include <tuple>

namespace {

bool sameKeys(QVector<Keyframe> const& a, QVector<Keyframe> const& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](Keyframe const& x, Keyframe const& y) {
        return std::tie(x.frame, x.pose, x.curves, x.parts, x.tween, x.easeIn, x.easeOut, x.liftIn, x.liftOut, x.controls, x.controlsZ, x.tension, x.continuity, x.bias)
            == std::tie(y.frame, y.pose, y.curves, y.parts, y.tween, y.easeIn, y.easeOut, y.liftIn, y.liftOut, y.controls, y.controlsZ, y.tension, y.continuity, y.bias);
    });
}

QString valueName(int value)
{
    static char const* const kNames[Driver::ValueCount] = {"Pos_x", "Pos_y", "Scale_x", "Scale_y", "Angle_z", "Opacity", "Pos_z", "Skew", "Fov"};
    return QString::fromLatin1(kNames[std::clamp(value, 0, int(Driver::ValueCount) - 1)]);
}

// A point of a column's curve (a keyframe that sets pos x to `v`).
Keyframe pointKey(int frame, double v)
{
    Keyframe k;
    k.frame = frame;
    k.pose.x = v;
    k.parts = Keyframe::PartX;
    return k;
}

// A column's values on every frame, as points: only those the line does not go straight through.
QVector<Keyframe> sampled(FunctionColumn const& column, Scene const& scene)
{
    int const last = scene.frameCount() - 1;
    QVector<double> v(last + 1);
    for (int f = 0; f <= last; ++f)
        v[f] = column.valueAt(f, &scene);
    QVector<Keyframe> keys;
    for (int f = 0; f <= last; ++f) {
        bool const between = f > 0 && f < last && std::abs(v[f] - (v[f - 1] + v[f + 1]) / 2.0) < 1e-9;
        if (!between) {
            keys.push_back(pointKey(f, v[f]));
            ValueCurve straight;
            straight.value = Driver::X;
            straight.after = ValueCurve::Straight;
            keys.last().setCurve(straight);
        }
    }
    return keys;
}

// One value a column gives (a 3D path: across, down or depth) on every frame, as points.
QVector<Keyframe> sampledValue(FunctionColumn const& column, int value, Scene const& scene)
{
    int const last = scene.frameCount() - 1;
    QVector<double> v(last + 1);
    for (int f = 0; f <= last; ++f)
        v[f] = column.valueFor(value, f, scene);
    QVector<Keyframe> keys;
    for (int f = 0; f <= last; ++f) {
        bool const between = f > 0 && f < last && std::abs(v[f] - (v[f - 1] + v[f + 1]) / 2.0) < 1e-9;
        if (!between) {
            keys.push_back(pointKey(f, v[f]));
            ValueCurve straight;
            straight.value = Driver::X;
            straight.after = ValueCurve::Straight;
            keys.last().setCurve(straight);
        }
    }
    return keys;
}

}  // namespace

int Document::addFunction(FunctionColumn column, QString const& text)
{
    column.id = m_scene.nextFunctionId;
    column.name = m_scene.uniqueFunctionName(column.name.trimmed().isEmpty() ? FunctionColumn::kindName(column.kind) : column.name.trimmed());
    int const id = column.id;
    apply(text, [&](Scene& s) {
        s.functions.push_back(column);
        s.nextFunctionId = id + 1;
    });
    return id;
}

void Document::setFunction(FunctionColumn const& column, QString const& text)
{
    int const i = m_scene.functionIndex(column.id);
    if (i < 0)
        return;
    FunctionColumn const& now = m_scene.functions[i];
    if (now.name == column.name && now.kind == column.kind && now.expression == column.expression && now.path == column.path && now.constantZ == column.constantZ
        && now.shown == column.shown && now.extendedTiming == column.extendedTiming && now.expressionOffset == column.expressionOffset
        && now.expressionFrames == column.expressionFrames && now.velocityRangeSet == column.velocityRangeSet && now.velocityStart == column.velocityStart
        && now.velocityEnd == column.velocityEnd && sameKeys(now.curve.keys, column.curve.keys) && sameKeys(now.velocity.keys, column.velocity.keys))
        return;
    apply(text, [&](Scene& s) { s.functions[i] = column; });
}

void Document::removeFunction(int id)
{
    int const i = m_scene.functionIndex(id);
    if (i < 0)
        return;
    apply(tr("Delete Function Column"), [&](Scene& s) {
        s.functions.removeAt(i);
        for (Layer& l : s.layers)
            l.links.removeIf([id](ValueLink const& link) { return link.function == id; });
    });
}

bool Document::renameFunction(int id, QString const& name)
{
    int const i = m_scene.functionIndex(id);
    QString const wanted = name.trimmed();
    if (i < 0 || wanted.isEmpty() || wanted == m_scene.functions[i].name)
        return false;
    for (FunctionColumn const& c : m_scene.functions)
        if (c.id != id && c.name.compare(wanted, Qt::CaseInsensitive) == 0)
            return false;
    apply(tr("Rename Function Column"), [&](Scene& s) { s.functions[i].name = wanted; });
    return true;
}

void Document::linkValue(int layer, int value, int function, QString const& text)
{
    if (layer < 0 || layer >= m_scene.layers.size() || value < 0 || value >= Driver::ValueCount || (function != 0 && !m_scene.function(function)))
        return;
    if (m_scene.layers[layer].linkOf(value) == function)
        return;
    apply(text, [&](Scene& s) {
        Layer& l = s.layers[layer];
        l.links.removeIf([value](ValueLink const& link) { return link.value == value; });
        if (function != 0)
            l.links.push_back({value, function});
    });
}

int Document::storeAsFunction(int layer, int value, int kind)
{
    if (layer < 0 || layer >= m_scene.layers.size() || value < 0 || value >= Driver::ValueCount || kind < 0 || kind >= FunctionColumn::KindCount)
        return 0;
    bool const path = kind == FunctionColumn::Path3D;
    if (path && value != Driver::X && value != Driver::Y && value != Driver::Z)
        return 0;
    Layer const& l = m_scene.layers[layer];
    FunctionColumn column;
    column.kind = kind;
    column.constantZ = path && FunctionColumn::defaultConstantZ;
    column.name = l.name + QLatin1Char('_') + (path ? QStringLiteral("Path") : valueName(value));
    // The animation it has: its points (a 3D path: where the layer is on each keyframe, each on its frame).
    QVector<int> const through = l.keysOf(value);
    for (int k : through) {
        Keyframe const& key = l.keys[k];
        if (path) {
            Pose const p = m_scene.poseOf(layer, key.frame);
            column.path.push_back({p.x, p.y, p.z, key.frame, 0.0, 0.0, 0.0});
            continue;
        }
        double const v = value == Driver::Fov && key.pose.fov <= 0.0 ? l.fovAt(key.frame) : Driver::read(key.pose, value);
        Keyframe point = pointKey(key.frame, v);
        point.tween = key.tween;
        point.easeIn = key.easeIn;
        point.easeOut = key.easeOut;
        point.liftIn = key.liftIn;
        point.liftOut = key.liftOut;
        if (ValueCurve const* own = key.curveOf(value)) {
            ValueCurve moved = *own;
            moved.value = Driver::X;
            point.setCurve(moved);
        }
        column.curve.keys.push_back(point);
    }
    double const resolved = Driver::read(m_scene.poseOf(layer, 0), value);
    double const now = value == Driver::Fov && resolved <= 0.0 ? l.fovAt(0) : resolved;
    if (kind == FunctionColumn::Expression)
        column.expression = QString::number(now, 'g', 10);
    if (path && column.path.isEmpty()) {
        Pose const p = m_scene.poseOf(layer, 0);
        column.path.push_back({p.x, p.y, p.z, 0, 0.0, 0.0, 0.0});
    }
    if (!path && kind != FunctionColumn::Expression && column.curve.keys.isEmpty())
        column.curve.keys.push_back(pointKey(0, now));
    beginMacro(tr("Create Function Column"));
    int const id = addFunction(column, tr("Create Function Column"));
    for (int v : path ? QVector<int>{Driver::X, Driver::Y, Driver::Z} : QVector<int>{value})
        linkValue(layer, v, id, tr("Link Function Column"));
    endMacro();
    return id;
}

int Document::convertFunction(int id, int kind, bool intoNew)
{
    FunctionColumn const* from = m_scene.function(id);
    if (!from || kind < 0 || kind >= FunctionColumn::KindCount || (kind == from->kind && !intoNew))
        return 0;
    FunctionColumn made = *from;
    made.kind = kind;
    bool const keepsPoints = (from->kind == FunctionColumn::Bezier || from->kind == FunctionColumn::Ease) && (kind == FunctionColumn::Bezier || kind == FunctionColumn::Ease);
    if (kind == FunctionColumn::Bezier || kind == FunctionColumn::Ease) {
        if (!keepsPoints)
            made.curve.keys = sampled(*from, m_scene);
        made.expression.clear();
        made.path.clear();
        made.velocity.keys.clear();
    } else if (kind == FunctionColumn::Expression) {
        made.expression = QString::number(from->valueAt(0, &m_scene), 'g', 10);
    } else if (kind == FunctionColumn::VeloBased && !from->hasPoints()) {
        made.curve.keys = sampled(*from, m_scene);
    }
    if (intoNew) {
        made.name = from->name;
        return addFunction(made, tr("Convert Function Column"));
    }
    setFunction(made, tr("Convert Function Column"));
    return id;
}

int Document::separatePath(int layer)
{
    if (layer < 0 || layer >= m_scene.layers.size())
        return 0;
    // (Copies: each step below replaces the scene's vectors.)
    QVector<QPair<int, FunctionColumn>> paths;
    for (int value : {int(Driver::X), int(Driver::Y), int(Driver::Z)})
        if (FunctionColumn const* c = m_scene.function(m_scene.layers[layer].linkOf(value)); c && c->kind == FunctionColumn::Path3D)
            paths.push_back({value, *c});
    if (paths.isEmpty())
        return 0;
    beginMacro(tr("Create Separate beziers"));
    for (auto const& [value, path] : std::as_const(paths)) {
        FunctionColumn made;
        made.kind = FunctionColumn::Bezier;
        made.name = path.name + (value == Driver::X ? QStringLiteral("_x") : value == Driver::Y ? QStringLiteral("_y") : QStringLiteral("_z"));
        made.curve.keys = sampledValue(path, value, m_scene);
        int const id = addFunction(made, tr("Create Separate beziers"));
        linkValue(layer, value, id, tr("Create Separate beziers"));
    }
    endMacro();
    return int(paths.size());
}

int Document::shareFunctions(QVector<int> const& layers)
{
    QVector<int> valid;
    for (int l : layers)
        if (l >= 0 && l < m_scene.layers.size() && !valid.contains(l))
            valid.push_back(l);
    if (valid.size() < 2)
        return 0;
    int const first = valid.first();
    int const kind = FunctionColumn::defaultBezier ? FunctionColumn::Bezier : FunctionColumn::Ease;
    int shared = 0;
    beginMacro(tr("Share Functions"));
    for (int value = 0; value < Driver::ValueCount; ++value) {
        int id = m_scene.layers[first].linkOf(value);
        if (id == 0 && !m_scene.layers[first].keysOf(value).isEmpty())
            id = storeAsFunction(first, value, kind);
        if (id == 0)
            continue;
        for (int i = 1; i < valid.size(); ++i)
            linkValue(valid[i], value, id, tr("Share Functions"));
        ++shared;
    }
    endMacro();
    return shared;
}

int Document::convertFunctionRelinked(int id, int kind)
{
    beginMacro(tr("Convert Function Column"));
    int const made = convertFunction(id, kind, true);
    if (made != 0)
        for (int l = 0; l < m_scene.layers.size(); ++l)
            for (int value = 0; value < Driver::ValueCount; ++value)
                if (m_scene.layers[l].linkOf(value) == id)
                    linkValue(l, value, made, tr("Convert Function Column"));
    endMacro();
    return made;
}

int Document::convertVelocity(int id, bool intoNew, bool relink)
{
    FunctionColumn const* now = m_scene.function(id);
    if (!now || now->velocity.keys.isEmpty())
        return 0;
    FunctionColumn changed = *now;
    bool const allEased = std::all_of(changed.velocity.keys.cbegin(), changed.velocity.keys.cend(), [](Keyframe const& k) { return k.easeIn > 0.0 && k.easeOut > 0.0; });
    for (Keyframe& k : changed.velocity.keys) {
        k.easeIn = k.easeOut = allEased ? 0.0 : 0.33;
        k.liftIn = k.liftOut = 0.0;
        k.dropCurve(Driver::X);
    }
    QString const text = changed.kind == FunctionColumn::Path3D ? tr("Convert velocity of 3dpath") : tr("Convert velobased");
    if (!intoNew) {
        setFunction(changed, text);
        return id;
    }
    beginMacro(text);
    int const made = addFunction(changed, text);
    if (relink)
        for (int l = 0; l < m_scene.layers.size(); ++l)
            for (int value = 0; value < Driver::ValueCount; ++value)
                if (m_scene.layers[l].linkOf(value) == id)
                    linkValue(l, value, made, text);
    endMacro();
    return made;
}
