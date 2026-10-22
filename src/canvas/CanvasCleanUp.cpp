// Eclipse - the Camera view: protected colours, art by selection, other drawings moved alike, auto matte, ink, repaint, smoothing brush.
// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "canvas/Canvas.h"

#include <QApplication>

#include <cmath>

#include <QRandomGenerator>
#include <functional>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QSettings>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "actions/theme/AppSettings.h"
#include "canvas/CanvasCleanUpShared.h"

using namespace canvas_clean_up_shared;

bool Canvas::keepsColour(int swatch) const
{
    return m_respectProtected && m_document->scene().protects(swatch);
}

void Canvas::onActionRespectProtected(bool on)
{
    m_respectProtected = on;
}

void Canvas::onActionSelectionArt(QString const& how)
{
    Scene const& scene = m_document->scene();
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection() || m_layer < 0 || m_layer >= scene.layers.size()) {
        emit message(tr("Select artwork with the Select tool first."));
        return;
    }
    What const what = how.startsWith(QLatin1String("unpaint")) ? UnpaintArt : how.startsWith(QLatin1String("repaint")) ? RepaintArt : RemoveArt;
    bool const outside = how.contains(QLatin1String("Outside"));
    bool const everywhere = how.endsWith(QLatin1String("All"));
    QString const text = what == UnpaintArt ? (outside ? tr("Unpaint Outside Selection") : tr("Unpaint Selection"))
                         : what == RepaintArt ? (outside ? tr("Repaint Outside Selection") : tr("Repaint Selection"))
                         : (outside ? tr("Remove Art Outside Selection") : tr("Remove Art Inside Selection"));
    QColor const colour = scene.resolve(m_swatch, Qt::black);
    int const swatch = m_swatch;
    auto kept = [this](int id) { return keepsColour(id); };
    QVector<int> const lines = m_selStrokes, paints = m_selFills;
    // In the other drawings: whatever lies within the place the selection has here.
    QRectF const region = selectionBounds().adjusted(-1.5, -1.5, 1.5, 1.5);

    Drawing edited = *d;
    bool const here = changeArt(edited.strokes, edited.fills,
                                [&](bool line, int i) { return (line ? lines.contains(i) : paints.contains(i)) != outside; }, what, kept, swatch, colour);
    // Things may have gone: what is selected is known by its place in the drawing no longer.
    bool const fewer = edited.strokes.size() != d->strokes.size() || edited.fills.size() != d->fills.size();
    QVector<int> keptLines, keptPaints;
    if (fewer && outside && what == RemoveArt) {
        // All that is left is what was selected: it stays selected.
        for (int i = 0; i < edited.strokes.size(); ++i)
            keptLines.push_back(i);
        for (int i = 0; i < edited.fills.size(); ++i)
            keptPaints.push_back(i);
    } else if (fewer && outside) {
        keptLines = lines;
        for (int i : paints) {
            int below = 0;
            for (int k = 0; k < i; ++k)
                below += !paints.contains(k) && !kept(d->fills[k].swatch);
            keptPaints.push_back(i - below);
        }
    }
    if (everywhere)
        m_document->beginMacro(text);
    if (here) {
        if (fewer)
            clearSelection();
        commitDrawing(edited, text);
        if (fewer && !(keptLines.isEmpty() && keptPaints.isEmpty())) {
            m_selStrokes = keptLines;
            m_selFills = keptPaints;
            emit selectionChanged();
        }
    }
    if (everywhere) {
        // A copy: the scene changes under us with every drawing set.
        QVector<Drawing> const drawings = scene.layers[m_layer].drawings;
        QVector<int> const cells = scene.layers[m_layer].cells;
        int const sheet = sheetOf(m_art);
        QVector<bool> done(drawings.size(), false);
        if (m_frame >= 0 && m_frame < cells.size() && cells[m_frame] >= 0 && cells[m_frame] < done.size())
            done[cells[m_frame]] = true;    // the one in hand has been seen to
        m_committing = true;
        for (int f = 0; f < cells.size(); ++f) {
            int const index = cells[f];
            if (index < 0 || index >= drawings.size() || done[index])
                continue;
            done[index] = true;
            Drawing whole = drawings[index];
            QVector<Stroke>& strokes = whole.strokesOf(sheet);
            QVector<Fill>& fills = whole.fillsOf(sheet);
            auto within = [&](bool line, int i) { return region.contains(line ? strokes[i].bounds() : fills[i].bounds()) != outside; };
            if (changeArt(strokes, fills, within, what, kept, swatch, colour))
                m_document->setDrawing(m_layer, f, whole, text);
        }
        m_committing = false;
        m_document->endMacro();
    }
    update();
}

