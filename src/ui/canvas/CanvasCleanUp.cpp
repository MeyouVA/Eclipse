// Eclipse - the Camera view: Drawing > Paint and Clean Up on what is selected (or on all that is
// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "ui/canvas/Canvas.h"

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
#include "ui/window/MainWindowPrivate.h"

namespace {

enum What { UnpaintArt, RepaintArt, RemoveArt };

// Does `what` to the lines and paint that `chosen` says yes to; true when anything changed.
bool changeArt(QVector<Stroke>& strokes, QVector<Fill>& fills, std::function<bool(bool line, int index)> const& chosen, What what,
               std::function<bool(int swatch)> const& kept, int swatch, QColor const& colour)
{
    bool changed = false;
    for (int i = int(strokes.size()) - 1; i >= 0; --i) {
        if (!chosen(true, i))
            continue;
        Stroke& stroke = strokes[i];
        if (what == RemoveArt) {
            strokes.removeAt(i);
            changed = true;
        } else if (kept(stroke.swatch)) {
            continue;       // a protected colour is left as it is
        } else if (what == UnpaintArt && stroke.width > 0.0f) {
            stroke.width = 0.0f;        // there still, to bound paint, but not seen
            changed = true;
        } else if (what == RepaintArt && stroke.width > 0.0f && (stroke.swatch != swatch || stroke.color != colour)) {
            stroke.swatch = swatch;
            stroke.color = colour;
            changed = true;
        }
    }
    for (int i = int(fills.size()) - 1; i >= 0; --i) {
        if (!chosen(false, i))
            continue;
        Fill& fill = fills[i];
        if (what == RemoveArt) {
            fills.removeAt(i);
            changed = true;
        } else if (kept(fill.swatch)) {
            continue;
        } else if (what == UnpaintArt) {
            fills.removeAt(i);
            changed = true;
        } else if (fill.swatch != swatch || fill.color != colour) {
            fill.swatch = swatch;
            fill.color = colour;
            changed = true;
        }
    }
    return changed;
}

}  // namespace

bool Canvas::keepsColour(int swatch) const
{
    return m_respectProtected && m_document->scene().protects(swatch);
}

void Canvas::onActionRespectProtected(bool on)
{
    m_respectProtected = on;
}

// `how` is what to do - "unpaint", "repaint" or "remove" - then "Outside" when it is for all that
// is not selected, then "All" when it is for every drawing of the layer.
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
        // Only paint outside the selection went: the lines are where they were; the paint selected
        // has moved down by as many as went below it.
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

// ---- Reposition All Drawings ------------------------------------------------------------------

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

// ---- the Stamp tool --------------------------------------------------------------------------------

Drawing Canvas::stampNow() const
{
    if (m_stampOwn)
        return m_stamp;
    // To begin with: a star, in the colour in hand.
    Drawing star;
    Fill shape;
    QPolygonF outline;
    for (int i = 0; i < 10; ++i) {
        double const angle = -3.14159265358979323846 / 2.0 + i * 3.14159265358979323846 / 5.0;
        double const reach = i % 2 == 0 ? 40.0 : 16.0;
        outline << QPointF(std::cos(angle) * reach, std::sin(angle) * reach);
    }
    shape.outlines = {outline};
    shape.swatch = m_swatch;
    shape.color = m_document->scene().resolve(m_swatch, Qt::black);
    star.fills = {shape};
    return star;
}

