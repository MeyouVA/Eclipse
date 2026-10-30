// Eclipse - the Camera view: how the scene is looked at - zoom, turn, onion skin, from layer to view and back.
#include "canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

void Canvas::setDrawingView(bool on)
{
    if (on == m_drawingView)
        return;
    m_drawingView = on;
    // Each of the two views keeps how it was looked at: the first time, the other begins as this one is.
    if (m_otherViewSet) {
        std::swap(m_zoom, m_otherZoom);
        std::swap(m_pan, m_otherPan);
        std::swap(m_viewRotation, m_otherRotation);
    } else {
        m_otherZoom = m_zoom;
        m_otherPan = m_pan;
        m_otherRotation = m_viewRotation;
        m_otherViewSet = true;
    }
    update();
    emit zoomChanged();
    emit drawingViewChanged(on);
}

QTransform Canvas::viewTransform() const
{
    if (m_planeView)
        return m_planeProjection;
    if (m_drawingView)
        return cameraViewTransform();   // no camera: the sheet as it is
    return m_document->scene().cameraTransform(m_frame) * cameraViewTransform();
}

QTransform Canvas::cameraViewTransform() const
{
    if (m_planeView)
        return m_planeProjection;
    Scene const& scene = m_document->scene();
    QTransform t;
    t.translate(width() / 2.0 + m_pan.x(), height() / 2.0 + m_pan.y());
    t.rotate(m_viewRotation);
    t.scale(m_mirror ? -m_zoom : m_zoom, m_zoom);
    t.translate(-scene.width / 2.0, -scene.height / 2.0);
    return t;
}

void Canvas::rotateView(double degrees)
{
    // About the middle of the view: what is there stays there.
    QPointF const anchor(width() / 2.0, height() / 2.0);
    QPointF const scenePoint = toScene(anchor);
    m_viewRotation = std::remainder(m_viewRotation + degrees, 360.0);
    if (std::abs(m_viewRotation) < 1e-9)
        m_viewRotation = 0.0;
    m_pan += anchor - viewTransform().map(scenePoint);
    m_fitted = false;
    update();
}

void Canvas::onActionMirrorView(bool on)
{
    QPointF const anchor(width() / 2.0, height() / 2.0);
    QPointF const scenePoint = toScene(anchor);
    m_mirror = on;
    m_pan += anchor - viewTransform().map(scenePoint);
    update();
}

void Canvas::onActionResetPan()
{
    m_pan = QPointF();
    update();
}

void Canvas::onActionRecentre()
{
    QPointF const pointer = mapFromGlobal(QCursor::pos());
    if (rect().contains(pointer.toPoint()))
        recentreOn(pointer);    // (the pointer not over the view: nothing to put in the middle)
}

void Canvas::recentreOn(QPointF widgetPos)
{
    m_pan += QPointF(width() / 2.0, height() / 2.0) - widgetPos;
    m_fitted = false;
    update();
}

void Canvas::onActionFrameSelection()
{
    if (!hasSelection()) {
        emit message(tr("Select something first: it is framed in the view."));
        return;
    }
    QRectF const box = layerViewTransform().mapRect(selectionBounds());
    if (box.width() < 1e-6 && box.height() < 1e-6)
        return;
    double const factor = std::clamp(std::min(width() * 0.8 / std::max(box.width(), 1e-6), height() * 0.8 / std::max(box.height(), 1e-6)), 1e-3, 1e3);
    zoomAt(box.center(), factor);
    recentreOn(layerViewTransform().mapRect(selectionBounds()).center());
}

void Canvas::onActionQuickCloseup()
{
    QPointF pointer = mapFromGlobal(QCursor::pos());
    if (!rect().contains(pointer.toPoint()))
        pointer = QPointF(width() / 2.0, height() / 2.0);
    quickCloseupAt(pointer);
}

void Canvas::quickCloseupAt(QPointF pointer)
{
    if (m_closeup) {
        m_closeup = false;
        m_zoom = m_closeupZoom;
        m_pan = m_closeupPan;
        emit zoomChanged();
        update();
        return;
    }
    m_closeup = true;
    m_closeupZoom = m_zoom;
    m_closeupPan = m_pan;
    zoomAt(pointer, m_quickFactor);
    m_pan += QPointF(width() / 2.0, height() / 2.0) - pointer;
    update();
}

