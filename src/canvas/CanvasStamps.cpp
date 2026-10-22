// Eclipse - the Camera view: stamps - made from drawings, kept, chosen, stamped.
// not), in this drawing or in every drawing of the layer; closing gaps; strokes made pencil lines.
#include "canvas/Canvas.h"

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
