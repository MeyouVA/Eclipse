// Eclipse - the Camera view: whole artwork - colour art from line art, groups, flattening, invisible lines, copying art between art layers.
#include "canvas/Canvas.h"
#include <QSet>
#include <QRandomGenerator>
#include <QLineF>
#include <algorithm>

#include <QInputDialog>
#include <QPainterPath>
#include <QSettings>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "actions/theme/AppSettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include "canvas/CanvasTidyShared.h"

using namespace canvas_tidy_shared;

void Canvas::onActionCreateColourArt()
{
    if (!canEditDrawing())
        return;
    Drawing const* const real = realDrawing();
    if (!real || (real->strokes.isEmpty() && real->fills.isEmpty())) {
        emit message(tr("There is no line art on this frame to make colour art from."));
        return;
    }
    QVector<Stroke> lines;
    for (Stroke const& stroke : real->strokes)
        lines.push_back(stroke);
    for (Fill const& fill : real->fills)
        for (Stroke const& contour : fill.contours)
            lines.push_back(contour);
    Drawing whole = *real;
    int made = 0;
    for (Stroke line : std::as_const(lines)) {
        line.width = 0.0f;
        line.group = 0;
        bool there = false;
        for (Stroke const& other : std::as_const(whole.colourStrokes)) {
            if (other.width > 0.0f || other.points.size() != line.points.size())
                continue;
            bool same = true;
            for (int i = 0; same && i < line.points.size(); ++i)
                same = QLineF(other.points[i].pos, line.points[i].pos).length() < 1e-6;
            there = there || same;
        }
        if (!there) {
            whole.colourStrokes.push_back(line);
            ++made;
        }
    }
    if (made == 0) {
        emit message(tr("The colour art has these lines already."));
        return;
    }
    clearSelection();
    m_committing = true;
    keepWholeDrawing(whole, tr("Create Colour Art from Line Art"));
    m_committing = false;
    emit message(tr("%n line(s) made in the colour art.", nullptr, made));
}

void Canvas::onActionGroupArtwork()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.size() + m_selFills.size() < 2) {
        emit message(tr("Select two or more things with the Select tool to group them."));
        return;
    }
    // A number no group of this sheet has yet.
    int group = 1;
    for (Stroke const& stroke : d->strokes)
        group = std::max(group, stroke.group + 1);
    for (Fill const& fill : d->fills)
        group = std::max(group, fill.group + 1);
    Drawing edited = *d;
    for (int i : std::as_const(m_selStrokes))
        edited.strokes[i].group = group;
    for (int i : std::as_const(m_selFills))
        edited.fills[i].group = group;
    commitDrawing(edited, tr("Group"));
}

void Canvas::onActionUngroupArtwork()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection())
        return;
    Drawing edited = *d;
    bool any = false;
    for (int i : std::as_const(m_selStrokes)) {
        any = any || edited.strokes[i].group != 0;
        edited.strokes[i].group = 0;
    }
    for (int i : std::as_const(m_selFills)) {
        any = any || edited.fills[i].group != 0;
        edited.fills[i].group = 0;
    }
    if (any)
        commitDrawing(edited, tr("Ungroup"));
    else
        emit message(tr("Nothing selected is in a group."));
}

