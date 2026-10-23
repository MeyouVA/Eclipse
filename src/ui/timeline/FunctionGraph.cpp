// Eclipse - the Function view's graph: what it shows, fitting, zoom, where things are on it, the points and their handles.
#include "ui/timeline/FunctionView.h"

#include <QWheelEvent>

#include <cmath>

#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"

FunctionGraph::FunctionGraph(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setObjectName(QStringLiteral("FunctionGraph"));
    setMinimumSize(160, 100);
    setFocusPolicy(Qt::ClickFocus);
    connect(m_document, &Document::changed, this, [this] {
        pruneSelection();
        fit();
        update();
    });
    fit();
}

QColor FunctionGraph::colourOf(Timeline::Parameter which)
{
    // Across, down and depth are the user's (the Colours page); the others are fixed.
    if (which == Timeline::PosX)
        return InterfaceColours::get("functionCurveX");
    if (which == Timeline::PosY)
        return InterfaceColours::get("functionCurveY");
    if (which == Timeline::PosZ)
        return InterfaceColours::get("functionCurveZ");
    static QColor const colours[Timeline::ParameterCount] = {QColor(232, 96, 84),  QColor(110, 200, 120), QColor(240, 170, 70),
                                                             QColor(100, 170, 240), QColor(200, 130, 230), QColor(210, 210, 210),
                                                             QColor(90, 210, 200), QColor(180, 190, 90), QColor(230, 120, 170)};
    return colours[which];
}

void FunctionGraph::setCurrentCell(int layer, int frame)
{
    if (layer == m_layer && frame == m_frame)
        return;
    bool const other = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (other) {
        m_others.removeAll(layer);
        pruneSelection();
        fit();
    }
    update();
}

void FunctionGraph::setShown(Timeline::Parameter which, bool shown)
{
    if (m_shown[which] == shown)
        return;
    m_shown[which] = shown;
    pruneSelection();
    fit();
    update();
}

void FunctionGraph::setOtherLayers(QVector<int> const& layers)
{
    QVector<int> others;
    for (int l : layers)
        if (l != m_layer && l >= 0 && l < m_document->scene().layers.size() && !others.contains(l))
            others.push_back(l);
    if (others == m_others)
        return;
    m_others = others;
    pruneSelection();
    fit();
    update();
}

void FunctionGraph::setLayerHidden(int layer, bool hidden)
{
    if (hidden == m_hidden.contains(layer))
        return;
    if (hidden)
        m_hidden.push_back(layer);
    else
        m_hidden.removeAll(layer);
    pruneSelection();
    fit();
    update();
}

QVector<int> FunctionGraph::layersDrawn() const
{
    QVector<int> out;
    int const count = int(m_document->scene().layers.size());
    if (m_layer >= 0 && m_layer < count && !m_hidden.contains(m_layer))
        out.push_back(m_layer);
    for (int l : m_others)
        if (l >= 0 && l < count && !m_hidden.contains(l) && !out.contains(l))
            out.push_back(l);
    return out;
}

Layer const& FunctionGraph::layerOf(int layer) const
{
    int const i = int(m_previewLayers.indexOf(layer));
    return i >= 0 ? m_preview[i] : m_document->scene().layers[layer];
}

bool FunctionGraph::driven(int layer, int value) const
{
    Scene const& scene = m_document->scene();
    for (Driver const& d : scene.layers[layer].drivers)
        if (d.value == value && scene.layerIndex(d.source) >= 0)
            return true;
    return false;
}

double FunctionGraph::valueOf(int layer, int value, double frame) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return 0.0;
    Layer const& l = layerOf(layer);
    auto onFrame = [&](int f) {
        if (driven(layer, value))
            return Driver::read(scene.poseOf(layer, f), value);
        return value == Timeline::Fov ? l.fovAt(f) : Driver::read(l.poseAt(f), value);
    };
    int const whole = int(std::floor(frame));
    if (frame - whole < 1e-9 || driven(layer, value))
        return onFrame(int(std::lround(frame))) * unitOf(value);
    // Between two frames: on the curve itself where the value has a shape of its own there, else straight across.
    QVector<int> const through = l.keysOf(value);
    for (int n = 0; n + 1 < through.size(); ++n) {
        Keyframe const& a = l.keys[through[n]];
        Keyframe const& b = l.keys[through[n + 1]];
        if (a.frame <= frame && frame < b.frame && (a.curveOf(value) || b.curveOf(value)) && a.controls.isEmpty() && value != Timeline::Fov)
            return l.curveValue(value, through, frame) * unitOf(value);
    }
    double const t = frame - whole;
    return (onFrame(whole) * (1.0 - t) + onFrame(whole + 1) * t) * unitOf(value);
}

double FunctionGraph::valueAt(Timeline::Parameter which, int frame) const
{
    return valueOf(m_layer, which, frame);
}

QVector<FunctionPoint> FunctionGraph::points() const
{
    QVector<FunctionPoint> out;
    for (int layer : layersDrawn()) {
        Layer const& l = layerOf(layer);
        for (int value = 0; value < Timeline::ParameterCount; ++value) {
            if (!m_shown[value] || !Timeline::parameterFor(l.kind, Timeline::Parameter(value)) || driven(layer, value))
                continue;
            for (int k : l.keysOf(value))
                out.push_back({layer, value, l.keys[k].frame});
        }
    }
    return out;
}