void Canvas::loadStamp()
{
    // Kept as a scene of one layer with one drawing: the stamp.
    QByteArray const kept = settings().value(QStringLiteral("tools/stamp")).toByteArray();
    Scene scene;
    QString error;
    if (kept.isEmpty() || !Scene::fromJson(QJsonDocument::fromJson(kept).object(), scene, &error) || scene.layers.isEmpty()
        || scene.layers[0].drawings.isEmpty() || scene.layers[0].drawings[0].isEmpty())
        return;
    m_stampSet.clear();
    for (Drawing const& kept_ : std::as_const(scene.layers[0].drawings)) {
        if (kept_.isEmpty())
            continue;
        Drawing one;
        one.strokes = kept_.strokes;
        one.fills = kept_.fills;
        // (The colours of another scene's palette are not this one's: each keeps the colour it had.)
        for (Stroke& stroke : one.strokes)
            stroke.swatch = Scene::kNoSwatch;
        for (Fill& fill : one.fills)
            fill.swatch = Scene::kNoSwatch;
        m_stampSet.push_back(one);
    }
    m_stampTurn = 0;
    m_stamp = m_stampSet.first();
    m_stampOwn = true;
    {
        // One from before there was a list goes onto it.
        QSettings store = settings();
        QVariantList list = store.value(QStringLiteral("tools/stampList")).toList();
        bool listed = false;
        for (QVariant const& one : std::as_const(list))
            listed = listed || one.toByteArray() == kept;
        if (!listed) {
            QStringList names = store.value(QStringLiteral("tools/stampNames")).toStringList();
            list.push_back(kept);
            names.push_back(tr("Stamp %1").arg(list.size()));
            store.setValue(QStringLiteral("tools/stampList"), list);
            store.setValue(QStringLiteral("tools/stampNames"), names);
        }
    }
    update();
    emit stampsChanged();
}

void Canvas::clearStamp()
{
    m_stamp = Drawing();
    m_stampSet.clear();
    m_stampTurn = 0;
    m_stampOwn = false;
    settings().remove(QStringLiteral("tools/stamp"));
    update();
    emit stampsChanged();
}

QStringList Canvas::stampNames() const
{
    QSettings const store = settings();
    QStringList names = store.value(QStringLiteral("tools/stampNames")).toStringList();
    qsizetype const count = store.value(QStringLiteral("tools/stampList")).toList().size();
    while (names.size() < count)
        names.push_back(tr("Stamp %1").arg(names.size() + 1));
    names.resize(count);
    return names;
}

int Canvas::currentStamp() const
{
    if (!m_stampOwn)
        return -1;
    QSettings const store = settings();
    QByteArray const now = store.value(QStringLiteral("tools/stamp")).toByteArray();
    QVariantList const list = store.value(QStringLiteral("tools/stampList")).toList();
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toByteArray() == now)
            return i;
    return -1;
}

void Canvas::chooseStamp(int index)
{
    QVariantList const list = settings().value(QStringLiteral("tools/stampList")).toList();
    if (index < 0 || index >= list.size()) {
        clearStamp();       // the star
        return;
    }
    settings().setValue(QStringLiteral("tools/stamp"), list[index].toByteArray());
    loadStamp();
}

void Canvas::removeStamp(int index)
{
    QSettings store = settings();
    QVariantList list = store.value(QStringLiteral("tools/stampList")).toList();
    if (index < 0 || index >= list.size())
        return;
    QStringList names = stampNames();
    bool const inHand = index == currentStamp();
    list.removeAt(index);
    names.removeAt(index);
    store.setValue(QStringLiteral("tools/stampList"), list);
    store.setValue(QStringLiteral("tools/stampNames"), names);
    store.sync();
    if (inHand)
        clearStamp();
    else
        emit stampsChanged();
}

void Canvas::onActionCreateStamp()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to make a stamp of with the Select tool."));
        return;
    }
    // What is selected, kept about its own middle: that is where the pointer will be when it is stamped.
    QTransform const home = QTransform::fromTranslate(-selectionBounds().center().x(), -selectionBounds().center().y());
    Drawing stamp;
    for (int i : std::as_const(m_selStrokes)) {
        stamp.strokes.push_back(d->strokes[i]);
        stamp.strokes.last().transform(home);
    }
    for (int i : std::as_const(m_selFills)) {
        stamp.fills.push_back(d->fills[i]);
        stamp.fills.last().transform(home);
    }
    keepStamp({stamp});
    emit message(tr("The Stamp tool now stamps this artwork."));
}

