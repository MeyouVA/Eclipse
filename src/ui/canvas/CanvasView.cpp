// Eclipse - the Camera view: how the scene is looked at - zoom, turn, onion skin, from layer to view and back.
#include "ui/canvas/Canvas.h"

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
    if (m_drawingView)
        return cameraViewTransform();   // no camera: the sheet as it is
    // The stored camera, not one being dragged: the view holds still while the transform
    // tool moves the camera, and follows when the drag ends.
    return m_document->scene().cameraTransform(m_frame) * cameraViewTransform();
}

QTransform Canvas::cameraViewTransform() const
{
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

void Canvas::onActionBacklight(bool on)
{
    m_backlight = on;
    update();
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
    m_showControl = on;
    update();
    emit showControlChanged(on);
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
    } else if (m_drag == DragQuad) {
        Layer const& l = scene.layers[m_layer];
        over.layer = m_layer;
        over.frame = m_frame;
        over.hasMatrix = true;
        over.matrix = Layer::quadWarp(l.quadSize, m_dragQuad) * Layer::matrix(l.poseAt(m_frame), l.pivot);
    }
    return over;
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
    QTransform const flat = scene.flatTransform(scene.parentIndex(m_layer), m_frame);
    if (scene.layers[m_layer].kind == Layer::Camera)
        return flat;
    return flat * scene.depthTransform(scene.depthOf(m_layer, m_frame), m_frame);
}

Pose Canvas::poseOf(int layer, int frame) const
{
    if (layer == m_layer && frame == m_frame && posing())
        return m_dragPose;
    return m_document->scene().layers[layer].poseAt(frame);
}

bool Canvas::showsRest(int layer) const
{
    // Only the transform tool and the hand look at the scene; every other tool works on
    // the artwork of the current layer.
    return layer == m_layer && !m_playing && m_tool != Transform && m_tool != Hand && m_tool != IK;
}

Drawing Canvas::shown(int layer, int frame, Drawing const& drawing) const
{
    Scene const& scene = m_document->scene();
    Layer const& l = scene.layers[layer];
    bool const plain = (l.bones.size() < 2 && !l.hasWarp()) || showsRest(layer);
    if (!m_highlightColour || m_playing)
        return plain ? drawing : Deform::shown(drawing, l, poseOf(layer, frame));
    // Highlight Selected Colour: what is not of the colour in hand is shown faint.
    Drawing made = plain ? drawing : Deform::shown(drawing, l, poseOf(layer, frame));
    auto fade = [&](int& swatch, QColor& colour) {
        if (swatch == m_swatch)
            return;
        colour = scene.resolve(swatch, colour);
        colour.setAlphaF(colour.alphaF() * 0.18f);
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
