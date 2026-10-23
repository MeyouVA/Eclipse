// Eclipse - the Function view's graph: its points moved, cut, copied, pasted, deleted, inserted; their handles and segments.
#include "ui/timeline/FunctionView.h"

#include <QMap>

#include <cmath>

#include "model/document/Commands.h"

namespace {

// The handle's place in the widget, from its place in frames and value (and back), the graph's scale given by two points.
struct Scale {
    double perFrame = 1.0, perValue = 1.0;      // pixels across a frame, pixels up a unit shown
    QPointF toPixels(QPointF h, double unit) const { return QPointF(h.x() * perFrame, -h.y() * unit * perValue); }
    QPointF fromPixels(QPointF d, double unit) const { return QPointF(d.x() / perFrame, -d.y() / perValue / unit); }
};

QPointF unit(QPointF v)
{
    double const n = std::hypot(v.x(), v.y());
    return n > 1e-12 ? v / n : QPointF();
}

}  // namespace

void FunctionGraph::boundsOfSelection(int& low, int& high) const
{
    Scene const& scene = m_document->scene();
    low = -scene.frameCount();
    high = scene.frameCount();
    for (FunctionPoint const& p : m_selection) {
        low = std::max(low, -p.frame);
        high = std::min(high, scene.frameCount() - 1 - p.frame);
        Layer const& l = scene.layers[p.layer];
        for (int k : l.keysOf(p.value)) {
            int const f = l.keys[k].frame;
            if (m_selection.contains(FunctionPoint{p.layer, p.value, f}))
                continue;
            if (f < p.frame)
                low = std::max(low, f + 1 - p.frame);
            else if (f > p.frame)
                high = std::min(high, f - 1 - p.frame);
        }
    }
}

void FunctionGraph::makePreview()
{
    Scene const& scene = m_document->scene();
    QVector<Layer> layers;
    QVector<int> which;
    if (m_drag == Points) {
        for (FunctionPoint const& p : std::as_const(m_selection))
            if (!which.contains(p.layer))
                which.push_back(p.layer);
        for (int layer : which) {
            Layer copy = scene.layers[layer];
            QVector<QPair<int, int>> moving;
            for (FunctionPoint const& p : std::as_const(m_selection)) {
                if (p.layer != layer)
                    continue;
                int const k = copy.keyIndex(p.frame);
                if (k >= 0 && m_dragDelta != 0.0) {
                    double v = (valueOf(p.layer, p.value, p.frame) + m_dragDelta) / unitOf(p.value);
                    if (p.value == Timeline::Opacity)
                        v = std::clamp(v, 0.0, 1.0);
                    Driver::write(copy.keys[k].pose, p.value, v);
                    copy.keys[k].parts |= 1 << p.value;
                }
                moving.push_back({p.frame, 1 << p.value});
            }
            if (m_dragBy != 0)
                copy.moveValues(moving, m_dragBy, scene.frameCount() - 1);
            layers.push_back(copy);
        }
    } else if (m_drag == Handle) {
        Layer copy = scene.layers[m_grip.layer];
        int const k = copy.keyIndex(m_grip.frame);
        if (k >= 0)
            copy.keys[k].setCurve(m_dragCurve);
        which.push_back(m_grip.layer);
        layers.push_back(copy);
    }
    m_preview = layers;
    m_previewLayers = which;
}

void FunctionGraph::commitMove(int by, double delta)
{
    Scene const& scene = m_document->scene();
    QString const text = by != 0 ? tr("Move Points") : tr("Set Value");
    QVector<int> layers;
    for (FunctionPoint const& p : std::as_const(m_selection))
        if (!layers.contains(p.layer))
            layers.push_back(p.layer);
    QVector<FunctionPoint> moved = m_selection;
    for (FunctionPoint& p : moved)
        p.frame += by;
    int const to = m_selection.isEmpty() ? m_frame : m_selection.first().frame + by;
    m_document->beginMacro(text);
    for (int layer : layers) {
        QVector<Document::ValueChange> changes;
        QVector<QPair<int, int>> points;
        for (FunctionPoint const& p : std::as_const(m_selection)) {
            if (p.layer != layer)
                continue;
            points.push_back({p.frame, 1 << p.value});
            double const v = (valueOf(p.layer, p.value, p.frame) + delta) / unitOf(p.value);
            if (delta == 0.0)
                continue;
            // (The angle of view: the other keyframes keep the angle they stood for.)
            if (p.value == Timeline::Fov && scene.layers[layer].kind == Layer::Camera)
                m_document->setFov(layer, v, p.frame);
            else
                changes.push_back({p.frame, p.value, v});
        }
        if (!changes.isEmpty())
            m_document->setKeyValues(layer, changes, text);
        if (by != 0)
            m_document->moveValuePoints(layer, points, by, text);
    }
    m_document->endMacro();
    m_selection = moved;
    pruneSelection();
    emit selectionChanged();
    if (by != 0)
        emit framePicked(to);
}