void Canvas::createStampFrom(QVector<Drawing> const& drawings)
{
    QVector<Drawing> set;
    for (Drawing const& drawing : drawings) {
        Drawing one;
        one.strokes = drawing.strokes;
        one.fills = drawing.fills;
        if (one.isEmpty())
            continue;
        // Each about its own middle: that is where the pointer will be when it is stamped.
        QRectF box;
        for (Stroke const& stroke : std::as_const(one.strokes))
            box = box.isNull() ? stroke.bounds() : box.united(stroke.bounds());
        for (Fill const& fill : std::as_const(one.fills))
            box = box.isNull() ? fill.bounds() : box.united(fill.bounds());
        QTransform const home = QTransform::fromTranslate(-box.center().x(), -box.center().y());
        for (Stroke& stroke : one.strokes)
            stroke.transform(home);
        for (Fill& fill : one.fills)
            fill.transform(home);
        set.push_back(one);
    }
    if (set.isEmpty()) {
        emit message(tr("There is no artwork in the drawings selected to make a stamp of."));
        return;
    }
    keepStamp(set);
    emit message(set.size() == 1 ? tr("The Stamp tool now stamps this artwork.") : tr("The Stamp tool now stamps these %1 drawings, one after another.").arg(set.size()));
}

void Canvas::keepStamp(QVector<Drawing> const& drawings)
{
    m_stampSet = drawings;
    m_stampTurn = 0;
    m_stamp = drawings.first();
    m_stampOwn = true;
    {
        // Kept for the next time the program runs, with the colours it shows now.
        Scene keep = m_document->scene();
        Layer only;
        only.kind = Layer::DrawingLayer;
        only.name = QStringLiteral("Stamp");
        for (Drawing plain : drawings) {
            for (Stroke& stroke : plain.strokes)
                stroke.color = keep.resolve(stroke.swatch, stroke.color);
            for (Fill& fill : plain.fills)
                fill.color = keep.resolve(fill.swatch, fill.color);
            only.drawings.push_back(plain);
        }
        only.cells = QVector<int>(keep.frameCount(), 0);
        keep.layers = {only};
        QByteArray const bytes = QJsonDocument(keep.toJson()).toJson(QJsonDocument::Compact);
        QSettings store = settings();
        store.setValue(QStringLiteral("tools/stamp"), bytes);
        // And onto the list of stamps, under a name no other has.
        QVariantList list = store.value(QStringLiteral("tools/stampList")).toList();
        QStringList names = stampNames();
        bool listed = false;
        for (QVariant const& one : std::as_const(list))
            listed = listed || one.toByteArray() == bytes;
        if (!listed) {
            int number = int(list.size()) + 1;
            while (names.contains(tr("Stamp %1").arg(number)))
                ++number;
            list.push_back(bytes);
            names.push_back(tr("Stamp %1").arg(number));
            store.setValue(QStringLiteral("tools/stampList"), list);
            store.setValue(QStringLiteral("tools/stampNames"), names);
        }
    }
    emit stampsChanged();
    update();
}

void Canvas::stampAt(QPointF p)
{
    Drawing const stamp = stampNow();
    double const k = m_stampScale / 100.0 * (1.0 - QRandomGenerator::global()->bounded(1.0) * m_stampRandomSize / 100.0);
    QTransform const place = QTransform::fromScale(k, k) * QTransform::fromTranslate(p.x(), p.y());
    Drawing edited = currentDrawing() ? *currentDrawing() : Drawing();
    // Stamped, its parts are one thing: picked together.
    int group = 0;
    if (stamp.strokes.size() + stamp.fills.size() > 1) {
        group = 1;
        for (Stroke const& stroke : std::as_const(edited.strokes))
            group = std::max(group, stroke.group + 1);
        for (Fill const& fill : std::as_const(edited.fills))
            group = std::max(group, fill.group + 1);
    }
    for (Fill fill : stamp.fills) {
        fill.transform(place);
        fill.group = group;
        edited.fills.push_back(fill);
    }
    for (Stroke stroke : stamp.strokes) {
        stroke.transform(place);
        stroke.group = group;
        edited.strokes.push_back(stroke);
    }
    commitDrawing(edited, tr("Stamp"));
    // A stamp of several drawings: the next click puts down another of them.
    if (m_stampOwn && m_stampSet.size() > 1) {
        m_stampTurn = m_stampRandom ? int(QRandomGenerator::global()->bounded(int(m_stampSet.size()))) : (m_stampTurn + 1) % int(m_stampSet.size());
        m_stamp = m_stampSet[m_stampTurn];
    }
    update();
}

// ---- Generate Auto-Matte ---------------------------------------------------------------------------

