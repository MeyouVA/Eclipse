// Eclipse - the Camera view: what is selected turned over, turned, put in front or behind, made brush or pencil.
#include "ui/canvas/Canvas.h"

#include <QPainterPath>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"

int Canvas::pointThickness() const
{
    if (m_tool != ContourEditor || !contourValid() || m_ceFill >= 0)
        return -1;      // a brush shape has no thickness at a point: its contour is its edge
    return int(currentDrawing()->strokes[m_ceStroke].pressureAt(m_cePoint) * 100.0f + 0.5f);
}

void Canvas::setPointThickness(int percent)
{
    if (pointThickness() < 0 || pointThickness() == percent)
        return;
    Drawing edited = *currentDrawing();
    Stroke& line = edited.strokes[m_ceStroke];
    float const pressure = float(std::clamp(percent, 0, 400)) / 100.0f;
    if (line.thickness.isEmpty()) {
        line.points[m_cePoint].pressure = pressure;
    } else {
        // A thickness point at that point of the line, as thick as asked (to each side as it leaned there).
        ThicknessPoint& key = line.thickness[line.thicknessPointAt(m_cePoint)];
        float const was = (key.left + key.right) / 2.0f;
        if (was > 1e-6f) {
            key.left *= pressure / was;
            key.right *= pressure / was;
        } else {
            key.left = key.right = pressure;
        }
    }
    commitDrawing(edited, tr("Change Thickness"));
    update();
}

void Canvas::transformSelection(QTransform const& about, QString const& text)
{
    if (m_tool == Reposition)
        selectAll();        // that tool has all of the drawing in hand, always
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection())
        return;
    // `about` works round the origin: here, round the middle of what is selected.
    QPointF const c = selectionBounds().center();
    QTransform const t = QTransform::fromTranslate(-c.x(), -c.y()) * about * QTransform::fromTranslate(c.x(), c.y());
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes))
        edited.strokes[i].transform(t);
    for (int i : std::as_const(m_selFills))
        edited.fills[i].transform(t);
    if (picturesHere())
        for (int i : std::as_const(m_selPictures))
            if (i >= 0 && i < edited.pictures.size())
                edited.pictures[i].transform(t);
    bool const all = m_tool == Reposition;
    if (all)
        m_document->beginMacro(text);
    commitDrawing(edited, text);
    if (all) {
        repositionOthers(t, text);
        m_document->endMacro();
    }
    update();
}

void Canvas::setSelectionRect(QRectF const& box)
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection() || box.width() <= 0.0 || box.height() <= 0.0)
        return;
    QRectF const now = selectionBounds();
    // A selection with no width (an upright line) cannot be made wider: it keeps what it has.
    double const sx = now.width() > 1e-9 ? box.width() / now.width() : 1.0;
    double const sy = now.height() > 1e-9 ? box.height() / now.height() : 1.0;
    QPointF const from = now.center(), to = box.center();
    if (std::abs(sx - 1.0) < 1e-9 && std::abs(sy - 1.0) < 1e-9 && QLineF(from, to).length() < 1e-9)
        return;
    QTransform const t = QTransform::fromTranslate(-from.x(), -from.y()) * QTransform::fromScale(sx, sy) * QTransform::fromTranslate(to.x(), to.y());
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes))
        edited.strokes[i].transform(t);
    for (int i : std::as_const(m_selFills))
        edited.fills[i].transform(t);
    commitDrawing(edited, tr("Transform Selection"));
    update();
}

void Canvas::onActionFlipHorizontal()
{
    transformSelection(QTransform::fromScale(-1, 1), tr("Flip Horizontal"));
}

void Canvas::onActionFlipVertical()
{
    transformSelection(QTransform::fromScale(1, -1), tr("Flip Vertical"));
}

void Canvas::rotateSelection(double degrees)
{
    if (std::abs(degrees) > 1e-9)
        transformSelection(QTransform().rotate(degrees), tr("Rotate"));
}

void Canvas::onActionRotate90CW()
{
    transformSelection(QTransform().rotate(90), tr("Rotate 90 CW"));
}

void Canvas::onActionRotate90CCW()
{
    transformSelection(QTransform().rotate(-90), tr("Rotate 90 CCW"));
}