ValueCurve FunctionGraph::pulledCurve(FunctionPoint const& p, bool out, QPointF to, bool breaking) const
{
    Scene const& scene = m_document->scene();
    Layer const& l = scene.layers[p.layer];
    ValueCurve curve = curveAt(p);
    curve.value = p.value;
    QVector<int> const through = l.keysOf(p.value);
    int const at = int(through.indexOf(l.keyIndex(p.frame)));
    double const before = at > 0 ? l.keys[through[at - 1]].frame - p.frame : -1e9;
    double const after = at >= 0 && at + 1 < through.size() ? l.keys[through[at + 1]].frame - p.frame : 1e9;
    double const u = unitOf(p.value);
    QRectF const r = plot();
    Scale const scale{r.width() / frameSpan(), r.height() / (m_high - m_low)};
    QPointF const origin = pointOf(p);
    // Where the pointer is, in frames and value from the point: not past the points either side.
    QPointF h = scale.fromPixels(to - origin, u);
    h.setX(out ? std::clamp(h.x(), 0.0, after) : std::clamp(h.x(), before, 0.0));
    (out ? curve.out : curve.in) = h;
    if (breaking)
        curve.corner = true;
    if (!curve.corner) {
        // Smooth: the other handle turned to stay in one line with it, as long as it was (on the screen).
        QPointF& other = out ? curve.in : curve.out;
        double length = std::hypot(scale.toPixels(other, u).x(), scale.toPixels(other, u).y());
        if (length < 3.0)
            length = std::hypot(scale.toPixels(h, u).x(), scale.toPixels(h, u).y());
        QPointF const turned = scale.fromPixels(-unit(scale.toPixels(h, u)) * length, u);
        other = turned;
        other.setX(out ? std::clamp(other.x(), before, 0.0) : std::clamp(other.x(), 0.0, after));
    }
    return curve;
}

void FunctionGraph::commitHandle()
{
    m_document->setValueCurves(m_grip.layer, {{m_grip.frame, m_dragCurve}}, tr("Move Handle"));
}

bool FunctionGraph::selectedSpot(double& frame, double& value) const
{
    if (m_selection.isEmpty())
        return false;
    FunctionPoint const& p = m_selection.first();
    if (m_handle != 0) {
        ValueCurve const curve = curveAt(p);
        QPointF const h = m_handle == 2 ? curve.out : curve.in;
        frame = p.frame + h.x() + 1.0;
        value = valueOf(p.layer, p.value, p.frame) + h.y() * unitOf(p.value);
        return true;
    }
    frame = p.frame + 1.0;
    value = valueOf(p.layer, p.value, p.frame);
    return true;
}

void FunctionGraph::setSelectedSpot(double frame, double value)
{
    if (m_selection.isEmpty())
        return;
    FunctionPoint const p = m_selection.first();
    if (m_handle != 0) {
        m_grip = p;
        m_gripOut = m_handle == 2;
        m_dragCurve = pulledCurve(p, m_gripOut, QPointF(xOfFrame(frame - 1.0), yOfValue(value)), false);
        commitHandle();
        return;
    }
    int low = 0, high = 0;
    boundsOfSelection(low, high);
    int const by = std::clamp(int(std::lround(frame - 1.0)) - p.frame, low, high);
    double const delta = value - valueOf(p.layer, p.value, p.frame);
    if (by != 0 || std::abs(delta) > 1e-12)
        commitMove(by, delta);
}

void FunctionGraph::copySelection()
{
    if (m_selection.isEmpty())
        return;
    int first = m_selection.first().frame;
    for (FunctionPoint const& p : std::as_const(m_selection))
        first = std::min(first, p.frame);
    m_copied.clear();
    Scene const& scene = m_document->scene();
    for (FunctionPoint const& p : std::as_const(m_selection)) {
        Copied c;
        c.value = p.value;
        c.offset = p.frame - first;
        c.v = valueOf(p.layer, p.value, p.frame) / unitOf(p.value);
        Layer const& l = scene.layers[p.layer];
        if (ValueCurve const* own = l.keys[l.keyIndex(p.frame)].curveOf(p.value)) {
            c.hasCurve = true;
            c.curve = *own;
        }
        m_copied.push_back(c);
    }
}

void FunctionGraph::cutSelection()
{
    if (m_selection.isEmpty())
        return;
    copySelection();
    m_document->beginMacro(tr("Cut Points"));
    deleteSelection();
    m_document->endMacro();
}