void Canvas::onActionFlatten()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || (m_selFills.size() < 2 && m_selStrokes.size() < 2)) {
        if (!m_quietFlatten)
            emit message(tr("Select two or more painted areas of one colour, or lines that meet or cross, with the Select tool to flatten them."));
        return;
    }
    // Each colour among the selected areas: its areas become one, where the last of them was.
    Drawing edited = *d;
    QVector<int> gone, kept;
    for (int a = 0; a < m_selFills.size(); ++a) {
        int const first = m_selFills[a];
        if (gone.contains(first) || kept.contains(first))
            continue;
        QPainterPath joined;
        joined.setFillRule(Qt::OddEvenFill);
        QVector<int> same;
        bool shapes = false;    // whether any of them is a contour shape: the result is one then
        for (int b = a; b < m_selFills.size(); ++b) {
            Fill const& fill = d->fills[m_selFills[b]];
            if (fill.swatch != d->fills[first].swatch || fill.color != d->fills[first].color)
                continue;
            shapes = shapes || !fill.contours.isEmpty();
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);
            for (QPolygonF const& outline : fill.outlines)
                path.addPolygon(outline);
            path.closeSubpath();
            joined = same.isEmpty() ? path : joined.united(path);
            same.push_back(m_selFills[b]);
        }
        if (same.size() < 2)
            continue;
        Fill& into = edited.fills[same.last()];
        into.outlines.clear();
        into.contours.clear();
        for (QPolygonF const& outline : joined.simplified().toSubpathPolygons())
            if (outline.size() >= 3)
                into.outlines.push_back(outline);
        if (shapes) {
            // Contour shapes stay contour shapes: the joined edge gets points and handles.
            for (QPolygonF const& outline : std::as_const(into.outlines))
                into.contours.push_back(Fill::contourOf(outline, std::max(0.25 / m_zoom, 0.05)));
            into.rebuild();
        }
        kept.push_back(same.last());
        same.removeLast();
        gone += same;
    }
    QVector<int> goneLines;
    // Where the selected lines cross: a point on each, lying on one another.
    int const crossings = addCrossingPoints(edited.strokes, m_selStrokes);
    {
        double const near = std::max(1.0 / std::max(m_zoom, 1e-6), 0.5);
        auto alike = [](Stroke const& a, Stroke const& b) {
            return a.swatch == b.swatch && a.color == b.color && std::abs(a.width - b.width) < 1e-3f && a.curve == b.curve && a.join == b.join;
        };
        auto meet = [&](StrokePoint const& a, StrokePoint const& b) { return QLineF(a.pos, b.pos).length() <= near; };
        bool joinedAny = true;
        while (joinedAny) {
            joinedAny = false;
            for (int x = 0; x < m_selStrokes.size() && !joinedAny; ++x) {
                Stroke& a = edited.strokes[m_selStrokes[x]];
                if (a.closed || a.points.size() < 2 || goneLines.contains(m_selStrokes[x]))
                    continue;
                for (int y = 0; y < m_selStrokes.size() && !joinedAny; ++y) {
                    if (y == x || goneLines.contains(m_selStrokes[y]))
                        continue;
                    Stroke const& b = edited.strokes[m_selStrokes[y]];
                    if (b.closed || b.points.size() < 2 || !alike(a, b))
                        continue;
                    Stroke mine = a, theirs = b;
                    if (meet(mine.points.last(), theirs.points.first())) {
                        // as they are
                    } else if (meet(mine.points.last(), theirs.points.last())) {
                        theirs.reverse();
                    } else if (meet(mine.points.first(), theirs.points.first())) {
                        mine.reverse();
                    } else if (meet(mine.points.first(), theirs.points.last())) {
                        mine.reverse();
                        theirs.reverse();
                    } else {
                        continue;
                    }
                    // Where they meet is one point: it arrives as the first line did and leaves as the second does.
                    int const meeting = int(mine.points.size()) - 1;
                    mine.append(theirs, true);
                    mine.points[meeting].corner = true;
                    a = mine;
                    goneLines.push_back(m_selStrokes[y]);
                    joinedAny = true;
                }
            }
        }
        for (int index : std::as_const(m_selStrokes)) {
            Stroke& line = edited.strokes[index];
            if (goneLines.contains(index) || line.closed || line.points.size() < 4 || !meet(line.points.first(), line.points.last()))
                continue;
            if (goneLines.isEmpty())
                continue;       // only lines that were joined here are closed: one drawn that way is left as it is
            line.points.first().in = line.points.last().in;
            line.points.removeLast();
            line.closed = true;
        }
    }
    if (gone.isEmpty() && goneLines.isEmpty() && crossings == 0) {
        if (!m_quietFlatten)
            emit message(tr("There is nothing to flatten: the areas selected are of different colours, and no lines of one colour and thickness meet or cross."));
        return;
    }
    std::sort(gone.begin(), gone.end());
    for (int i = int(gone.size()) - 1; i >= 0; --i)
        edited.fills.removeAt(gone[i]);
    std::sort(goneLines.begin(), goneLines.end());
    for (int i = int(goneLines.size()) - 1; i >= 0; --i)
        edited.strokes.removeAt(goneLines[i]);
    clearSelection();
    commitDrawing(edited, tr("Flatten"));
}

