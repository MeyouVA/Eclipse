// Eclipse - the Camera view: cut, copy, paste and delete.
#include "ui/canvas/Canvas.h"
#include "ui/timeline/Timeline.h"

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
        // (A line's thickness of its own stays where it was along it.)
        QVector<QPair<double, ThicknessPoint>> thick;
        if (m_ceFill < 0 && m_ceStroke >= 0 && m_ceStroke < edited.strokes.size() && !edited.strokes[m_ceStroke].thickness.isEmpty())
            thick = edited.strokes[m_ceStroke].thicknessByLength();
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
            if (!thick.isEmpty())
                edited.strokes[m_ceStroke].setThicknessByLength(thick);
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
    bool const picturesGo = picturesHere() && !m_selPictures.isEmpty();
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

void Canvas::onActionDeleteNonSelection()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select what is to stay first: the rest of the drawing goes."));
        return;
    }
    Drawing kept;
    for (int i = 0; i < d->strokes.size(); ++i)
        if (m_selStrokes.contains(i))
            kept.strokes.push_back(d->strokes[i]);
    for (int i = 0; i < d->fills.size(); ++i)
        if (m_selFills.contains(i))
            kept.fills.push_back(d->fills[i]);
    bool const pictures = picturesHere();
    if (pictures)
        for (int i = 0; i < d->pictures.size(); ++i)
            if (m_selPictures.contains(i))
                kept.pictures.push_back(d->pictures[i]);
    m_keepNoPictures = pictures;
    commitDrawing(kept, tr("Delete Non Selection"));
    m_keepNoPictures = false;
    // What was selected is all there is now, in the same order: all of it selected.
    QVector<int> strokes, fills, kept_pictures;
    for (int i = 0; i < kept.strokes.size(); ++i)
        strokes << i;
    for (int i = 0; i < kept.fills.size(); ++i)
        fills << i;
    for (int i = 0; pictures && i < kept.pictures.size(); ++i)
        kept_pictures << i;
    setSelection(strokes, fills, kept_pictures);
}

void Canvas::copySelection()
{
    if (m_tool == ContourEditor) {
        // The points picked on the curve.
        Drawing const* const d = currentDrawing();
        Stroke const* const line = d && contourValid() ? contourLine(*d) : nullptr;
        if (!line)
            return;
        QVector<int> picked = contourPointsPicked();
        picked << m_cePoint;
        std::sort(picked.begin(), picked.end());
        s_pointClipboard.clear();
        for (int i : std::as_const(picked))
            s_pointClipboard << line->points[i];
        return;
    }
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return;
    s_clipboard = Drawing();
    Timeline::setArtworkCopiedLast(true);
    for (int i : m_selStrokes)
        s_clipboard.strokes.push_back(d->strokes[i]);
    for (int i : m_selFills)
        s_clipboard.fills.push_back(d->fills[i]);
    if (picturesHere())
        for (int i : m_selPictures)
            if (i >= 0 && i < d->pictures.size())
                s_clipboard.pictures.push_back(d->pictures[i]);
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
    bool const picturesGo = picturesHere() && !m_selPictures.isEmpty();
    if (picturesGo)
        for (int i = 0; i < d->pictures.size(); ++i)
            if (!m_selPictures.contains(i))
                kept.pictures.push_back(d->pictures[i]);
    m_keepNoPictures = picturesGo;
    commitDrawing(kept, tr("Cut"));
    m_keepNoPictures = false;
    setSelection({}, {});
}

bool Canvas::canPaste()
{
    return !s_clipboard.isEmpty();
}

void Canvas::paste()
{
    if (m_tool == ContourEditor && !s_pointClipboard.isEmpty() && !m_playing) {
        // Points copied in the Contour Editor: added after the picked point of the curve, a little way along.
        Drawing const* const d = currentDrawing();
        if (!d || !contourValid() || m_ceFill >= 0)
            return;
        Drawing edited = *d;
        Stroke* const line = contourLine(edited);
        if (!line)
            return;
        QPointF const shift(8.0 / std::max(m_zoom, 1e-6), 8.0 / std::max(m_zoom, 1e-6));
        int at = m_cePoint + 1;
        QVector<int> added;
        for (StrokePoint p : std::as_const(s_pointClipboard)) {
            p.pos += shift;
            line->points.insert(at, p);
            added << at++;
        }
        commitDrawing(edited, tr("Paste Point(s)"));
        m_cePoint = added.last();
        m_ceExtra = added.mid(0, added.size() - 1);
        update();
        return;
    }
    if (s_clipboard.isEmpty() || m_playing)
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Drawing const* const d = currentDrawing();
    Drawing merged = d ? *d : Drawing();
    QVector<int> strokes, fills;
    // Where it goes (Preferences: Paste Behaviour): where it was, or in the middle of the view - always, or when where it was
    // is out of sight.
    Drawing placed = s_clipboard;
    if (m_pasteBehaviour != 0) {
        QRectF const box = placed.bounds();
        QTransform const toView = layerViewTransform();
        QRectF const seen = toView.mapRect(box);
        bool const outside = !rect().intersects(seen.toAlignedRect());
        if (!box.isEmpty() && toView.isInvertible() && (m_pasteBehaviour == 1 || outside)) {
            QPointF const middle = toView.inverted().map(QPointF(width() / 2.0, height() / 2.0));
            QPointF const by = middle - box.center();
            QTransform const move = QTransform::fromTranslate(by.x(), by.y());
            for (Fill& f : placed.fills)
                f.transform(move);
            for (Stroke& s : placed.strokes)
                s.transform(move);
            for (Picture& p : placed.pictures)
                p.transform(move);
        }
    }
    // Its joints its own: not joined to what happens to have the same numbers here.
    Drawing::freshJoints(placed.strokes, merged);
    for (Fill const& f : placed.fills) {
        fills.push_back(int(merged.fills.size()));
        merged.fills.push_back(f);
    }
    for (Stroke const& s : placed.strokes) {
        strokes.push_back(int(merged.strokes.size()));
        merged.strokes.push_back(s);
    }
    // Pictures go where pictures are: with the line art.
    QVector<int> pictures;
    if (picturesHere()) {
        for (Picture const& picture : placed.pictures) {
            pictures.push_back(int(merged.pictures.size()));
            merged.pictures.push_back(picture);
        }
    }
    commitDrawing(merged, tr("Paste"));
    if (selecting())
        setSelection(strokes, fills, pictures);
}

QVector<StrokePoint> Canvas::s_pointClipboard;