void Canvas::arrangeSelection(bool front)
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection())
        return;
    auto arrange = [front](auto& items, QVector<int> const& selected) {
        std::remove_reference_t<decltype(items)> chosen, rest;
        for (int i = 0; i < items.size(); ++i)
            (selected.contains(i) ? chosen : rest).push_back(items[i]);
        QVector<int> now;
        int const first = front ? int(rest.size()) : 0;
        for (int i = 0; i < chosen.size(); ++i)
            now.push_back(first + i);
        items = front ? rest + chosen : chosen + rest;
        return now;
    };
    Drawing edited = *d;
    // In the order of the sheet too: over (or under) all the rest, lines and areas alike.
    bool reordered = false;
    {
        QVector<Fill> restFills;
        QVector<Stroke> restStrokes;
        for (int i = 0; i < edited.fills.size(); ++i)
            if (!m_selFills.contains(i))
                restFills.push_back(edited.fills[i]);
        for (int i = 0; i < edited.strokes.size(); ++i)
            if (!m_selStrokes.contains(i))
                restStrokes.push_back(edited.strokes[i]);
        if (!restFills.isEmpty() || !restStrokes.isEmpty()) {
            int const place = front ? Drawing::topOrder(restFills, restStrokes) + 1 : Drawing::bottomOrder(restFills, restStrokes) - 1;
            for (int i : std::as_const(m_selFills))
                if (i >= 0 && i < edited.fills.size() && edited.fills[i].order != place) {
                    edited.fills[i].order = place;
                    reordered = true;
                }
            for (int i : std::as_const(m_selStrokes))
                if (i >= 0 && i < edited.strokes.size() && edited.strokes[i].order != place) {
                    edited.strokes[i].order = place;
                    reordered = true;
                }
        }
    }
    QVector<int> const strokes = arrange(edited.strokes, m_selStrokes);
    QVector<int> const fills = arrange(edited.fills, m_selFills);
    if (strokes == m_selStrokes && fills == m_selFills && !reordered)
        return;  // there already
    commitDrawing(edited, front ? tr("Bring to Front") : tr("Send to Back"));
    setSelection(strokes, fills);
}

void Canvas::stepSelection(int direction)
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection())
        return;
    auto swapAt = [](auto& items, int a, int b) {
        items.swapItemsAt(a, b);
        std::swap(items[a].order, items[b].order);
    };
    auto step = [direction, &swapAt](auto& items, QVector<int> selected) {
        int const n = int(items.size());
        if (direction > 0) {
            for (int i = n - 2; i >= 0; --i)
                if (selected.contains(i) && !selected.contains(i + 1)) {
                    swapAt(items, i, i + 1);
                    selected[selected.indexOf(i)] = i + 1;
                }
        } else {
            for (int i = 1; i < n; ++i)
                if (selected.contains(i) && !selected.contains(i - 1)) {
                    swapAt(items, i, i - 1);
                    selected[selected.indexOf(i)] = i - 1;
                }
        }
        return selected;
    };
    Drawing edited = *d;
    QVector<int> strokes = step(edited.strokes, m_selStrokes);
    QVector<int> fills = step(edited.fills, m_selFills);
    std::sort(strokes.begin(), strokes.end());
    std::sort(fills.begin(), fills.end());
    if (strokes == m_selStrokes && fills == m_selFills)
        return;  // as far as it goes already
    commitDrawing(edited, direction > 0 ? tr("Bring Forward") : tr("Send Backward"));
    setSelection(strokes, fills);
}

void Canvas::onActionBringForward()
{
    stepSelection(+1);
}

void Canvas::onActionSendBackward()
{
    stepSelection(-1);
}

void Canvas::onActionRotate180()
{
    transformSelection(QTransform().rotate(180), tr("Rotate 180"));
}

void Canvas::onActionShowStrokes(bool on)
{
    m_showStrokes = on;
    update();
}

void Canvas::onActionShowStrokesWash(bool on)
{
    m_strokesWash = on;
    update();
}

void Canvas::onActionConvertBrushToPencil()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select the lines to convert with the Select tool."));
        return;
    }
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selStrokes)) {
        changed = changed || !edited.strokes[i].thickness.isEmpty();
        edited.strokes[i].thickness.clear();
        for (StrokePoint& p : edited.strokes[i].points) {
            changed = changed || p.pressure != 1.0f;
            p.pressure = 1.0f;
        }
    }
    if (changed)
        commitDrawing(edited, tr("Convert to Pencil Lines"));
    update();
}