void FunctionGraph::pasteAtCurrentFrame()
{
    Scene const& scene = m_document->scene();
    if (m_copied.isEmpty() || m_layer < 0 || m_layer >= scene.layers.size())
        return;
    QVector<Document::ValueChange> values;
    QVector<FunctionPoint> pasted;
    for (Copied const& c : std::as_const(m_copied)) {
        int const frame = m_frame + c.offset;
        if (frame >= scene.frameCount() || !Timeline::parameterFor(scene.layers[m_layer].kind, Timeline::Parameter(c.value)))
            continue;
        values.push_back({frame, c.value, c.v});
        pasted.push_back({m_layer, c.value, frame});
    }
    if (values.isEmpty())
        return;
    m_document->beginMacro(tr("Paste Points"));
    m_document->setKeyValues(m_layer, values, tr("Paste Points"));
    QVector<Document::CurveChange> curves;
    for (Copied const& c : std::as_const(m_copied))
        if (c.hasCurve && m_frame + c.offset < scene.frameCount())
            curves.push_back({m_frame + c.offset, c.curve});
    if (!curves.isEmpty())
        m_document->setValueCurves(m_layer, curves, tr("Paste Points"));
    m_document->endMacro();
    setSelection(pasted);
}

void FunctionGraph::deleteSelection()
{
    if (m_selection.isEmpty())
        return;
    QMap<int, QVector<QPair<int, int>>> byLayer;
    for (FunctionPoint const& p : std::as_const(m_selection))
        byLayer[p.layer].push_back({p.frame, 1 << p.value});
    m_document->beginMacro(tr("Delete Points"));
    for (auto it = byLayer.cbegin(); it != byLayer.cend(); ++it)
        m_document->removeKeyValues(it.key(), it.value(), tr("Delete Points"));
    m_document->endMacro();
    setSelection({});
}

void FunctionGraph::setSegment(int segment)
{
    if (m_selection.isEmpty())
        return;
    Scene const& scene = m_document->scene();
    QMap<int, QVector<Document::CurveChange>> byLayer;
    QVector<QPair<int, int>> tweening;      // stop-motion keyframes that tween again (layer, frame)
    for (FunctionPoint const& p : std::as_const(m_selection)) {
        ValueCurve c = curveAt(p);
        c.after = segment;
        byLayer[p.layer].push_back({p.frame, c});
        Layer const& l = scene.layers[p.layer];
        int const k = l.keyIndex(p.frame);
        if (segment != ValueCurve::Held && k >= 0 && !l.keys[k].tween && !tweening.contains(QPair<int, int>(p.layer, p.frame)))
            tweening.push_back({p.layer, p.frame});
    }
    // A stop-motion keyframe made to tween for these values: its other values stay held.
    for (QPair<int, int> const& t : std::as_const(tweening)) {
        Layer const& l = scene.layers[t.first];
        int const k = l.keyIndex(t.second);
        for (int value = 0; value < Timeline::ParameterCount; ++value) {
            FunctionPoint const other{t.first, value, t.second};
            if (m_selection.contains(other) || !l.keysOf(value).contains(k))
                continue;
            ValueCurve held = curveAt(other);
            held.after = ValueCurve::Held;
            byLayer[t.first].push_back({t.second, held});
        }
    }
    QString const text = segment == ValueCurve::Held ? tr("Set Constant Segment") : segment == ValueCurve::Straight ? tr("Set Linear Segment") : tr("Set Curved Segment");
    m_document->beginMacro(text);
    for (QPair<int, int> const& t : std::as_const(tweening))
        m_document->setKeyTween(t.first, t.second, true);
    for (auto it = byLayer.cbegin(); it != byLayer.cend(); ++it)
        m_document->setValueCurves(it.key(), it.value(), text);
    m_document->endMacro();
}

void FunctionGraph::setCorner(bool corner)
{
    if (m_selection.isEmpty())
        return;
    QRectF const r = plot();
    Scale const scale{r.width() / frameSpan(), r.height() / (m_high - m_low)};
    QMap<int, QVector<Document::CurveChange>> byLayer;
    for (FunctionPoint const& p : std::as_const(m_selection)) {
        ValueCurve c = curveAt(p);
        c.corner = corner;
        bool const both = !handleAt(p, false).isNull() && !handleAt(p, true).isNull();
        if (!corner && both) {
            // Smooth: the two handles turned into one line, the way between the ways they went.
            double const u = unitOf(p.value);
            QPointF const out = scale.toPixels(c.out, u), in = scale.toPixels(c.in, u);
            QPointF way = unit(unit(out) - unit(in));
            if (way.isNull())
                way = QPointF(1, 0);
            if (way.x() < 0.0)
                way = -way;
            c.out = scale.fromPixels(way * std::hypot(out.x(), out.y()), u);
            c.in = scale.fromPixels(-way * std::hypot(in.x(), in.y()), u);
        }
        byLayer[p.layer].push_back({p.frame, c});
    }
    m_document->beginMacro(corner ? tr("Set Corner") : tr("Set Smooth"));
    for (auto it = byLayer.cbegin(); it != byLayer.cend(); ++it)
        m_document->setValueCurves(it.key(), it.value(), corner ? tr("Set Corner") : tr("Set Smooth"));
    m_document->endMacro();
}

