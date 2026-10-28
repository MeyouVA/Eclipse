// Eclipse - the Camera view: cut, copy, paste and delete.
#include "canvas/Canvas.h"

#include <QHash>
#include <QMessageBox>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "render/effects/Effects.h"

// What Copy put aside: shared by every canvas, lost when the program closes.
static Drawing s_clipboard;
// The colours it was drawn in, as the scene it was copied from had them: to find or make them again where it is pasted.
static QVector<Swatch> s_clipboardColours;

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
        if (!canEditDrawing())
            return;
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
    if (!canEditDrawing() || !d || !hasSelection())
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
    bool const everywhere = (m_applyAll || m_applyOnion) && m_tool == Select;
    QSet<int> const onion = m_applyAll ? QSet<int>() : onionDrawings();
    if (everywhere)
        m_document->beginMacro(tr("Delete"));
    m_keepNoPictures = picturesGo;
    commitDrawing(kept, tr("Delete"));
    m_keepNoPictures = false;
    if (everywhere) {
        alsoInOtherDrawings(region, nullptr, tr("Delete"), m_applyAll ? nullptr : &onion);
        m_document->endMacro();
    }
    setSelection({}, {});
}

void Canvas::onActionDeleteNonSelection()
{
    if (!canEditDrawing())
        return;
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
    setArtworkCopiedLast(true);
    for (int i : m_selStrokes)
        s_clipboard.strokes.push_back(d->strokes[i]);
    for (int i : m_selFills)
        s_clipboard.fills.push_back(d->fills[i]);
    if (picturesHere())
        for (int i : m_selPictures)
            if (i >= 0 && i < d->pictures.size())
                s_clipboard.pictures.push_back(d->pictures[i]);
    s_clipboardColours.clear();
    Scene const& scene = m_document->scene();
    auto keep = [&](int id) {
        if (Swatch const* const s = scene.findSwatch(id); s && std::none_of(s_clipboardColours.begin(), s_clipboardColours.end(), [id](Swatch const& k) { return k.id == id; }))
            s_clipboardColours.push_back(*s);
    };
    for (Stroke const& stroke : std::as_const(s_clipboard.strokes))
        keep(stroke.swatch);
    for (Fill const& fill : std::as_const(s_clipboard.fills))
        keep(fill.swatch);
}

void Canvas::cutSelection()
{
    if (!canEditDrawing() || !hasSelection())
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
    pasteWithPlacement(false);
}

void Canvas::pasteInPlace()
{
    pasteWithPlacement(true);
}

void Canvas::pasteWithPlacement(bool inPlace)
{
    if (!canEditDrawing())
        return;
    if (m_tool == ContourEditor && !s_pointClipboard.isEmpty() && !m_playing) {
        // Points copied in the Contour Editor: added after the picked point of the curve, a little way along.
        Drawing const* const d = currentDrawing();
        if (!d || !contourValid() || m_ceFill >= 0)
            return;
        Drawing edited = *d;
        Stroke* const line = contourLine(edited);
        if (!line)
            return;
        QPointF const shift = inPlace ? QPointF() : QPointF(8.0 / std::max(m_zoom, 1e-6), 8.0 / std::max(m_zoom, 1e-6));
        int at = m_cePoint + 1;
        QVector<int> added;
        for (StrokePoint p : std::as_const(s_pointClipboard)) {
            p.editIdentity = 0;
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
    Drawing placed = s_clipboard;
    placed.renewPointIdentities();
    if (!inPlace && m_pasteBehaviour != 0) {
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
    // And a shape's line and its paint know each other by numbers of their own here (a pasted shape is not the one copied).
    {
        int next = 1;
        for (Stroke const& s : std::as_const(merged.strokes))
            next = std::max(next, s.link + 1);
        for (Fill const& f : std::as_const(merged.fills))
            next = std::max(next, f.link + 1);
        QHash<int, int> fresh;
        auto renumber = [&](int& link) {
            if (link == 0)
                return;
            if (!fresh.contains(link))
                fresh.insert(link, next++);
            link = fresh.value(link);
        };
        for (Stroke& s : placed.strokes)
            renumber(s.link);
        for (Fill& f : placed.fills)
            renumber(f.link);
    }
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
    // Colours this scene does not have (the artwork came from another): recovered, or left to the artwork's own values.
    QHash<int, int> const recovered = recoverPastedColours(placed);
    auto relink = [&](int& swatch, QColor& colour) {
        auto const found = recovered.constFind(swatch);
        if (found == recovered.constEnd())
            return;
        swatch = found.value();
        if (swatch == Scene::kNoSwatch && Scene::replacementColour().isValid())
            colour = Scene::replacementColour();
    };
    for (int i : std::as_const(fills))
        relink(merged.fills[i].swatch, merged.fills[i].color);
    for (int i : std::as_const(strokes))
        relink(merged.strokes[i].swatch, merged.strokes[i].color);
    commitDrawing(merged, tr("Paste"));
    if (!recovered.isEmpty())
        m_document->endMacro();
    if (selecting())
        setSelection(strokes, fills, pictures);
}

QHash<int, int> Canvas::recoverPastedColours(Drawing const& placed)
{
    Scene const& scene = m_document->scene();
    // A colour is missing when this scene has no colour of that id, or one of another name (the id is another scene's).
    QVector<Swatch> missing;
    QVector<int> unknown;
    auto look = [&](int id, QColor const& colour) {
        if (id == Scene::kNoSwatch || unknown.contains(id))
            return;
        Swatch const* const here = scene.findSwatch(id);
        auto theirs = std::find_if(s_clipboardColours.begin(), s_clipboardColours.end(), [id](Swatch const& k) { return k.id == id; });
        if (here && (theirs == s_clipboardColours.end() || theirs->name == here->name))
            return;
        Swatch lost = theirs != s_clipboardColours.end() ? *theirs : Swatch();
        if (theirs == s_clipboardColours.end()) {
            lost.id = id;
            lost.name = tr("Colour %1").arg(id);
            lost.color = colour;
        }
        unknown.push_back(id);
        missing.push_back(lost);
    };
    for (Stroke const& stroke : placed.strokes)
        look(stroke.swatch, stroke.color);
    for (Fill const& fill : placed.fills)
        look(fill.swatch, fill.color);
    QHash<int, int> map;
    if (missing.isEmpty())
        return map;
    bool recover = s_colourRecovery;
    if (recover && s_interactiveRecovery) {
        QStringList names;
        for (Swatch const& s : std::as_const(missing))
            names << s.name;
        recover = QMessageBox::question(this, tr("Colour Recovery"), tr("The following colours were not found.\nDo you want to perform colour recovery?") + QStringLiteral("\n\n") + names.join(QStringLiteral(", ")))
                  == QMessageBox::Yes;
    }
    m_document->beginMacro(tr("Paste"));
    if (recover) {
        map = m_document->bringColours(missing);
        emit message(tr("%n colour(s) recovered.", nullptr, int(missing.size())));
    } else {
        // Unlinked: what is pasted keeps its values (or takes the replacement colour of Preferences).
        for (Swatch const& s : std::as_const(missing))
            map.insert(s.id, Scene::kNoSwatch);
    }
    return map;
}

QVector<StrokePoint> Canvas::s_pointClipboard;

// Which was copied last, artwork or cells: what a paste in the Xsheet or the Timeline puts down.
static bool s_artworkLast = false;

bool Canvas::artworkCopiedLast()
{
    return s_artworkLast;
}

void Canvas::setArtworkCopiedLast(bool artwork)
{
    s_artworkLast = artwork;
}