void Canvas::onActionConvertPencilToBrush()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select the lines to convert with the Select tool."));
        return;
    }
    Drawing edited = *d;
    QVector<int> gone;
    for (int i : std::as_const(m_selStrokes)) {
        Stroke const& stroke = d->strokes[i];
        if (stroke.points.size() < 2)
            continue;
        if (!stroke.uniform()) {
            // A line whose thickness changes: the shape it covers, as the brush would have made it.
            Fill const varying = Fill::fromStroke(stroke, 0.1);
            if (varying.outlines.isEmpty())
                continue;
            edited.fills.push_back(varying);
            gone.push_back(i);
            continue;
        }
        QPainterPath centre;
        QVector<StrokePoint> const line = stroke.path();
        centre.moveTo(line.first().pos);
        for (int k = 1; k < line.size(); ++k)
            centre.lineTo(line[k].pos);
        if (stroke.closed)
            centre.closeSubpath();
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(0.1, double(stroke.width) * stroke.pressureAt(0.0)));
        stroker.setJoinStyle(stroke.join == Stroke::MitreJoin ? Qt::MiterJoin : stroke.join == Stroke::BevelJoin ? Qt::BevelJoin : Qt::RoundJoin);
        stroker.setMiterLimit(Stroke::kMitreLimit / 2.0);
        stroker.setCapStyle(stroke.capStart == Stroke::FlatCap && stroke.capEnd == Stroke::FlatCap ? Qt::FlatCap : Qt::RoundCap);
        Fill shape;
        for (QPolygonF const& outline : stroker.createStroke(centre).simplified().toSubpathPolygons())
            if (outline.size() >= 3)
                shape.contours.push_back(Fill::contourOf(outline, 0.1));
        shape.rebuild();
        if (shape.outlines.isEmpty())
            continue;
        shape.swatch = stroke.swatch;
        shape.color = stroke.color;
        edited.fills.push_back(shape);
        gone.push_back(i);
    }
    if (gone.isEmpty()) {
        emit message(tr("Select the lines to convert with the Select tool."));
        return;
    }
    std::sort(gone.begin(), gone.end());
    for (int i = int(gone.size()) - 1; i >= 0; --i)
        edited.strokes.removeAt(gone[i]);
    clearSelection();
    commitDrawing(edited, tr("Convert to Brush Strokes"));
}

void Canvas::onActionBreakApartText()
{
    Drawing const* const d = currentDrawing();
    bool words = false;
    if (selecting() && d)
        for (int i : std::as_const(m_selFills))
            words = words || (i >= 0 && i < d->fills.size() && !d->fills[i].text.isEmpty());
    if (!words) {
        emit message(tr("Select words written with the Text tool to break them apart."));
        return;
    }
    Drawing edited = *d;
    edited.fills.clear();
    QVector<int> picked;
    for (int i = 0; i < d->fills.size(); ++i) {
        Fill const& fill = d->fills[i];
        if (!m_selFills.contains(i) || fill.text.isEmpty()) {
            if (m_selFills.contains(i))
                picked.push_back(int(edited.fills.size()));
            edited.fills.push_back(fill);
            continue;
        }
        // How many outlines each letter has, in the order they were made: an i has two.
        QVector<int> counts;
        QVector<QChar> letters;
        QVector<QRectF> boxes;      // each letter's outline as set at the origin
        QFont font;
        if (font.fromString(fill.textFont)) {
            font.setLetterSpacing(QFont::PercentageSpacing, fill.textKerning);
            for (QChar const letter : fill.text) {
                QPainterPath path;
                path.addText(QPointF(), font, QString(letter));
                int count = 0;
                for (QPolygonF const& outline : path.toSubpathPolygons())
                    count += outline.size() >= 3 ? 1 : 0;
                if (count > 0) {
                    counts.push_back(count);
                    letters.push_back(letter);
                    boxes.push_back(path.boundingRect());
                }
            }
        }
        QVector<Fill> const pieces = fill.pieces(counts);
        bool const stillText = letters.size() > 1 && pieces.size() == letters.size();
        for (int k = 0; k < pieces.size(); ++k) {
            Fill letter = pieces[k];
            if (stillText) {
                QRectF const stands = QPolygonF(letter.outlines.first()).boundingRect();
                letter.text = QString(letters[k]);
                letter.textFont = fill.textFont;
                letter.textAt = stands.topLeft() - fill.textShape.mapRect(boxes[k]).topLeft();
                letter.textShape = fill.textShape;
                letter.textAlign = 0;
                letter.textSpacing = fill.textSpacing;
                letter.textKerning = fill.textKerning;
            } else {
                // A single letter broken apart is a shape of its own: a contour shape, its points there to be edited.
                letter.giveContours(0.5);
            }
            picked.push_back(int(edited.fills.size()));
            edited.fills.push_back(letter);
        }
    }
    QVector<int> const lines = m_selStrokes;
    clearSelection();
    commitDrawing(edited, tr("Break Apart Text"));
    m_selStrokes = lines;
    m_selFills = picked;
    emit selectionChanged();
    update();
}

void Canvas::onActionBringToFront()
{
    arrangeSelection(true);
}

void Canvas::onActionSendToBack()
{
    arrangeSelection(false);
}