// A painted shape, in the colour in hand, that covers everything inside the outline of the line art:
// put at the back of the colour art, to stand behind the drawing or to be cut with.
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

// ---- the Pencil Editor ------------------------------------------------------------------------------

void Canvas::thicknessDown(QPointF p, Qt::KeyboardModifiers modifiers)
{
    Drawing const* const d = currentDrawing();
    m_thickStroke = m_thickPoint = -1;
    double nearest = 0.0;
    if (d && (modifiers & Qt::ControlModifier)) {
        // With Ctrl: a new point where the line was pressed, to make it thicker or thinner there.
        // The line keeps its shape and its thickness; the point is as thick as the line was at that place.
        for (int i = int(d->strokes.size()) - 1; i >= 0; --i) {
            if (d->strokes[i].width <= 0.0f || !d->strokes[i].hit(p, 4.0 / std::max(m_zoom, 1e-6)))
                continue;
            Drawing edited = *d;
            if (edited.strokes[i].insertPoint(p) >= 0)
                commitDrawing(edited, tr("Add Point"));
            update();
            return;
        }
        emit message(tr("Hold Ctrl and press on a pencil line to put a point there."));
        return;
    }
    if (d) {
        // The point of a line under the pointer: within the line there, or a few pixels off it.
        for (int i = 0; i < d->strokes.size(); ++i) {
            Stroke const& stroke = d->strokes[i];
            if (stroke.width <= 0.0f)
                continue;
            for (int k = 0; k < stroke.points.size(); ++k) {
                double const away = QLineF(stroke.points[k].pos, p).length();
                double const reach = stroke.width * stroke.points[k].pressure / 2.0 + 8.0 / std::max(m_zoom, 1e-6);
                if (away <= reach && (m_thickStroke < 0 || away < nearest)) {
                    m_thickStroke = i;
                    m_thickPoint = k;
                    nearest = away;
                }
            }
        }
    }
    if (m_thickStroke < 0) {
        emit message(tr("Press on a point of a pencil line and pull: away from the line makes it thicker there, towards it thinner. With Alt, only the side pulled on moves."));
        return;
    }
    m_eraseArt = *d;
    m_thickPos0 = d->strokes[m_thickStroke].points[m_thickPoint].pos;
    m_thickHalf0 = d->strokes[m_thickStroke].width * d->strokes[m_thickStroke].points[m_thickPoint].pressure / 2.0;
    m_drag = DragThickness;
    update();
}

// ---- the Ink tool -----------------------------------------------------------------------------------

void Canvas::inkAt(QPointF p)
{
    QColor const colour = m_document->scene().resolve(m_swatch, Qt::black);
    // A few pixels either side of the line will do: the tool is for lines, and nothing else is in its way.
    double const reach = 4.0 / std::max(m_zoom, 1e-6);
    for (Stroke& stroke : m_eraseArt.strokes) {
        if (stroke.width <= 0.0f || (stroke.swatch == m_swatch && stroke.color == colour) || keepsColour(stroke.swatch) || !stroke.hit(p, reach))
            continue;
        stroke.swatch = m_swatch;
        stroke.color = colour;
        m_inked = true;
    }
    update();
}

// ---- the Repaint Brush -----------------------------------------------------------------------------

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

// ---- the Smooth Editor -----------------------------------------------------------------------------

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
            line.points[k].pressure = was[k].pressure * 0.6f + (before.pressure + after.pressure) * 0.2f;
            line.points[k].in *= 0.8;
            line.points[k].out *= 0.8;
            line.points[k].corner = false;
            any = true;
        }
        return any;
    };
    for (Stroke& stroke : m_eraseArt.strokes)
        m_inked = settle(stroke) || m_inked;
    for (Fill& fill : m_eraseArt.fills) {
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

// ---- the Envelope tool ------------------------------------------------------------------------------

// So many points (eight to begin with) on a loop close round what is selected.
QVector<QPointF> Canvas::selectionEnvelope() const
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection())
        return {};
    Drawing chosen;
    for (int i : m_selStrokes)
        chosen.strokes.push_back(d->strokes[i]);
    for (int i : m_selFills)
        chosen.fills.push_back(d->fills[i]);
    return Deform::envelopeAround(chosen, std::clamp(m_envCount, 4, 32), 6.0 / std::max(m_zoom, 1e-6));
}

