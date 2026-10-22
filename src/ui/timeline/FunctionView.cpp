#include "ui/timeline/FunctionView.h"

#include <QWheelEvent>

#include <QHBoxLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

#include "model/document/Commands.h"
#include "ui/window/InterfaceColours.h"

// ---- the graph -------------------------------------------------------------------------------

FunctionGraph::FunctionGraph(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setObjectName(QStringLiteral("FunctionGraph"));
    setMinimumSize(160, 100);
    connect(m_document, &Document::changed, this, [this] {
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
    if (other)
        fit();
    update();
}

void FunctionGraph::setShown(Timeline::Parameter which, bool shown)
{
    if (m_shown[which] == shown)
        return;
    m_shown[which] = shown;
    fit();
    update();
}

double FunctionGraph::valueAt(Timeline::Parameter which, int frame) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return 0.0;
    if (which == Timeline::Fov)
        return scene.layers[m_layer].fovAt(frame);
    double const value = Timeline::parameterOf(scene.layers[m_layer].poseAt(frame), which);
    return which == Timeline::Opacity ? value * 100.0 : value;
}

int FunctionGraph::firstShown() const
{
    for (int which = 0; which < Timeline::ParameterCount; ++which)
        if (m_shown[which])
            return which;
    return -1;
}

QPointF FunctionGraph::easeHandleOf(Layer const& layer, int key, bool out) const
{
    int const which = firstShown();
    int const other = out ? key + 1 : key - 1;
    if (which < 0 || key < 0 || key >= layer.keys.size() || other < 0 || other >= layer.keys.size())
        return QPointF();
    double const ease = std::clamp(m_easeDrag == (out ? 1 : 2) && m_easeKey == key ? m_easeValue : out ? layer.keys[key].easeOut : layer.keys[key].easeIn, 0.0, 1.0);
    double const from = xOfFrame(layer.keys[key].frame), to = xOfFrame(layer.keys[other].frame);
    double const scale = which == Timeline::Opacity ? 100.0 : 1.0;
    double const value = Timeline::parameterOf(layer.keys[key].pose, Timeline::Parameter(which)) * scale;
    double const beyond = Timeline::parameterOf(layer.keys[other].pose, Timeline::Parameter(which)) * scale;
    double const lift = m_easeDrag == (out ? 1 : 2) && m_easeKey == key ? m_liftValue : out ? layer.keys[key].liftOut : layer.keys[key].liftIn;
    // (Out: that share of the way on to the next value.  In: that share of the way come, carried on past this one.)
    double const lifted = out ? value + (beyond - value) * lift : value + (value - beyond) * lift;
    return QPointF(from + (to - from) * (0.1 + 0.4 * ease), yOfValue(lifted));
}

QPointF FunctionGraph::easeHandle(bool out) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return QPointF();
    return easeHandleOf(scene.layers[m_layer], scene.layers[m_layer].keyIndex(m_frame), out);
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
    fit();
    update();
}