void Canvas::repositionOthers(QTransform const& done, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    // A copy: the scene changes under us with every drawing set.
    QVector<Drawing> const drawings = scene.layers[m_layer].drawings;
    QVector<int> const cells = scene.layers[m_layer].cells;
    int const current = m_frame >= 0 && m_frame < cells.size() ? cells[m_frame] : -1;
    int const mine = sheetOf(m_art);
    QVector<bool> seen(drawings.size(), false);
    m_committing = true;
    for (int f = 0; f < cells.size(); ++f) {
        int const index = cells[f];
        if (index < 0 || index >= drawings.size() || seen[index])
            continue;
        seen[index] = true;
        Drawing whole = drawings[index];
        bool changed = false;
        for (int sheet : {int(Drawing::UnderlaySheet), int(Drawing::ColourSheet), int(Drawing::LineSheet), int(Drawing::OverlaySheet)}) {
            if (index == current && sheet == mine)
                continue;       // that one has been seen to
            for (Stroke& stroke : whole.strokesOf(sheet)) {
                stroke.transform(done);
                changed = true;
            }
            for (Fill& fill : whole.fillsOf(sheet)) {
                fill.transform(done);
                changed = true;
            }
        }
        for (Picture& picture : whole.pictures) {
            picture.placement = picture.placement * done;
            changed = true;
        }
        if (changed)
            m_document->setDrawing(m_layer, f, whole, text);
    }
    m_committing = false;
}

void Canvas::onActionAutoMatte()
{
    Drawing const* const real = realDrawing();
    if (!real || (real->strokes.isEmpty() && real->fills.isEmpty())) {
        emit message(tr("There is no line art on this frame to make a matte of."));
        return;
    }
    // How far out it reaches, which art it is made from, and which art it is put in.
    QDialog ask(this);
    ask.setObjectName(QStringLiteral("AutoMatteDialog"));
    ask.setWindowTitle(tr("Generate Auto-Matte"));
    auto* const form = new QFormLayout(&ask);
    auto* const radius = new QDoubleSpinBox(&ask);
    radius->setObjectName(QStringLiteral("MatteRadius"));
    radius->setRange(0.0, 500.0);
    radius->setDecimals(1);
    radius->setValue(settings().value(QStringLiteral("tools/matteRadius"), 0.0).toDouble());
    radius->setToolTip(tr("How far out from the artwork the matte reaches (pixels)."));
    form->addRow(tr("Radius"), radius);
    auto arts = [&](char const* name, int chosen) {
        auto* const box = new QComboBox(&ask);
        box->setObjectName(QLatin1String(name));
        box->addItem(tr("Line Art"), int(Drawing::LineSheet));
        box->addItem(tr("Colour Art"), int(Drawing::ColourSheet));
        box->addItem(tr("Overlay"), int(Drawing::OverlaySheet));
        box->addItem(tr("Underlay"), int(Drawing::UnderlaySheet));
        box->setCurrentIndex(std::max(0, box->findData(chosen)));
        return box;
    };
    QComboBox* const source = arts("MatteSource", settings().value(QStringLiteral("tools/matteSource"), int(Drawing::LineSheet)).toInt());
    QComboBox* const destination = arts("MatteDestination", settings().value(QStringLiteral("tools/matteDestination"), int(Drawing::ColourSheet)).toInt());
    form->addRow(tr("Source"), source);
    form->addRow(tr("Destination"), destination);
    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    form->addRow(buttons);
    if (ask.exec() != QDialog::Accepted)
        return;
    settings().setValue(QStringLiteral("tools/matteRadius"), radius->value());
    settings().setValue(QStringLiteral("tools/matteSource"), source->currentData().toInt());
    settings().setValue(QStringLiteral("tools/matteDestination"), destination->currentData().toInt());
    if (!autoMatte(radius->value(), source->currentData().toInt(), destination->currentData().toInt()))
        emit message(tr("There is nothing in the %1 of this frame to make a matte of.").arg(source->currentText()));
}