void Canvas::onActionBacklight(bool on)
{
    m_backlight = on;
    update();
}

void Canvas::onActionShowSymbolPivot(bool on)
{
    m_symbolPivots = on;
    update();
}

QPointF Canvas::pivotInView(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return QPointF();
    return (worldOf(layer, m_frame) * viewTransform()).map(scene.layers[layer].pivot);
}

void Canvas::onActionOutlineLocked(bool on)
{
    m_outlineLocked = on;
    update();
}

void Canvas::onActionCurrentDrawingOnTop(bool on)
{
    m_currentOnTop = on;
    update();
}

void Canvas::onActionCameraMask(bool on)
{
    m_cameraMask = on;
    update();
}

void Canvas::onActionShowControl(bool on)
{
    if (m_showControl == on)
        return;
    bool const oldControls = showsPlanningControls();
    m_showControl = on;
    update();
    emit showControlChanged(on);
    if (oldControls != showsPlanningControls())
        emit planningControlsChanged();
}

void Canvas::onActionHideAllControls()
{
    onActionShowControl(false);
    clearSelection();
    m_ceStroke = m_cePoint = m_ceFill = -1;
    emit pointPicked(-1);
    update();
}

void Canvas::setOnionRange(int previous, int next)
{
    m_onionPrev = std::clamp(previous, 0, 6);
    m_onionNext = std::clamp(next, 0, 6);
    update();
    emit onionChanged();
}

void Canvas::setOnionOpacity(int percent)
{
    m_onionOpacity = std::clamp(percent, 5, 100);
    update();
    emit onionChanged();
}

void Canvas::setOnionStrength(bool after, int step, int percent)
{
    if (step < 0 || step >= kOnionSteps)
        return;
    int const value = std::clamp(percent, 0, 100);
    if (m_onionStrength[after ? 1 : 0][step] == value)
        return;
    m_onionStrength[after ? 1 : 0][step] = value;
    update();
    emit onionChanged();
}

void Canvas::onActionLightTable(bool on)
{
    m_lightTable = on;
    update();
}

QPointF Canvas::toScene(QPointF widgetPos) const
{
    return viewTransform().inverted().map(widgetPos);
}

PoseOverride Canvas::dragged() const
{
    PoseOverride over;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return over;
    if (posing()) {
        over.layer = m_layer;
        over.frame = m_frame;
        over.pose = m_dragPose;
        over.others = posedOthers();
    } else if (m_drag == DragSplineOffset) {
        // The layer where its moved path has it on this frame.
        over.layer = m_layer;
        over.frame = m_frame;
        over.pose = scene.layers[m_layer].poseAt(m_frame);
        QPointF const by = splineOffset();
        over.pose.x += by.x();
        over.pose.y += by.y();
    } else if (m_drag == DragQuad) {
        Layer const& l = scene.layers[m_layer];
        over.layer = m_layer;
        over.frame = m_frame;
        over.hasMatrix = true;
        over.matrix = Layer::quadWarp(l.quadSize, m_dragQuad) * Layer::matrix(l.poseAt(m_frame), l.pivot);
    }
    return over;
}

QPointF Canvas::splineOffset() const
{
    // The pen's move, seen in the space of what the layer hangs from (on this frame).
    Scene const& scene = m_document->scene();
    int const parent = m_layer >= 0 && m_layer < scene.layers.size() ? scene.parentIndex(m_layer) : -1;
    QTransform const toView = (parent >= 0 ? scene.worldTransform(parent, m_frame) : QTransform()) * viewTransform();
    if (!toView.isInvertible())
        return QPointF();
    QTransform const back = toView.inverted();
    return back.map(m_splineNow) - back.map(m_splineFrom);
}

QTransform Canvas::worldOf(int layer, int frame) const
{
    if (m_drawingView)
        return QTransform();            // flat: no layer, peg or bone moves the sheet
    PoseOverride const over = dragged();
    return m_document->scene().worldTransform(layer, frame, &over);
}