void FunctionGraph::fit()
{
    if (m_zoomed)
        return;     // the values were chosen by hand
    // From the lowest to the highest value any curve shown takes, with room above and below.
    Scene const& scene = m_document->scene();
    bool any = false;
    double low = 0.0, high = 0.0;
    for (int which = 0; which < Timeline::ParameterCount; ++which) {
        if (!m_shown[which])
            continue;
        for (int f = 0; f < scene.frameCount(); ++f) {
            double const v = valueAt(Timeline::Parameter(which), f);
            low = any ? std::min(low, v) : v;
            high = any ? std::max(high, v) : v;
            any = true;
            // ... and any curve of the other layers shown with it.
            if (which == Timeline::Fov)
                continue;
            for (int other : m_others) {
                if (other == m_layer || other >= scene.layers.size())
                    continue;
                double const o = Timeline::parameterOf(scene.layers[other].poseAt(f), Timeline::Parameter(which)) * (which == Timeline::Opacity ? 100.0 : 1.0);
                low = std::min(low, o);
                high = std::max(high, o);
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

int FunctionGraph::frameOfX(double x) const
{
    QRectF const r = plot();
    return std::clamp(int(std::lround(m_first + (x - r.left()) * frameSpan() / r.width())), 0, m_document->scene().frameCount() - 1);
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

void FunctionGraph::setGridShown(bool on)
{
    m_grid = on;
    update();
}

void FunctionGraph::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter p(this);
    p.fillRect(rect(), InterfaceColours::get("functionBackground"));
    QRectF const r = plot();
    p.fillRect(r, InterfaceColours::get("functionPlot"));
    p.setClipRect(QRectF(0, 0, width(), height()));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
    p.setFont(small);

    // Lines across at round values, with the value at the left.
    double const span = m_high - m_low;
    double step = std::pow(10.0, std::floor(std::log10(span / 4.0)));
    if (span / step > 10.0)
        step *= span / step > 25.0 ? 5.0 : 2.0;
    for (double v = std::ceil(m_low / step) * step; v <= m_high; v += step) {
        double const y = yOfValue(v);
        p.setPen(std::abs(v) < step / 2.0 ? InterfaceColours::get("functionAxis") : InterfaceColours::get("functionGrid"));
        if (m_grid || std::abs(v) < step / 2.0)
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(QColor(160, 160, 160));
        p.drawText(QRectF(0, y - 7, r.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(std::abs(v) < step / 2.0 ? 0.0 : v, 'g', 6));
    }
    // Lines down every so many frames, with the number of the frame under them.
    int const frames = scene.frameCount();
    int const every = frames <= 30 ? 5 : frames <= 120 ? 10 : frames <= 600 ? 50 : 100;
    for (int f = every - 1; f < frames; f += every) {
        double const x = xOfFrame(f);
        p.setPen(InterfaceColours::get("functionGrid"));
        if (m_grid)
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(QColor(160, 160, 160));
        p.drawText(QRectF(x - 20, r.bottom() + 2, 40, 14), Qt::AlignHCenter | Qt::AlignTop, QString::number(f + 1));
    }

    if (m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setClipRect(r.adjusted(-4, -4, 4, 4));
        // The other layers selected: the same values of theirs, faint and dashed, under the current layer's.
        for (int other : m_others) {
            if (other == m_layer || other >= scene.layers.size())
                continue;
            for (int which = 0; which < Timeline::ParameterCount; ++which) {
                if (!m_shown[which] || which == Timeline::Fov)
                    continue;
                QPolygonF curve;
                for (int f = 0; f < frames; ++f)
                    curve << QPointF(xOfFrame(f), yOfValue(Timeline::parameterOf(scene.layers[other].poseAt(f), Timeline::Parameter(which))
                                                           * (which == Timeline::Opacity ? 100.0 : 1.0)));
                QColor faint = colourOf(Timeline::Parameter(which));
                faint.setAlpha(120);
                QPen pen(faint, 1.2);
                pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(curve);
            }
        }
        for (int which = 0; which < Timeline::ParameterCount; ++which) {
            if (!m_shown[which])
                continue;
            Timeline::Parameter const param = Timeline::Parameter(which);
            QColor const colour = colourOf(param);
            // The curve: the value on each frame.  While a keyframe is dragged, the pose it would give.
            Layer preview;
            Layer const* source = &layer;
            if (m_dragParam == which && m_dragKey >= 0 && m_dragKey < layer.keys.size()) {
                preview = layer;
                preview.keys[m_dragKey].frame = m_dragFrame;
                Timeline::setParameterOf(preview.keys[m_dragKey].pose, param, param == Timeline::Opacity ? m_dragValue / 100.0 : m_dragValue);
                source = &preview;
            } else if (m_easeDrag != 0 && m_easeKey >= 0 && m_easeKey < layer.keys.size()) {
                // ... or while an ease handle is dragged, the ease it would give.
                preview = layer;
                (m_easeDrag == 1 ? preview.keys[m_easeKey].easeOut : preview.keys[m_easeKey].easeIn) = m_easeValue;
                (m_easeDrag == 1 ? preview.keys[m_easeKey].liftOut : preview.keys[m_easeKey].liftIn) = m_liftValue;
                source = &preview;
            }
            bool driven = false;
            for (Driver const& d : layer.drivers)
                driven = driven || (d.value == int(param) && scene.layerIndex(d.source) >= 0);
            if (driven) {
                // It follows another layer (an attribute control): the curve it takes from that one, dashed, no keyframes on it.
                QPolygonF curve;
                for (int f = 0; f < frames; ++f) {
                    double const v = Timeline::parameterOf(scene.poseOf(m_layer, f), param);
                    curve << QPointF(xOfFrame(f), yOfValue(param == Timeline::Opacity ? v * 100.0 : v));
                }
                p.setPen(QPen(colour, 1.6, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawPolyline(curve);
                continue;
            }
            auto value = [&](int f) {
                double const v = Timeline::parameterOf(source->poseAt(f), param);
                return param == Timeline::Opacity ? v * 100.0 : v;
            };
            QPolygonF curve;
            for (int f = 0; f < frames; ++f)
                curve << QPointF(xOfFrame(f), yOfValue(value(f)));
            p.setPen(QPen(colour, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(curve);
            // A square on each keyframe.
            p.setPen(QPen(QColor(24, 24, 24), 1));
            p.setBrush(colour);
            for (Keyframe const& key : source->keys)
                if (key.sets(1 << which))
                    p.drawRect(QRectF(xOfFrame(key.frame) - 3.5, yOfValue(value(key.frame)) - 3.5, 7, 7));
        }
        // The ease handles of the keyframe on the current frame.
        int const here = m_easeDrag != 0 ? m_easeKey : layer.keyIndex(m_frame);
        if (here >= 0 && here < layer.keys.size() && firstShown() >= 0) {
            QPointF const at = pointAt(Timeline::Parameter(firstShown()), layer.keys[here].frame);
            for (bool out : {true, false}) {
                QPointF const handle = easeHandleOf(layer, here, out);
                if (handle.isNull())
                    continue;
                p.setPen(QPen(QColor(235, 235, 235), 1.0));
                p.drawLine(at, handle);
                p.setBrush(QColor(46, 46, 46));
                p.drawEllipse(handle, 3.5, 3.5);
            }
        }
        p.setClipping(false);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    // The current frame.
    p.setPen(InterfaceColours::get("functionCurrentFrame"));
    p.drawLine(QPointF(xOfFrame(m_frame), r.top()), QPointF(xOfFrame(m_frame), r.bottom()));
}

int FunctionGraph::keyAt(QPointF pos, int* param) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return -1;
    Layer const& layer = scene.layers[m_layer];
    for (int which = Timeline::ParameterCount - 1; which >= 0; --which) {
        if (!m_shown[which])
            continue;
        for (int k = 0; k < layer.keys.size(); ++k) {
            if (layer.keys[k].sets(1 << which) && QLineF(pointAt(Timeline::Parameter(which), layer.keys[k].frame), pos).length() <= 6.0) {
                if (param)
                    *param = which;
                return k;
            }
        }
    }
    return -1;
}

void FunctionGraph::mouseDoubleClickEvent(QMouseEvent* event)
{
    // The middle button twice: all of the graph again.
    if (event->button() == Qt::MiddleButton) {
        m_sliding = false;
        resetView();
        return;
    }
    // On the graph where there is no keyframe: one is made on that frame, with the pose the layer has there.
    Scene const& scene = m_document->scene();
    if (event->button() != Qt::LeftButton || m_layer < 0 || m_layer >= scene.layers.size() || keyAt(event->position()) >= 0)
        return;
    int const frame = frameOfX(event->position().x());
    if (scene.layers[m_layer].keyIndex(frame) < 0)
        m_document->setKeyframe(m_layer, frame, scene.layers[m_layer].poseAt(frame), tr("Add Keyframe"));
    emit framePicked(frame);
}

void FunctionGraph::keyPressEvent(QKeyEvent* event)
{
    // Delete: the keyframe on the current frame goes.
    Scene const& scene = m_document->scene();
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_layer >= 0 && m_layer < scene.layers.size()
        && scene.layers[m_layer].keyIndex(m_frame) >= 0) {
        m_document->removeKeyframe(m_layer, m_frame);
        return;
    }
    QWidget::keyPressEvent(event);
}

void FunctionGraph::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton) {
        m_sliding = true;
        m_slideLast = event->position();
        return;
    }
    if (event->button() == Qt::RightButton) {
        // The right button on a keyframe takes it away.
        Scene const& scene = m_document->scene();
        int const key = keyAt(event->position());
        if (key >= 0 && m_layer < scene.layers.size())
            m_document->removeKeyframe(m_layer, scene.layers[m_layer].keys[key].frame);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    Scene const& scene = m_document->scene();
    // On an ease handle of the keyframe on this frame: it is pulled along.
    if (m_layer >= 0 && m_layer < scene.layers.size()) {
        int const here = scene.layers[m_layer].keyIndex(m_frame);
        for (bool out : {true, false}) {
            QPointF const handle = easeHandleOf(scene.layers[m_layer], here, out);
            if (!handle.isNull() && QLineF(handle, event->position()).length() <= 6.0) {
                m_easeDrag = out ? 1 : 2;
                m_easeKey = here;
                m_easeValue = out ? scene.layers[m_layer].keys[here].easeOut : scene.layers[m_layer].keys[here].easeIn;
                m_liftValue = out ? scene.layers[m_layer].keys[here].liftOut : scene.layers[m_layer].keys[here].liftIn;
                return;
            }
        }
    }
    // On the square of a keyframe: it is taken hold of.  The curve drawn last is on top.
    if (m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        for (int which = Timeline::ParameterCount - 1; which >= 0; --which) {
            if (!m_shown[which])
                continue;
            for (int k = 0; k < layer.keys.size(); ++k) {
                QPointF const at = pointAt(Timeline::Parameter(which), layer.keys[k].frame);
                if (layer.keys[k].sets(1 << which) && QLineF(at, event->position()).length() <= 6.0) {
                    m_dragParam = which;
                    m_dragKey = k;
                    m_dragValue = valueAt(Timeline::Parameter(which), layer.keys[k].frame);
                    m_dragFrame = layer.keys[k].frame;
                    emit framePicked(layer.keys[k].frame);
                    return;
                }
            }
        }
    }
    // Anywhere else: that frame.
    emit framePicked(frameOfX(event->position().x()));
}

void FunctionGraph::mouseMoveEvent(QMouseEvent* event)
{
    if (m_sliding) {
        // The graph goes with the pointer: the frames (when not all are shown) and the values.
        QRectF const r = plot();
        QPointF const d = event->position() - m_slideLast;
        m_slideLast = event->position();
        if (m_span > 0.0) {
            double const whole = std::max(2, m_document->scene().frameCount()) - 1.0;
            m_first = std::clamp(m_first - d.x() * m_span / r.width(), 0.0, whole - m_span);
        }
        double const by = d.y() * (m_high - m_low) / r.height();
        m_low += by;
        m_high += by;
        m_zoomed = true;
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if (m_easeDrag != 0) {
        // How far along the way to the next keyframe (or back to the one before) the handle is.
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && m_easeKey >= 0 && m_easeKey < scene.layers[m_layer].keys.size()) {
            QVector<Keyframe> const& keys = scene.layers[m_layer].keys;
            int const other = m_easeDrag == 1 ? m_easeKey + 1 : m_easeKey - 1;
            if (other >= 0 && other < keys.size()) {
                double const from = xOfFrame(keys[m_easeKey].frame), to = xOfFrame(keys[other].frame);
                if (std::abs(to - from) > 1e-9)
                    m_easeValue = std::clamp(((event->position().x() - from) / (to - from) - 0.1) / 0.4, 0.0, 1.0);
                int const which = firstShown();
                if (which >= 0) {
                    double const scale = which == Timeline::Opacity ? 100.0 : 1.0;
                    double const value = Timeline::parameterOf(keys[m_easeKey].pose, Timeline::Parameter(which)) * scale;
                    double const beyond = Timeline::parameterOf(keys[other].pose, Timeline::Parameter(which)) * scale;
                    double const way = m_easeDrag == 1 ? beyond - value : value - beyond;
                    if (std::abs(way) > 1e-9) {
                        m_liftValue = std::clamp((valueOfY(event->position().y()) - value) / way, -2.0, 2.0);
                        if (std::abs(yOfValue(value) - event->position().y()) < 5.0 || (event->modifiers() & Qt::ShiftModifier))
                            m_liftValue = 0.0;
                    }
                }
            }
        }
        update();
        return;
    }
    if (m_dragKey >= 0) {
        m_dragValue = valueOfY(event->position().y());
        if (m_dragParam == Timeline::Opacity)
            m_dragValue = std::clamp(m_dragValue, 0.0, 100.0);
        // Sideways it goes to other frames, as far as the keyframes either side of it.
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && m_dragKey < scene.layers[m_layer].keys.size()) {
            QVector<Keyframe> const& keys = scene.layers[m_layer].keys;
            int const least = m_dragKey > 0 ? keys[m_dragKey - 1].frame + 1 : 0;
            int const most = m_dragKey + 1 < keys.size() ? keys[m_dragKey + 1].frame - 1 : scene.frameCount() - 1;
            m_dragFrame = std::clamp(frameOfX(event->position().x()), least, most);
        }
        update();
    } else {
        emit framePicked(frameOfX(event->position().x()));
    }
}

void FunctionGraph::mouseReleaseEvent(QMouseEvent*)
{
    m_sliding = false;
    if (m_easeDrag != 0) {
        Scene const& scene = m_document->scene();
        int const which = m_easeDrag, key = m_easeKey;
        m_easeDrag = 0;
        m_easeKey = -1;
        if (m_layer >= 0 && m_layer < scene.layers.size() && key >= 0 && key < scene.layers[m_layer].keys.size()) {
            Keyframe const at = scene.layers[m_layer].keys[key];
            m_document->setKeyEase(m_layer, at.frame, which == 2 ? m_easeValue : at.easeIn, which == 1 ? m_easeValue : at.easeOut,
                                   which == 2 ? m_liftValue : at.liftIn, which == 1 ? m_liftValue : at.liftOut);
        }
        update();
        return;
    }
    if (m_dragKey < 0)
        return;
    Scene const& scene = m_document->scene();
    int const key = m_dragKey;
    Timeline::Parameter const param = Timeline::Parameter(m_dragParam);
    double const value = param == Timeline::Opacity ? m_dragValue / 100.0 : m_dragValue;
    m_dragKey = m_dragParam = -1;
    if (m_layer >= 0 && m_layer < scene.layers.size() && key < scene.layers[m_layer].keys.size()) {
        Keyframe const at = scene.layers[m_layer].keys[key];
        Pose pose = at.pose;
        bool const valued = Timeline::parameterOf(pose, param) != value;
        bool const moved = m_dragFrame != at.frame;
        // The other layers shown with it: their keyframes on that frame go up or down by as much.
        QVector<QPair<int, Pose>> along;
        if (valued && !moved && param != Timeline::Fov) {
            double const by = value - Timeline::parameterOf(at.pose, param);
            for (int other : std::as_const(m_others)) {
                if (other < 0 || other >= scene.layers.size() || other == m_layer)
                    continue;
                int const theirs = scene.layers[other].keyIndex(at.frame);
                if (theirs < 0)
                    continue;
                Pose changed = scene.layers[other].keys[theirs].pose;
                double to = Timeline::parameterOf(changed, param) + by;
                if (param == Timeline::Opacity)
                    to = std::clamp(to, 0.0, 1.0);
                Timeline::setParameterOf(changed, param, to);
                along.push_back({other, changed});
            }
        }
        Timeline::setParameterOf(pose, param, value);
        bool const several = (moved && valued) || !along.isEmpty();
        QString const setting = tr("Set %1").arg(Timeline::parameterName(param));
        if (several)
            m_document->beginMacro(moved ? tr("Move Keyframe") : setting);
        if (moved)
            m_document->moveKeyframe(m_layer, at.frame, m_dragFrame);
        if (valued && param == Timeline::Fov)
            m_document->setFov(m_layer, value, moved ? m_dragFrame : at.frame);     // the other keyframes are given the angle they stood for
        else if (valued)
            m_document->setKeyframe(m_layer, moved ? m_dragFrame : at.frame, pose, setting);
        for (QPair<int, Pose> const& other : std::as_const(along))
            m_document->setKeyframe(other.first, at.frame, other.second, setting);
        if (several)
            m_document->endMacro();
        if (moved)
            emit framePicked(m_dragFrame);
    }
    update();
}

// ---- the view --------------------------------------------------------------------------------

FunctionView::FunctionView(Document* document, QWidget* parent) : QWidget(parent)
{
    m_graph = new FunctionGraph(document, this);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("FunctionList"));
    m_list->setFixedWidth(110);
    for (int which = 0; which < Timeline::ParameterCount; ++which) {
        Timeline::Parameter const param = Timeline::Parameter(which);
        QPixmap swatch(12, 12);
        swatch.fill(FunctionGraph::colourOf(param));
        QListWidgetItem* const item = new QListWidgetItem(QIcon(swatch), Timeline::parameterName(param), m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_graph->isShown(param) ? Qt::Checked : Qt::Unchecked);
    }
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        m_graph->setShown(Timeline::Parameter(m_list->row(item)), item->checkState() == Qt::Checked);
    });
    connect(m_graph, &FunctionGraph::framePicked, this, &FunctionView::framePicked);
    QHBoxLayout* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list);
    layout->addWidget(m_graph, 1);
}