void FunctionGraph::pruneSelection()
{
    QVector<FunctionPoint> const all = points();
    QVector<FunctionPoint> kept;
    for (FunctionPoint const& p : std::as_const(m_selection))
        if (all.contains(p))
            kept.push_back(p);
    if (kept == m_selection)
        return;
    m_selection = kept;
    if (m_selection.isEmpty())
        m_handle = 0;
    emit selectionChanged();
}

void FunctionGraph::setSelection(QVector<FunctionPoint> const& points)
{
    m_selection = points;
    m_handle = 0;
    pruneSelection();
    emit selectionChanged();
    update();
}

void FunctionGraph::selectHandle(int which)
{
    if (m_selection.isEmpty())
        return;
    // Only a side the curve goes on to.
    m_handle = which != 0 && !handleAt(m_selection.first(), which == 2).isNull() ? which : 0;
    emit selectionChanged();
    update();
}

void FunctionGraph::selectAll()
{
    setSelection(points());
}

void FunctionGraph::deselectAll()
{
    setSelection({});
}

void FunctionGraph::selectNeighbour(bool next)
{
    // The points on the frame of the next (or previous) point of the curves drawn, from the one selected or the current frame.
    int const from = m_selection.isEmpty() ? m_frame : m_selection.first().frame;
    int best = -1;
    for (FunctionPoint const& p : points())
        if (next ? p.frame > from && (best < 0 || p.frame < best) : p.frame < from && (best < 0 || p.frame > best))
            best = p.frame;
    if (best < 0)
        return;
    QVector<FunctionPoint> there;
    for (FunctionPoint const& p : points())
        if (p.frame == best)
            there.push_back(p);
    setSelection(there);
    emit framePicked(best);
}

ValueCurve FunctionGraph::curveAt(FunctionPoint const& p) const
{
    Layer const& l = layerOf(p.layer);
    QVector<int> const through = l.keysOf(p.value);
    int const k = l.keyIndex(p.frame);
    int const at = int(through.indexOf(k));
    ValueCurve curve;
    curve.value = p.value;
    if (k < 0 || at < 0)
        return curve;
    if (ValueCurve const* own = l.keys[k].curveOf(p.value))
        return *own;
    QPointF out, in, unused;
    if (at > 0)
        l.segmentHandles(p.value, through[at - 1], k, unused, in);
    if (at + 1 < through.size())
        l.segmentHandles(p.value, k, through[at + 1], out, unused);
    curve.in = in;
    curve.out = out;
    curve.corner = at > 0 && at + 1 < through.size() && (l.keys[k].easeIn > 0.0 || l.keys[k].easeOut > 0.0);
    curve.after = l.keys[k].tween ? ValueCurve::Curved : ValueCurve::Held;
    return curve;
}

QPointF FunctionGraph::handleAt(FunctionPoint const& p, bool out) const
{
    Layer const& l = layerOf(p.layer);
    QVector<int> const through = l.keysOf(p.value);
    int const at = int(through.indexOf(l.keyIndex(p.frame)));
    int const other = out ? at + 1 : at - 1;
    if (at < 0 || other < 0 || other >= through.size())
        return QPointF();
    ValueCurve const curve = curveAt(p);
    QPointF const handle = out ? curve.out : curve.in;
    double const span = l.keys[through[other]].frame - p.frame;
    double const reach = std::clamp(handle.x(), std::min(0.0, span), std::max(0.0, span));
    QPointF const from = pointOf(p);
    QPointF const to(xOfFrame(p.frame + reach), yOfValue(valueOf(p.layer, p.value, p.frame) + handle.y() * unitOf(p.value)));
    // A handle of no length (a straight way): shown a third of the way to the next point, which shapes the curve the same.
    if (QLineF(from, to).length() < 3.0)
        return from + (pointOf({p.layer, p.value, l.keys[through[other]].frame}) - from) / 3.0;
    return to;
}

void FunctionGraph::fit()
{
    if (m_zoomed)
        return;     // the values were chosen by hand
    // From the lowest to the highest value any curve drawn takes, with room above and below.
    Scene const& scene = m_document->scene();
    bool any = false;
    double low = 0.0, high = 0.0;
    for (int layer : layersDrawn()) {
        for (int which = 0; which < Timeline::ParameterCount; ++which) {
            if (!m_shown[which] || (layer != m_layer && which == Timeline::Fov))
                continue;
            for (int f = 0; f < scene.frameCount(); ++f) {
                double const v = valueOf(layer, which, f);
                low = any ? std::min(low, v) : v;
                high = any ? std::max(high, v) : v;
                any = true;
            }
            // ... and the handles of the points selected.
            for (FunctionPoint const& p : std::as_const(m_selection)) {
                if (p.layer != layer || p.value != which)
                    continue;
                ValueCurve const c = curveAt(p);
                double const v = valueOf(layer, which, p.frame);
                for (double h : {c.in.y(), c.out.y()}) {
                    low = std::min(low, v + h * unitOf(which));
                    high = std::max(high, v + h * unitOf(which));
                }
            }
        }
    }
    double const room = std::max((high - low) * 0.15, std::max(std::abs(high), std::abs(low)) * 0.05 + 1.0);
    m_low = low - room;
    m_high = high + room;
}