QTransform Canvas::parentSeen() const
{
    Scene const& scene = m_document->scene();
    int const parent = scene.parentIndex(m_layer);
    QTransform flat = scene.flatTransform(parent, m_frame);
    if (scene.layers[m_layer].kind == Layer::Camera)
        return flat;
    int const align = scene.cameraAlignmentOf(m_layer);
    // (Pinned to the camera by its own setting: the camera carries it, though not what it hangs under.)
    if (align == Layer::PinToCamera && scene.cameraAlignmentOf(parent) != Layer::PinToCamera && scene.cameraIndex() >= 0)
        flat = flat * scene.cameraTransform(m_frame).inverted();
    if (align == Layer::ProjectOnCameraPlane)
        return flat;
    return flat * scene.depthTransform(scene.depthOf(m_layer, m_frame), m_frame);
}

void Canvas::zoomByShortcut(double factor)
{
    QPoint const pointer = mapFromGlobal(QCursor::pos());
    if (m_zoomOnMouse && rect().contains(pointer))
        zoomAt(QPointF(pointer), factor);
    else
        zoomBy(factor);
}

Pose Canvas::poseOf(int layer, int frame) const
{
    if (layer == m_layer && frame == m_frame && posing())
        return m_dragPose;
    return m_document->scene().poseOf(layer, frame);
}

bool Canvas::showsRest(int layer) const
{
    return layer == m_layer && !m_playing && m_tool != Transform && m_tool != Hand && m_tool != IK;
}

Drawing Canvas::shown(int layer, int frame, Drawing const& drawing) const
{
    Scene const& scene = m_document->scene();
    Layer const& l = scene.layers[layer];
    bool const plain = ((l.bones.size() < 2 && !l.hasWarp()) || showsRest(layer)) && drawing.model.isEmpty();
    // (And on the curve of a deformer of a layer it is attached to.)
    bool const rides = !showsRest(layer) && l.parent != 0;      // (and bends on bones it hangs from)
    // The active drawing supplies its point pose to both rendering and tool input.
    Pose pose = poseOf(layer, frame);
    bool const posedAlready = layer == m_layer && frame == m_frame && pointsPosed();
    bool const moves = !pose.points.isEmpty() && !posedAlready && !l.morphing(frame);
    Drawing const moved = moves ? Deform::movedPoints(drawing, l.drawingAt(frame), pose.points) : Drawing();
    Drawing const& base = moves ? moved : drawing;
    pose.points.clear();
    if (!m_highlightColour || m_playing) {
        Drawing const own = plain ? base : Deform::shown(base, l, pose);
        return rides ? Deform::ridden(scene, layer, frame, own) : own;
    }
    // Highlight Selected Colour: what is not of the colour in hand is shown faint.
    Drawing made = plain ? base : Deform::shown(base, l, pose);
    if (rides)
        made = Deform::ridden(scene, layer, frame, made);
    auto fade = [&](int& swatch, QColor& colour) {
        if (swatch == m_swatch)
            return;
        colour = scene.resolve(swatch, colour);
        colour.setAlphaF(colour.alphaF() * float(m_highlightColourOpacity) / 100.0f);
        swatch = Scene::kNoSwatch;
    };
    for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::LineSheet), int(Drawing::OverlaySheet)}) {
        for (Fill& fill : made.fillsOf(sheet))
            fade(fill.swatch, fill.color);
        for (Stroke& stroke : made.strokesOf(sheet))
            fade(stroke.swatch, stroke.color);
    }
    return made;
}

void Canvas::onActionHighlightColour(bool on)
{
    m_highlightColour = on;
    update();
}

QTransform Canvas::layerViewTransform() const
{
    return worldOf(m_layer, m_frame) * viewTransform();
}

QPointF Canvas::toLayer(QPointF widgetPos) const
{
    return layerViewTransform().inverted().map(widgetPos);
}

void Canvas::fitToWindow()
{
    Scene const& scene = m_document->scene();
    double const margin = 24.0;
    double const zx = (width() - 2 * margin) / scene.width;
    double const zy = (height() - 2 * margin) / scene.height;
    m_zoom = std::max(0.02, std::min(zx, zy));
    emit zoomChanged();
    m_pan = QPointF();
    m_viewRotation = 0.0;
    m_fitted = true;
    update();
}

void Canvas::resizeEvent(QResizeEvent*)
{
    if (m_fitted)
        fitToWindow();
}