int Canvas::addCrossingPoints(QVector<Stroke>& strokes, QVector<int> const& which)
{
    double const same = 0.05;       // (scene units) a point already there
    auto pointAt = [&](Stroke const& line, QPointF at) {
        for (int k = 0; k < line.points.size(); ++k)
            if (QLineF(line.points[k].pos, at).length() < same)
                return k;
        return -1;
    };
    int added = 0;
    int next = 1;
    for (Stroke const& line : std::as_const(strokes))
        for (StrokePoint const& point : line.points)
            next = std::max(next, point.joint + 1);
    // One point of both lines: a joint (two joints that meet become one).
    auto joint = [&](StrokePoint& a, StrokePoint& b) {
        if (a.joint != 0 && b.joint != 0 && a.joint != b.joint) {
            int const from = b.joint;
            for (Stroke& line : strokes)
                for (StrokePoint& point : line.points)
                    if (point.joint == from)
                        point.joint = a.joint;
            return;
        }
        int const j = a.joint != 0 ? a.joint : b.joint != 0 ? b.joint : next++;
        a.joint = b.joint = j;
    };
    // One crossing at a time: each new point changes the lines, so they are looked over again (a few hundred at most).
    for (int round = 0; round < 400; ++round) {
        bool found = false;
        for (int x = 0; x < which.size() && !found; ++x) {
            for (int y = x + 1; y < which.size() && !found; ++y) {
                if (which[x] < 0 || which[y] < 0 || which[x] >= strokes.size() || which[y] >= strokes.size())
                    continue;
                Stroke& a = strokes[which[x]];
                Stroke& b = strokes[which[y]];
                if (a.points.size() < 2 || b.points.size() < 2)
                    continue;
                QVector<StrokePoint> const ra = a.path(), rb = b.path();
                int const pa = int(ra.size()) - 1 + (a.closed && ra.size() > 2 ? 1 : 0), pb = int(rb.size()) - 1 + (b.closed && rb.size() > 2 ? 1 : 0);
                for (int i = 0; i < pa && !found; ++i) {
                    QPointF const p0 = ra[i].pos, p1 = ra[(i + 1) % ra.size()].pos;
                    for (int k = 0; k < pb && !found; ++k) {
                        double const t = crossing(p0, p1, rb[k].pos, rb[(k + 1) % rb.size()].pos);
                        if (t < 0.0)
                            continue;
                        QPointF const at = p0 + (p1 - p0) * t;
                        int ia = pointAt(a, at), ib = pointAt(b, at);
                        if (ia >= 0 && ib >= 0 && a.points[ia].pos == b.points[ib].pos) {
                            if (a.points[ia].joint != 0 && a.points[ia].joint == b.points[ib].joint)
                                continue;       // one point of both already
                            joint(a.points[ia], b.points[ib]);
                            ++added;
                            found = true;
                            continue;
                        }
                        if (ia < 0)
                            ia = a.insertPoint(at);
                        if (ib < 0)
                            ib = b.insertPoint(at);
                        if (ia < 0 || ib < 0)
                            continue;
                        // Exactly one place: where the one that was there already is, or between the two new ones.
                        QPointF const place = pointAt(a, at) >= 0 && pointAt(b, at) < 0 ? a.points[ia].pos
                                              : (a.points[ia].pos + b.points[ib].pos) / 2.0;
                        a.points[ia].pos = place;
                        b.points[ib].pos = place;
                        joint(a.points[ia], b.points[ib]);
                        ++added;
                        found = true;
                    }
                }
            }
        }
        if (!found)
            break;
    }
    return added;
}

void Canvas::onActionFlattenDrawing()
{
    if (m_tool != Select)
        setTool(Select);
    selectAll();
    onActionFlatten();
}

void Canvas::onActionFlattenAllDrawings()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    if (m_tool != Select)
        setTool(Select);
    int const was = m_frame;
    QVector<int> const cells = scene.layers[m_layer].cells;
    m_document->beginMacro(tr("Flatten On All Drawings"));
    m_quietFlatten = true;
    QSet<int> done;
    for (int frame = 0; frame < cells.size(); ++frame) {
        if (cells[frame] < 0 || done.contains(cells[frame]))
            continue;
        done.insert(cells[frame]);
        m_frame = frame;        // (each drawing at the first frame that shows it)
        selectAll();
        onActionFlatten();
    }
    m_quietFlatten = false;
    m_frame = was;
    m_document->endMacro();
    clearSelection();
    update();
}

void Canvas::onActionFlattenRemoveExtra()
{
    m_document->beginMacro(tr("Flatten and Remove Extra Strokes"));
    onActionFlatten();
    onActionRemoveExtraStrokes();
    m_document->endMacro();
}

void Canvas::onActionRemoveInvisibleLines()
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    bool const chosen = selecting() && !m_selStrokes.isEmpty();
    Drawing edited = *d;
    edited.strokes.clear();
    for (int i = 0; i < d->strokes.size(); ++i)
        if (d->strokes[i].width > 0.0f || (chosen && !m_selStrokes.contains(i)))
            edited.strokes.push_back(d->strokes[i]);
    if (edited.strokes.size() == d->strokes.size()) {
        emit message(tr("There are no invisible lines here."));
        return;
    }
    clearSelection();
    commitDrawing(edited, tr("Remove Invisible Lines"));
}

