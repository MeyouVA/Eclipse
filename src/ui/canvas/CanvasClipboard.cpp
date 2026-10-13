// Eclipse - the Camera view: cut, copy, paste and delete.
#include "ui/canvas/Canvas.h"

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "ui/panels/Effects.h"

// What Copy put aside: shared by every canvas, lost when the program closes.
static Drawing s_clipboard;

void Canvas::deleteSelection()
{
    if (m_tool == Reposition)
        return;     // it puts drawings elsewhere; it takes nothing away
    if (m_tool == Rigging && m_rigMode != RigBones) {
        // The last point of the deformer; the one before it then ends the curve.
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && !scene.layers[m_layer].warp.isEmpty()) {
            Layer const& layer = scene.layers[m_layer];
            QVector<QPointF> points = layer.warp;
            points.resize(points.size() - 3);
            Deform::smoothWarpHandles(points, layer.warpClosed, int(points.size()) / 3 - 1);
            if (layer.warpClosed)
                Deform::smoothWarpHandles(points, true, 0);
            m_document->setWarp(m_layer, points, layer.warpClosed, tr("Delete Deformer Point"));
        }
        return;
    }
    if (m_tool == Rigging) {
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && !scene.layers[m_layer].bones.isEmpty()) {
            QVector<QPointF> joints = scene.layers[m_layer].bones;
            joints.removeLast();
            m_document->setBones(m_layer, joints, tr("Delete Bone"));
        }
        return;
    }
    if (m_tool == ContourEditor) {
        // The picked point; a line cannot have fewer than two, so then the line goes.
        if (!contourValid())
            return;
        Drawing edited = *currentDrawing();
        // Every point picked goes; from the last to the first, so that the places of the others hold.
        QVector<int> going = contourPointsPicked();
        going.push_back(m_cePoint);
        std::sort(going.begin(), going.end(), std::greater<int>());
        m_ceExtra.clear();
        m_cePoint = going.takeLast();
        // The others first; the one left (the first of them) goes below, as a single point does.
        int const fewest = m_ceFill >= 0 ? 3 : 2;
        if (Stroke* const line = contourLine(edited)) {
            if (int(line->points.size()) - int(going.size()) > fewest) {
                for (int point : std::as_const(going))
                    line->points.removeAt(point);
            } else if (!going.isEmpty()) {
                line->points.resize(fewest);    // too few would be left: the curve goes (below)
                m_cePoint = 0;
            }
        }
        if (m_ceFill >= 0) {
            // A contour needs three points; with fewer it goes, and the shape with its last contour.
            Fill& shape = edited.fills[m_ceFill];
            if (shape.contours[m_ceContour].points.size() <= 3) {
                shape.contours.removeAt(m_ceContour);
                if (shape.contours.isEmpty())
                    edited.fills.removeAt(m_ceFill);
                else
                    shape.rebuild();
                m_ceFill = -1;
            } else {
                shape.contours[m_ceContour].points.removeAt(m_cePoint);
                shape.rebuild();
            }
        } else if (edited.strokes[m_ceStroke].points.size() <= 2) {
            edited.strokes.removeAt(m_ceStroke);
            m_ceStroke = -1;
        } else {
            edited.strokes[m_ceStroke].points.removeAt(m_cePoint);
        }
        m_cePoint = -1;
        commitDrawing(edited, tr("Delete Point"));
        update();
        return;
    }
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return;
    Drawing kept;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            kept.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            kept.fills.push_back(d->fills[i]);
    // The pictures selected go too (they lie with the line art).
    bool const picturesGo = m_art == LineArt && !m_selPictures.isEmpty();
    if (picturesGo)
        for (int i = 0; i < d->pictures.size(); ++i)
            if (!m_selPictures.contains(i))
                kept.pictures.push_back(d->pictures[i]);
    QRectF const region = selectionBounds();
    if (m_applyAll && m_tool == Select)
        m_document->beginMacro(tr("Delete"));
    m_keepNoPictures = picturesGo;
    commitDrawing(kept, tr("Delete"));
    m_keepNoPictures = false;
    if (m_applyAll && m_tool == Select) {
        alsoInOtherDrawings(region, nullptr, tr("Delete"));
        m_document->endMacro();
    }
    setSelection({}, {});
}

void Canvas::copySelection()
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return;
    s_clipboard = Drawing();
    for (int i : m_selStrokes)
        s_clipboard.strokes.push_back(d->strokes[i]);
    for (int i : m_selFills)
        s_clipboard.fills.push_back(d->fills[i]);
}

void Canvas::cutSelection()
{
    if (!hasSelection())
        return;
    copySelection();
    Drawing const* const d = currentDrawing();
    Drawing kept;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (!m_selStrokes.contains(i))
            kept.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (!m_selFills.contains(i))
            kept.fills.push_back(d->fills[i]);
    commitDrawing(kept, tr("Cut"));
    setSelection({}, {});
}

bool Canvas::canPaste()
{
    return !s_clipboard.isEmpty();
}

void Canvas::paste()
{
    if (s_clipboard.isEmpty() || m_playing)
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Drawing const* const d = currentDrawing();
    Drawing merged = d ? *d : Drawing();
    QVector<int> strokes, fills;
    for (Fill const& f : s_clipboard.fills) {
        fills.push_back(int(merged.fills.size()));
        merged.fills.push_back(f);
    }
    for (Stroke const& s : s_clipboard.strokes) {
        strokes.push_back(int(merged.strokes.size()));
        merged.strokes.push_back(s);
    }
    commitDrawing(merged, tr("Paste"));
    if (selecting())
        setSelection(strokes, fills);
}