QPointF Canvas::envelopePointInView(int point) const
{
    QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
    return m_tool == Envelope && point >= 0 && point * 3 < points.size() ? layerViewTransform().map(points[point * 3]) : QPointF();
}

QPointF Canvas::envelopeHandleInView(bool after) const
{
    QVector<QPointF> const points = m_drag == DragEnvelope ? m_envNow : selectionEnvelope();
    if (m_tool != Envelope || m_envPicked < 0 || m_envPicked + 2 >= points.size())
        return QPointF();
    return layerViewTransform().map(points[m_envPicked] + points[m_envPicked + (after ? 2 : 1)]);
}

bool Canvas::envelopeDown(QPointF widgetPos)
{
    QVector<QPointF> const points = selectionEnvelope();
    QTransform const view = layerViewTransform();
    // A handle of the point pressed last: the outline is led another way through its point.
    if (m_envPicked >= 0 && m_envPicked + 2 < points.size()) {
        for (int which : {1, 2}) {
            if (QLineF(view.map(points[m_envPicked] + points[m_envPicked + which]), widgetPos).length() > 7.0)
                continue;
            m_envRest = m_envNow = points;
            m_envPoint = m_envPicked + which;
            m_eraseArt = *currentDrawing();
            m_drag = DragEnvelope;
            return true;
        }
    }
    for (int i = 0; i < points.size(); i += 3) {
        if (QLineF(view.map(points[i]), widgetPos).length() > 8.0)
            continue;
        m_envRest = m_envNow = points;
        m_envPoint = i;
        m_envPicked = i;
        m_eraseArt = *currentDrawing();
        m_drag = DragEnvelope;
        return true;
    }
    return false;
}

void Canvas::envelopeMove(QPointF p)
{
    Drawing const* const d = currentDrawing();
    if (!d || m_envPoint < 0 || m_envPoint >= m_envNow.size())
        return;
    // The point goes with the pen; the curve is led smoothly through it and its two neighbours again.
    m_envNow = m_envRest;
    if (m_envPoint % 3 != 0) {
        // A handle: it goes with the pen (it is kept as how far it stands from its point); nothing else is led anew.
        m_envNow[m_envPoint] = p - m_envRest[m_envPoint - m_envPoint % 3];
    } else {
        m_envNow[m_envPoint] = p;
        int const count = int(m_envNow.size()) / 3, at = m_envPoint / 3;
        for (int k : {(at + count - 1) % count, at, (at + 1) % count})
            Deform::smoothWarpHandles(m_envNow, true, k);
    }
    QVector<QPointF> moved(m_envRest.size());
    for (int i = 0; i < moved.size(); ++i)
        moved[i] = m_envNow[i] - m_envRest[i];
    // What is selected, reshaped by that; the rest of the drawing as it is.
    Drawing chosen;
    for (int i : std::as_const(m_selStrokes))
        chosen.strokes.push_back(d->strokes[i]);
    for (int i : std::as_const(m_selFills))
        chosen.fills.push_back(d->fills[i]);
    Drawing const pulled = Deform::warp(chosen, m_envRest, true, moved);
    m_eraseArt = *d;
    if (pulled.strokes.size() == m_selStrokes.size() && pulled.fills.size() == m_selFills.size()) {
        for (int k = 0; k < m_selStrokes.size(); ++k)
            m_eraseArt.strokes[m_selStrokes[k]] = pulled.strokes[k];
        for (int k = 0; k < m_selFills.size(); ++k)
            m_eraseArt.fills[m_selFills[k]] = pulled.fills[k];
    }
    update();
}

// ---- Close Gaps ---------------------------------------------------------------------------------