void Canvas::onActionCopyArt()
{
    if (!canEditDrawing())
        return;
    // From one sheet of the drawing into another: the lines (with no thickness, into the colour art) and the areas.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_copyArt.from == m_copyArt.to) {
        emit message(tr("Unable to set art layer to itself."));
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    QVector<int> frames;
    if (m_copyArt.allDrawings) {
        QSet<int> done;
        for (int frame = 0; frame < layer.cells.size(); ++frame)
            if (layer.cells[frame] >= 0 && !done.contains(layer.cells[frame])) {
                done.insert(layer.cells[frame]);
                frames.push_back(frame);
            }
    } else {
        frames = {m_frame};
    }
    int made = 0;
    m_document->beginMacro(tr("Copy Art"));
    for (int frame : std::as_const(frames)) {
        int const index = frame < layer.cells.size() ? layer.cells[frame] : -1;
        if (index < 0 || index >= layer.drawings.size())
            continue;
        Drawing whole = layer.drawings[index];
        QVector<Stroke> lines = whole.strokesOf(m_copyArt.from);
        QVector<Fill> areas = whole.fillsOf(m_copyArt.from);
        if (lines.isEmpty() && areas.isEmpty())
            continue;
        if (m_copyArt.clearFirst) {
            whole.strokesOf(m_copyArt.to).clear();
            whole.fillsOf(m_copyArt.to).clear();
        }
        bool const toColour = m_copyArt.to == Drawing::ColourSheet;
        for (Stroke line : std::as_const(lines)) {
            if (toColour)
                line.width = 0.0f;
            line.group = 0;
            whole.strokesOf(m_copyArt.to).push_back(line);
            ++made;
        }
        for (Fill const& area : std::as_const(areas)) {
            if (toColour && !area.contours.isEmpty()) {
                // A brush shape's edge bounds the paint in the colour art: its contours as lines with no thickness.
                for (Stroke contour : area.contours) {
                    contour.width = 0.0f;
                    whole.strokesOf(m_copyArt.to).push_back(contour);
                    ++made;
                }
                continue;
            }
            whole.fillsOf(m_copyArt.to).push_back(area);
            ++made;
        }
        m_committing = true;
        m_document->setDrawing(m_layer, frame, whole, tr("Copy Art"));
        m_committing = false;
    }
    m_document->endMacro();
    clearSelection();
    update();
    emit message(tr("%n thing(s) copied.", nullptr, made));
}

void Canvas::onActionCopyArtSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Copy Art Settings"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QStringList const names = {tr("Underlay Art"), tr("Colour Art"), tr("Line Art"), tr("Overlay Art")};
    QComboBox* const from = new QComboBox(&dialog);
    QComboBox* const to = new QComboBox(&dialog);
    from->addItems(names);
    to->addItems(names);
    from->setCurrentIndex(m_copyArt.from);
    to->setCurrentIndex(m_copyArt.to);
    QCheckBox* const clear = new QCheckBox(tr("Clear Destination Art First"), &dialog);
    clear->setChecked(m_copyArt.clearFirst);
    QCheckBox* const all = new QCheckBox(tr("Apply to All Drawings"), &dialog);
    all->setChecked(m_copyArt.allDrawings);
    form->addRow(tr("Source Art"), from);
    form->addRow(tr("Destination Art"), to);
    form->addRow(clear);
    form->addRow(all);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_copyArt = {from->currentIndex(), to->currentIndex(), clear->isChecked(), all->isChecked()};
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    s.setValue(QStringLiteral("tools/copyArt"), QStringLiteral("%1,%2,%3,%4").arg(m_copyArt.from).arg(m_copyArt.to).arg(int(m_copyArt.clearFirst)).arg(int(m_copyArt.allDrawings)));
}