double FunctionGraph::frameSpan() const
{
    return m_span > 0.0 ? m_span : std::max(2, m_document->scene().frameCount()) - 1.0;
}

void FunctionGraph::resetView()
{
    m_first = 0.0;
    m_span = 0.0;
    m_zoomed = false;
    fit();
    update();
}

void FunctionGraph::resetZoom()
{
    // All of the frames, and as many values as the curves need, about the value the graph had been slid to.
    double const middle = (m_low + m_high) / 2.0;
    bool const was = m_zoomed;
    m_span = 0.0;
    m_first = 0.0;
    m_zoomed = false;
    fit();
    if (was) {
        double const half = (m_high - m_low) / 2.0;
        m_low = middle - half;
        m_high = middle + half;
        m_zoomed = true;
    }
    update();
}

void FunctionGraph::resetPan()
{
    // As close as it is, but from the first frame and with the values fitted round the curves again.
    double const along = m_high - m_low;
    m_first = 0.0;
    bool const was = m_zoomed;
    m_zoomed = false;
    fit();
    if (was) {
        double const middle = (m_low + m_high) / 2.0;
        m_low = middle - along / 2.0;
        m_high = middle + along / 2.0;
        m_zoomed = true;
    }
    update();
}

void FunctionGraph::zoomFrames(double factor)
{
    double const whole = std::max(2, m_document->scene().frameCount()) - 1.0;
    double const middle = m_first + frameSpan() / 2.0;
    double const span = std::clamp(frameSpan() * factor, 2.0, whole);
    m_span = span >= whole - 1e-9 ? 0.0 : span;
    m_first = m_span == 0.0 ? 0.0 : std::clamp(middle - span / 2.0, 0.0, whole - span);
    update();
}

void FunctionGraph::zoomValues(double factor)
{
    double const middle = (m_low + m_high) / 2.0;
    double const span = std::clamp((m_high - m_low) * factor, 1e-3, 1e9);
    m_low = middle - span / 2.0;
    m_high = middle + span / 2.0;
    m_zoomed = true;
    update();
}

void FunctionGraph::wheelEvent(QWheelEvent* event)
{
    QRectF const r = plot();
    double const factor = std::pow(1.2, -event->angleDelta().y() / 120.0);     // rolled away: closer
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        // Along the values, about the one under the pointer.
        double const at = valueOfY(event->position().y());
        double const span = std::clamp((m_high - m_low) * factor, 1e-3, 1e9);
        double const share = (at - m_low) / (m_high - m_low);
        m_low = at - span * share;
        m_high = m_low + span;
        m_zoomed = true;
    } else {
        // Along the frames, about the one under the pointer; never further out than the whole scene.
        double const whole = std::max(2, m_document->scene().frameCount()) - 1.0;
        double const at = m_first + (event->position().x() - r.left()) * frameSpan() / r.width();
        double const span = std::clamp(frameSpan() * factor, 2.0, whole);
        m_first = std::clamp(at - (event->position().x() - r.left()) * span / r.width(), 0.0, whole - span);
        m_span = span >= whole - 1e-9 ? 0.0 : span;
        if (m_span == 0.0)
            m_first = 0.0;
    }
    update();
    event->accept();
}

QRectF FunctionGraph::plot() const
{
    return QRectF(46, 8, std::max(10, width() - 54), std::max(10, height() - 28));
}

double FunctionGraph::xOfFrame(double frame) const
{
    QRectF const r = plot();
    return r.left() + (frame - m_first) * r.width() / frameSpan();
}

double FunctionGraph::frameOfXExact(double x) const
{
    QRectF const r = plot();
    return m_first + (x - r.left()) * frameSpan() / r.width();
}

int FunctionGraph::frameOfX(double x) const
{
    return std::clamp(int(std::lround(frameOfXExact(x))), 0, m_document->scene().frameCount() - 1);
}

double FunctionGraph::yOfValue(double value) const
{
    QRectF const r = plot();
    return r.bottom() - (value - m_low) * r.height() / (m_high - m_low);
}

double FunctionGraph::valueOfY(double y) const
{
    QRectF const r = plot();
    return m_low + (r.bottom() - y) * (m_high - m_low) / r.height();
}

QPointF FunctionGraph::pointAt(Timeline::Parameter which, int frame) const
{
    return QPointF(xOfFrame(frame), yOfValue(valueAt(which, frame)));
}

QPointF FunctionGraph::pointOf(FunctionPoint const& p) const
{
    return QPointF(xOfFrame(p.frame), yOfValue(valueOf(p.layer, p.value, p.frame)));
}

void FunctionGraph::setGridShown(bool on)
{
    m_grid = on;
    update();
}

void FunctionGraph::setCurrentFrameShown(bool on)
{
    m_currentShown = on;
    update();
}