int Canvas::closeGaps(double gap)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return 0;
    bool const some = selecting() && hasSelection();
    // Every line drawn out, to look for the nearest place on it.
    QVector<QVector<QPointF>> paths;
    for (Stroke const& stroke : d->strokes) {
        QVector<QPointF> path;
        for (StrokePoint const& point : stroke.path())
            path.push_back(point.pos);
        if (stroke.closed && !path.isEmpty())
            path.push_back(path.first());
        paths.push_back(path);
    }
    auto nearestOn = [](QPointF a, QPointF b, QPointF p) {
        QPointF const ab = b - a;
        double const along = ab.x() * ab.x() + ab.y() * ab.y();
        double const t = along < 1e-12 ? 0.0 : std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / along, 0.0, 1.0);
        return a + ab * t;
    };
    Drawing edited = *d;
    QVector<QLineF> made;
    for (int i = 0; i < d->strokes.size(); ++i) {
        Stroke const& stroke = d->strokes[i];
        if (stroke.closed || paths[i].size() < 2 || (some && !m_selStrokes.contains(i)))
            continue;
        for (int end = 0; end < 2; ++end) {
            QPointF const from = end == 0 ? paths[i].first() : paths[i].last();
            QPointF best;
            double nearest = gap;
            bool found = false;
            auto look = [&](QPointF q) {
                double const distance = QLineF(from, q).length();
                if (distance <= nearest) {
                    nearest = distance;
                    best = q;
                    found = true;
                }
            };
            for (int j = 0; j < paths.size(); ++j) {
                if (j == i) {
                    look(end == 0 ? paths[i].last() : paths[i].first());     // of its own line, only the other end
                    continue;
                }
                for (int k = 1; k < paths[j].size(); ++k)
                    look(nearestOn(paths[j][k - 1], paths[j][k], from));
            }
            if (!found || nearest < 1e-6)
                continue;       // nothing near, or it touches already
            bool twice = false;
            for (QLineF const& line : std::as_const(made))
                twice = twice || (QLineF(line.p1(), best).length() < 1e-6 && QLineF(line.p2(), from).length() < 1e-6);
            if (twice)
                continue;       // the two ends found each other: one line between them is enough
            made.push_back(QLineF(from, best));
            // A line with no thickness: not seen, but paint stops at it.
            Stroke bridge;
            bridge.width = 0.0f;
            bridge.swatch = stroke.swatch;
            bridge.color = stroke.color;
            bridge.points = {{from, 1.0f}, {best, 1.0f}};
            edited.strokes.push_back(bridge);
        }
    }
    if (!made.isEmpty())
        commitDrawing(edited, tr("Close Gaps"));
    return int(made.size());
}

void Canvas::onActionCloseGaps()
{
    if (!currentDrawing()) {
        emit message(tr("There is no drawing on this frame to clean up."));
        return;
    }
    bool ok = false;
    int const gap = QInputDialog::getInt(this, tr("Close Gaps"), tr("Close gaps no wider than (pixels):"),
                                         settings().value(QStringLiteral("tools/gapSize"), 10).toInt(), 1, 500, 1, &ok);
    if (!ok)
        return;
    settings().setValue(QStringLiteral("tools/gapSize"), gap);
    emit message(tr("%n gap(s) closed.", nullptr, closeGaps(gap)));
}

// ---- contour strokes ------------------------------------------------------------------------------

void Canvas::onActionCreateContourStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selFills.isEmpty()) {
        emit message(tr("Select painted areas with the Select tool to put strokes round them."));
        return;
    }
    // Round each outline of each area, a closed line with no thickness: not seen, but paint stops at it.
    Drawing edited = *d;
    for (int i : std::as_const(m_selFills)) {
        Fill const& fill = d->fills[i];
        bool const curves = fill.contours.size() == fill.outlines.size();
        for (int k = 0; k < fill.outlines.size(); ++k) {
            Stroke round;
            if (curves) {
                round = fill.contours[k];
            } else {
                QPolygonF outline = fill.outlines[k];
                if (outline.size() > 1 && outline.first() == outline.last())
                    outline.removeLast();
                for (QPointF const& point : std::as_const(outline))
                    round.points.push_back({point, 1.0f});
            }
            if (round.points.size() < 3)
                continue;
            round.closed = true;
            round.width = 0.0f;
            round.swatch = fill.swatch;
            round.color = fill.color;
            round.group = fill.group;
            edited.strokes.push_back(round);
        }
    }
    if (edited.strokes.size() != d->strokes.size())
        commitDrawing(edited, tr("Create Contour Strokes"));
    update();
}