void Canvas::onActionOptimizeDrawings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Optimize Drawings"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const flatten = new QCheckBox(tr("Flatten the lines of every drawing"), &dialog);
    flatten->setObjectName(QStringLiteral("OptimizeFlatten"));
    flatten->setChecked(true);
    QCheckBox* const invisible = new QCheckBox(tr("Remove invisible lines"), &dialog);
    invisible->setObjectName(QStringLiteral("OptimizeInvisible"));
    invisible->setToolTip(tr("Lines with no thickness (the edges of paint) are taken out; the paint stays."));
    QCheckBox* const crop = new QCheckBox(tr("Crop the pictures of drawings to what shows"), &dialog);
    crop->setObjectName(QStringLiteral("OptimizeCrop"));
    crop->setChecked(true);
    form->addRow(new QLabel(tr("Every drawing of every drawing layer of the scene is optimized."), &dialog));
    form->addRow(flatten);
    form->addRow(invisible);
    form->addRow(crop);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].holdsDrawings() && !scene.layers[l].showsSymbol() && !scene.layers[l].locked)
            layers.push_back(l);
    if (layers.isEmpty())
        return;
    m_document->beginMacro(tr("Optimize Drawings"));
    if (flatten->isChecked()) {
        int const wasLayer = m_layer;
        for (int l : std::as_const(layers))
            if (!m_document->scene().layers[l].bitmap) {
                m_layer = l;
                onActionFlattenAllDrawings();
            }
        m_layer = wasLayer;
    }
    bool const dropInvisible = invisible->isChecked(), cropping = crop->isChecked();
    if (dropInvisible || cropping)
        m_document->editLayers(layers, tr("Optimize Drawings"), [dropInvisible, cropping](Layer& layer) {
            for (Drawing& d : layer.drawings) {
                if (dropInvisible)
                    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                        QVector<Stroke>& lines = d.strokesOf(sheet);
                        lines.erase(std::remove_if(lines.begin(), lines.end(), [](Stroke const& s) { return s.width <= 0.0f; }), lines.end());
                    }
                if (cropping)
                    cropPictures(d);
            }
        });
    m_document->endMacro();
    clearSelection();
    update();
    emit message(tr("The drawings of %n layer(s) were optimized.", nullptr, int(layers.size())));
}

int Canvas::cropPictures(Drawing& drawing)
{
    int cropped = 0;
    for (Picture& picture : drawing.pictures) {
        if (picture.image.isNull() || picture.linked || !picture.image.hasAlphaChannel())
            continue;
        QImage const image = picture.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(row[x]) != 0) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        }
        if (right < 0 || (left == 0 && top == 0 && right == image.width() - 1 && bottom == image.height() - 1))
            continue;
        QRect const kept(QPoint(left, top), QPoint(right, bottom));
        QSizeF const shown = picture.size.isEmpty() ? QSizeF(image.size()) : QSizeF(picture.size);
        double const sx = shown.width() / image.width(), sy = shown.height() / image.height();
        QPointF const offset(left * sx, top * sy);
        picture.image = picture.image.copy(kept);
        picture.size = QSize(std::max(1, int(std::lround(kept.width() * sx))), std::max(1, int(std::lround(kept.height() * sy))));
        picture.placement = QTransform::fromTranslate(offset.x(), offset.y()) * picture.placement;
        for (QPolygonF& ring : picture.clip)
            ring.translate(-offset);
        ++cropped;
    }
    return cropped;
}

int Canvas::reducePictures(Drawing& drawing, int percent)
{
    int reduced = 0;
    for (Picture& picture : drawing.pictures) {
        if (picture.image.isNull() || picture.linked)
            continue;
        QSize const fewer(std::max(1, picture.image.width() * percent / 100), std::max(1, picture.image.height() * percent / 100));
        if (fewer == picture.image.size())
            continue;
        if (picture.size.isEmpty())
            picture.size = picture.image.size();
        picture.image = picture.image.scaled(fewer, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        ++reduced;
    }
    return reduced;
}

void Canvas::onActionCropBrushTextures()
{
    Drawing const* const d = currentDrawing();
    if (!d || !canEditDrawing())
        return;
    Drawing edited = *d;
    if (cropPictures(edited) == 0) {
        emit message(tr("There is nothing to crop in this drawing."));
        return;
    }
    commitDrawing(edited, tr("Crop Brush Textures"));
}

void Canvas::onActionReduceTextureResolution()
{
    Drawing const* const d = currentDrawing();
    if (!d || !canEditDrawing())
        return;
    if (d->pictures.isEmpty()) {
        emit message(tr("This drawing has no textures to reduce."));
        return;
    }
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Reduce Drawing Texture Resolution"), tr("Keep this much of the pixels (%):"), 50, 5, 99, 5, &ok);
    if (!ok)
        return;
    Drawing edited = *d;
    if (reducePictures(edited, percent) == 0)
        return;
    commitDrawing(edited, tr("Reduce Drawing Texture Resolution"));
}