bool Canvas::autoMatte(double radius, int source, int destination)
{
    Drawing const* const real = realDrawing();
    if (!real || source < 0 || source >= Drawing::SheetCount || destination < 0 || destination >= Drawing::SheetCount)
        return false;
    Fill matte;
    matte.outlines = Deform::outerEdges(*real, source, radius);
    if (matte.outlines.isEmpty())
        return false;
    matte.swatch = m_swatch;
    matte.color = m_document->scene().resolve(m_swatch, Qt::black);
    Drawing whole = *real;
    whole.fillsOf(destination).push_front(matte);
    clearSelection();
    m_committing = true;
    m_document->setDrawing(m_layer, m_frame, whole, tr("Generate Auto-Matte"));
    m_committing = false;
    emit message(destination == Drawing::ColourSheet ? tr("The matte is in the colour art, behind the lines.") : tr("The matte is at the back of the art it was asked for."));
    update();
    return true;
}

void Canvas::thicknessDown(QPointF p, Qt::KeyboardModifiers modifiers)
{
    Drawing const* const d = currentDrawing();
    m_thickStroke = m_thickKey = -1;
    if (d && (modifiers & Qt::ControlModifier)) {
        for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
            if (d->strokes[i].width <= 0.0f || d->strokes[i].points.size() < 2 || !d->strokes[i].hit(p, 4.0 / std::max(m_zoom, 1e-6)))
                continue;
            Drawing edited = *d;
            Stroke& line = edited.strokes[i];
            line.ownThickness();
            int const had = int(line.thickness.size());
            line.thicknessPointAt(line.placeAt(p), 1e-3);
            if (line.thickness.size() > had)
                commitDrawing(edited, tr("Add Thickness Point"));
            update();
            return;
        }
        emit message(tr("Hold Ctrl and press on a pencil line to put a thickness point there."));
        return;
    }
    thicknessPointDown(p, modifiers);
}

void Canvas::inkAt(QPointF p)
{
    QColor const colour = m_document->scene().resolve(m_swatch, Qt::black);
    // A few pixels either side of the line will do: the tool is for lines, and nothing else is in its way.
    double const reach = 4.0 / std::max(m_zoom, 1e-6);
    QVector<Stroke>& lines = m_eraseArt.strokes;
    // Selection mode (Ctrl the other way): the selected lines; else those under the pen.
    bool const selected = m_inkSelectionMode != bool(QApplication::keyboardModifiers() & Qt::ControlModifier);
    QVector<int> hits;
    for (int i = 0; i < lines.size(); ++i)
        if (selected ? m_selStrokes.contains(i) : lines[i].hit(p, reach))
            hits.push_back(i);
    // Be Smart on Connecting Lines: lines joined end to end with those are inked with them.
    for (int grown = 0; m_inkSmart && grown < hits.size(); ++grown) {
        Stroke const& from = lines[hits[grown]];
        if (from.points.isEmpty())
            continue;
        for (int j = 0; j < lines.size(); ++j) {
            if (hits.contains(j) || lines[j].points.isEmpty())
                continue;
            bool joined = false;
            for (StrokePoint const& a : {from.points.first(), from.points.last()})
                for (StrokePoint const& b : {lines[j].points.first(), lines[j].points.last()})
                    joined = joined || (a.joint != 0 && a.joint == b.joint) || QLineF(a.pos, b.pos).length() <= reach * 0.5;
            if (joined)
                hits.push_back(j);
        }
    }
    for (int i : std::as_const(hits)) {
        Stroke& stroke = lines[i];
        if (stroke.width <= 0.0f || keepsColour(stroke.swatch))
            continue;
        bool const recolour = stroke.swatch != m_swatch || stroke.color != colour;
        bool const rejoin = m_inkJoinChange && stroke.join != m_join;
        if (!recolour && !rejoin)
            continue;
        stroke.swatch = m_swatch;
        stroke.color = colour;
        if (m_inkJoinChange)
            stroke.join = m_join;
        m_inked = true;
        if (!m_inkedNow.contains(i))
            m_inkedNow.push_back(i);
    }
    update();
}

void Canvas::finishInk(Drawing& art, int order)
{
    // The inked lines over the other lines (1) or under them (2); where they end up, for the selection.
    QVector<int> placed = m_inkedNow;
    std::sort(placed.begin(), placed.end());
    if (order != 0 && !placed.isEmpty()) {
        QVector<Stroke> inked, rest;
        for (int i = 0; i < art.strokes.size(); ++i)
            (placed.contains(i) ? inked : rest).push_back(art.strokes[i]);
        art.strokes = order == 1 ? rest + inked : inked + rest;
        int const first = order == 1 ? int(rest.size()) : 0;
        for (int k = 0; k < placed.size(); ++k)
            placed[k] = first + k;
    }
    m_inkedNow = placed;
}