void Canvas::onActionRemoveContourStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || !hasSelection()) {
        emit message(tr("Select the artwork to take the strokes with no thickness out of."));
        return;
    }
    Drawing edited = *d;
    edited.strokes.clear();
    QVector<int> lines;
    for (int i = 0; i < d->strokes.size(); ++i) {
        if (m_selStrokes.contains(i) && d->strokes[i].width <= 0.0f)
            continue;
        if (m_selStrokes.contains(i))
            lines.push_back(int(edited.strokes.size()));
        edited.strokes.push_back(d->strokes[i]);
    }
    if (edited.strokes.size() == d->strokes.size())
        return;
    QVector<int> const paints = m_selFills;
    clearSelection();
    commitDrawing(edited, tr("Remove Contour Strokes"));
    m_selStrokes = lines;
    m_selFills = paints;
    emit selectionChanged();
    update();
}

// Remove Extra Strokes: the strokes with no thickness that lie wholly inside paint of one colour
// divide nothing, and go.  Of what is selected, or of the whole drawing when nothing is.
void Canvas::onActionRemoveExtraStrokes()
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    bool const chosen = selecting() && hasSelection();
    // The paint on top at a place: its number, or -1 where there is none.
    auto paintAt = [&](QPointF q) {
        for (int i = int(d->fills.size()) - 1; i >= 0; --i)
            if (d->fills[i].contains(q))
                return i;
        return -1;
    };
    auto extra = [&](Stroke const& stroke) {
        if (stroke.width > 0.0f || stroke.points.size() < 2)
            return false;
        int swatch = Scene::kNoSwatch;
        QColor colour;
        bool first = true;
        auto same = [&](QPointF q) {
            // Not only at the place itself: a little to every side of it too, so that a stroke along an edge stays.
            for (QPointF const off : {QPointF(0, 0), QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
                int const paint = paintAt(q + off);
                if (paint < 0)
                    return false;
                Fill const& fill = d->fills[paint];
                if (first) {
                    swatch = fill.swatch;
                    colour = fill.color;
                    first = false;
                } else if (fill.swatch != swatch || (swatch == Scene::kNoSwatch && fill.color != colour)) {
                    return false;
                }
            }
            return true;
        };
        for (int i = 0; i < stroke.points.size(); ++i) {
            if (!same(stroke.points[i].pos))
                return false;
            if (i + 1 < stroke.points.size() && !same((stroke.points[i].pos + stroke.points[i + 1].pos) / 2.0))
                return false;
        }
        return true;
    };
    Drawing edited = *d;
    edited.strokes.clear();
    QVector<int> lines;
    for (int i = 0; i < d->strokes.size(); ++i) {
        bool const mine = !chosen || m_selStrokes.contains(i);
        if (mine && extra(d->strokes[i]))
            continue;
        if (chosen && m_selStrokes.contains(i))
            lines.push_back(int(edited.strokes.size()));
        edited.strokes.push_back(d->strokes[i]);
    }
    if (edited.strokes.size() == d->strokes.size()) {
        emit message(tr("There are no extra strokes: none with no thickness lies wholly inside paint of one colour."));
        return;
    }
    int const gone = int(d->strokes.size() - edited.strokes.size());
    QVector<int> const paints = chosen ? m_selFills : QVector<int>();
    clearSelection();
    commitDrawing(edited, tr("Remove Extra Strokes"));
    m_selStrokes = lines;
    m_selFills = paints;
    emit selectionChanged();
    emit message(tr("%n extra stroke(s) removed.", nullptr, gone));
    update();
}

// ---- Strokes to Pencil Lines ---------------------------------------------------------------------

void Canvas::onActionStrokesToPencil()
{
    Drawing const* const d = currentDrawing();
    if (!selecting() || !d || m_selStrokes.isEmpty()) {
        emit message(tr("Select the strokes to convert with the Select tool."));
        return;
    }
    // A stroke with no thickness gets the thickness the pencil has now.
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selStrokes)) {
        Stroke& stroke = edited.strokes[i];
        if (stroke.width > 0.0f)
            continue;
        stroke.width = float(m_brushWidth);
        for (StrokePoint& point : stroke.points)
            point.pressure = 1.0f;
        changed = true;
    }
    if (changed)
        commitDrawing(edited, tr("Strokes to Pencil Lines"));
    update();
}