void FunctionGraph::insertPoint()
{
    // On the current frame: the curves of the points selected, or else every curve of the layer drawn.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    QVector<QPair<int, int>> curves;     // (layer, value)
    for (FunctionPoint const& p : std::as_const(m_selection))
        if (!curves.contains(QPair<int, int>(p.layer, p.value)))
            curves.push_back({p.layer, p.value});
    if (curves.isEmpty())
        for (int value = 0; value < Timeline::ParameterCount; ++value)
            if (m_shown[value] && Timeline::parameterFor(scene.layers[m_layer].kind, Timeline::Parameter(value)) && !driven(m_layer, value))
                curves.push_back({m_layer, value});
    QMap<int, QVector<Document::ValueChange>> values;
    QMap<int, QVector<Document::CurveChange>> shapes;
    QVector<FunctionPoint> made;
    for (QPair<int, int> const& c : std::as_const(curves)) {
        Layer const& l = scene.layers[c.first];
        QVector<int> const through = l.keysOf(c.second);
        bool there = false;
        for (int k : through)
            there = there || l.keys[k].frame == m_frame;
        if (there)
            continue;
        values[c.first].push_back({m_frame, c.second, valueOf(c.first, c.second, m_frame) / unitOf(c.second)});
        made.push_back({c.first, c.second, m_frame});
        // Between two points: the curve split there, each side keeping its shape.
        int i = -1;
        for (int n = 0; n + 1 < through.size(); ++n)
            if (l.keys[through[n]].frame < m_frame && m_frame < l.keys[through[n + 1]].frame)
                i = n;
        if (i < 0 || !l.keys[through[i]].tween)
            continue;
        Keyframe const& a = l.keys[through[i]];
        Keyframe const& b = l.keys[through[i + 1]];
        ValueCurve ca = curveAt({c.first, c.second, a.frame}), cb = curveAt({c.first, c.second, b.frame});
        ValueCurve mid;
        mid.value = c.second;
        mid.after = ca.after;
        if (ca.after == ValueCurve::Curved && !(a.controls.size() && c.second <= Timeline::PosY)) {
            QPointF out, in;
            l.segmentHandles(c.second, through[i], through[i + 1], out, in);
            double const span = b.frame - a.frame;
            QPointF const p0(0.0, Driver::read(a.pose, c.second)), p3(span, Driver::read(b.pose, c.second));
            QPointF const p1(std::clamp(out.x(), 0.0, span), p0.y() + out.y()), p2(std::clamp(span + in.x(), 0.0, span), p3.y() + in.y());
            // Where along the curve the frame is, and the curve cut there (de Casteljau).
            auto xAt = [&](double s) {
                double const u = 1.0 - s;
                return u * u * u * p0.x() + 3 * u * u * s * p1.x() + 3 * u * s * s * p2.x() + s * s * s * p3.x();
            };
            double lo = 0.0, hi = 1.0;
            for (int n = 0; n < 48; ++n)
                (xAt((lo + hi) / 2.0) < m_frame - a.frame ? lo : hi) = (lo + hi) / 2.0;
            double const s = (lo + hi) / 2.0;
            QPointF const p01 = p0 + (p1 - p0) * s, p12 = p1 + (p2 - p1) * s, p23 = p2 + (p3 - p2) * s;
            QPointF const p012 = p01 + (p12 - p01) * s, p123 = p12 + (p23 - p12) * s, cut = p012 + (p123 - p012) * s;
            ca.out = p01 - p0;
            cb.in = p23 - p3;
            mid.in = p012 - cut;
            mid.out = p123 - cut;
            shapes[c.first].push_back({a.frame, ca});
            shapes[c.first].push_back({b.frame, cb});
            shapes[c.first].push_back({m_frame, mid});
        } else if (ca.after != ValueCurve::Curved) {
            shapes[c.first].push_back({m_frame, mid});
        }
    }
    if (made.isEmpty())
        return;
    m_document->beginMacro(tr("Insert Point"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_document->setKeyValues(it.key(), it.value(), tr("Insert Point"));
    for (auto it = shapes.cbegin(); it != shapes.cend(); ++it)
        m_document->setValueCurves(it.key(), it.value(), tr("Insert Point"));
    m_document->endMacro();
    setSelection(made);
}