void Canvas::repaintTo(QPointF p, double tolerance)
{
    if (m_repaintPath.points.isEmpty() || QLineF(m_repaintPath.points.last().pos, p).length() > 1e-9)
        {
        StrokePoint point;
        point.pos = p;
        m_repaintPath.points.push_back(point);
    }
    QVector<QPolygonF> area = m_repaintPath.outlinePolygons();
    if (area.isEmpty()) {
        // One point: a round dab.
        double const r = m_repaintPath.width / 2.0;
        QPolygonF dab;
        for (int k = 0; k < 24; ++k)
            dab << p + QPointF(std::cos(k * 3.14159265358979323846 / 12.0) * r, std::sin(k * 3.14159265358979323846 / 12.0) * r);
        area = {dab};
    }
    QColor const colour = m_document->scene().resolve(m_swatch, Qt::black);
    Drawing art = m_repaintFrom;
    art.fills.clear();
    bool any = false;
    for (Fill const& fill : m_repaintFrom.fills) {
        if ((fill.swatch == m_swatch && fill.color == colour) || keepsColour(fill.swatch)) {
            art.fills.push_back(fill);
            continue;
        }
        QVector<Fill> inside = fill.within(area, tolerance);
        if (inside.isEmpty()) {
            art.fills.push_back(fill);
            continue;
        }
        any = true;
        art.fills += fill.minus(area, tolerance);
        for (Fill& piece : inside) {
            piece.swatch = m_swatch;
            piece.color = colour;
            piece.text.clear();
            piece.gradPlaced = false;
        }
        art.fills += inside;
    }
    m_eraseArt = art;
    m_inked = any;
    update();
}

void Canvas::smoothAt(QPointF p)
{
    // As far round the pen as the size in Tool Properties.
    double const reach = std::max(m_brushWidth / 2.0, 4.0 / std::max(m_zoom, 1e-6));
    auto settle = [&](Stroke& line) {
        int const n = int(line.points.size());
        if (n < 3)
            return false;
        bool any = false;
        QVector<StrokePoint> const was = line.points;
        for (int k = 0; k < n; ++k) {
            if (!line.closed && (k == 0 || k == n - 1))
                continue;       // the ends of an open line stay where they are
            if (QLineF(was[k].pos, p).length() > reach)
                continue;
            StrokePoint const& before = was[(k + n - 1) % n];
            StrokePoint const& after = was[(k + 1) % n];
            // A step towards the middle of its neighbours; a corner is a corner no longer.
            line.points[k].pos = was[k].pos * 0.6 + (before.pos + after.pos) * 0.2;
            if (line.thickness.isEmpty())
                line.points[k].pressure = was[k].pressure * 0.6f + (before.pressure + after.pressure) * 0.2f;
            line.points[k].in *= 0.8;
            line.points[k].out *= 0.8;
            line.points[k].corner = false;
            any = true;
        }
        return any;
    };
    // Apply to Single Stroke: the first line (or brush shape) the rub reaches, and that one only.
    auto reached = [&](Stroke const& line) {
        for (StrokePoint const& point : line.points)
            if (QLineF(point.pos, p).length() <= reach)
                return true;
        return false;
    };
    if (m_singleNow && m_onlyLine == -1) {
        for (int i = 0; i < m_eraseArt.strokes.size() && m_onlyLine == -1; ++i)
            if (reached(m_eraseArt.strokes[i]))
                m_onlyLine = i;
        for (int i = 0; i < m_eraseArt.fills.size() && m_onlyLine == -1; ++i)
            for (Stroke const& contour : m_eraseArt.fills[i].contours)
                if (m_onlyLine == -1 && reached(contour))
                    m_onlyLine = -2 - i;
    }
    for (int i = 0; i < m_eraseArt.strokes.size(); ++i)
        if (!m_singleNow || m_onlyLine == i)
            m_inked = settle(m_eraseArt.strokes[i]) || m_inked;
    for (int i = 0; i < m_eraseArt.fills.size(); ++i) {
        Fill& fill = m_eraseArt.fills[i];
        if (m_singleNow && m_onlyLine != -2 - i)
            continue;
        bool changed = false;
        for (Stroke& contour : fill.contours)
            changed = settle(contour) || changed;
        if (changed) {
            fill.rebuild();
            m_inked = true;
        }
    }
    update();
}
