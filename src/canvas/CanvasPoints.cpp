// Eclipse - the Camera view: point animation - the Contour Editor moving a drawing's own points on keyframes.
#include "canvas/Canvas.h"

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

bool Canvas::pointAnimating() const
{
    if (!m_animatePoints || !m_animate || m_tool != ContourEditor)
        return false;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return false;
    Layer const& l = scene.layers[m_layer];
    return l.holdsDrawings() && !l.showsSymbol() && !l.morphing(m_frame) && l.drawingAt(m_frame) != Layer::kNoDrawing;
}

Drawing const* Canvas::pointViewOf(Drawing const& rest, int index) const
{
    Pose const pose = m_document->scene().layers[m_layer].poseAt(m_frame);
    QVector<PointMove> mine;
    for (PointMove const& m : pose.points)
        if (m.drawing == index)
            mine.push_back(m);
    QVector<void const*> key;
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        key << rest.strokesOf(sheet).constData() << rest.fillsOf(sheet).constData();
    if (m_pointViewLayer == m_layer && m_pointViewIndex == index && m_pointViewRest == key && m_pointViewMoves == mine)
        return &m_pointView;
    m_pointViewLayer = m_layer;
    m_pointViewIndex = index;
    m_pointViewRest = key;
    m_pointViewMoves = mine;
    m_pointView = Deform::movedPoints(rest, index, mine);
    return &m_pointView;
}

Drawing const& Canvas::layerDrawing(int layer, int index) const
{
    Layer const& l = m_document->scene().layers[layer];
    if (layer == m_layer && index == l.drawingAt(m_frame) && pointAnimating())
        return *realDrawing();
    return l.drawings[index];
}

void Canvas::commitPointMoves(Drawing const& whole, QString const& text)
{
    if (!canEditDrawing())
        return;
    Layer const& l = m_document->scene().layers[m_layer];
    int const index = l.drawingAt(m_frame);
    QVector<PointMove> mine;
    if (!Deform::pointMoves(l.drawings[index], whole, index, mine)) {
        emit message(tr("Animate Points only moves the points a drawing has: turn it off to add, delete or join points."));
        update();
        return;
    }
    Pose pose = l.poseAt(m_frame);
    pose.points = Deform::withMoves(pose.points, index, mine);
    m_committing = true;
    m_document->setPartKeyframe(m_layer, m_frame, Keyframe::PartRest, pose, text);
    m_committing = false;
}

void Canvas::onActionAnimatePoints(bool on)
{
    m_animatePoints = on;
    emit message(on ? tr("Animate Points: with Animate on, the Contour Editor moves the drawing's points on a keyframe of this frame.") : QString());
    emit toolSettingsChanged();
    update();
}

void Canvas::onActionResetPoints()
{
    if (!canEditDrawing())
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const& l = scene.layers[m_layer];
    int const index = l.drawingAt(m_frame);
    Pose pose = l.poseAt(m_frame);
    if (index == Layer::kNoDrawing || !Deform::movesPoints(pose.points, index)) {
        emit message(tr("No points of this drawing are moved on this frame."));
        return;
    }
    pose.points = Deform::withMoves(pose.points, index, {});
    m_document->setPartKeyframe(m_layer, m_frame, Keyframe::PartRest, pose, tr("Reset Animated Points"));
}

Drawing Canvas::followedPreview() const
{
    if (!pointAnimating())
        return followLines(withArt(previewDrawing()), realDrawing(), true);
    Layer const& l = m_document->scene().layers[m_layer];
    int const index = l.drawingAt(m_frame);
    Drawing const whole = withArt(previewDrawing());
    QVector<PointMove> mine;
    return Deform::pointMoves(l.drawings[index], whole, index, mine) ? Deform::movedPoints(l.drawings[index], index, mine) : whole;
}
