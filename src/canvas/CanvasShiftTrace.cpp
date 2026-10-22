// Eclipse - the Camera view: Shift and Trace - the drawings of the onion skin and the Desk moved, turned or scaled to trace from.
#include "canvas/Canvas.h"

#include <QPainter>

#include <cmath>

#include "model/document/Commands.h"

namespace {

QColor const kDeskTint(70, 110, 220);

}  // namespace

QTransform Canvas::traceOf(int layer, int drawing) const
{
    if (!m_shiftTrace)
        return QTransform();
    if (m_drag == DragTrace && layer == m_traceLayer && drawing == m_traceDrawing)
        return m_traceLive;
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || drawing < 0 || drawing >= scene.layers[layer].drawings.size())
        return QTransform();
    return scene.layers[layer].drawings[drawing].trace;
}

void Canvas::onActionShiftTrace(bool on)
{
    m_shiftTrace = on;
    emit onionChanged();
    update();
}

void Canvas::onActionShiftTraceManipulator()
{
    m_traceManipulator = !m_traceManipulator;
    update();
}

void Canvas::setTraceTarget(int layer, int drawing)
{
    m_traceLayer = layer;
    m_traceDrawing = drawing;
    emit onionChanged();
    update();
}

void Canvas::onActionShiftTraceReset()
{
    if (m_traceLayer >= 0)
        m_document->setTrace(m_traceLayer, m_traceDrawing, QTransform(), tr("Clear Shift and Trace Position"));
}

void Canvas::onActionClearShiftTrace()
{
    m_document->clearTraces(-1);
}

void Canvas::onActionShiftTraceVisibility()
{
    // The drawing being moved shown or hidden: on the Desk, its tick; in the onion skin, for this session.
    if (m_traceLayer < 0)
        return;
    Scene const& scene = m_document->scene();
    QVector<Scene::DeskItem> desk = scene.desk;
    for (Scene::DeskItem& item : desk)
        if (scene.layerIndex(item.layer) == m_traceLayer && item.drawing == m_traceDrawing) {
            item.shown = !item.shown;
            m_document->setDesk(desk, tr("Shift and Trace Visibility"));
            return;
        }
    QPair<int, int> const which(m_traceLayer, m_traceDrawing);
    if (!m_traceHidden.remove(which))
        m_traceHidden.insert(which);
    emit onionChanged();
    update();
}

void Canvas::onActionShiftTraceMove(QString const& way)
{
    // The drawing being moved one place up or down the Desk.
    Scene const& scene = m_document->scene();
    QVector<Scene::DeskItem> desk = scene.desk;
    for (int i = 0; i < desk.size(); ++i) {
        if (scene.layerIndex(desk[i].layer) != m_traceLayer || desk[i].drawing != m_traceDrawing)
            continue;
        int const to = way == QLatin1String("up") ? i - 1 : i + 1;
        if (to < 0 || to >= desk.size())
            return;
        desk.swapItemsAt(i, to);
        m_document->setDesk(desk, way == QLatin1String("up") ? tr("Move Shift and Trace Drawing Up") : tr("Move Shift and Trace Drawing Down"));
        return;
    }
}

void Canvas::paintDesk(QPainter& painter, QTransform const& view) const
{
    // The drawings on the Desk, faint and blue, each where Shift and Trace has put it, the first on top.
    Scene const& scene = m_document->scene();
    for (int i = int(scene.desk.size()) - 1; i >= 0; --i) {
        Scene::DeskItem const& item = scene.desk[i];
        int const l = scene.layerIndex(item.layer);
        if (!item.shown || l < 0 || item.drawing < 0 || item.drawing >= scene.layers[l].drawings.size())
            continue;
        painter.setTransform(traceOf(l, item.drawing) * worldOf(l, m_frame) * view);
        paintOnion(painter, scene, scene.layers[l].drawings[item.drawing], 0.45, kDeskTint);
    }
}

void Canvas::paintTraceBox(QPainter& painter, QTransform const& view) const
{
    Scene const& scene = m_document->scene();
    if (!m_traceManipulator || m_traceLayer < 0 || m_traceLayer >= scene.layers.size() || m_traceDrawing < 0
        || m_traceDrawing >= scene.layers[m_traceLayer].drawings.size())
        return;
    QRectF const box = scene.layers[m_traceLayer].drawings[m_traceDrawing].bounds();
    if (box.isEmpty())
        return;
    painter.save();
    painter.setTransform(traceOf(m_traceLayer, m_traceDrawing) * worldOf(m_traceLayer, m_frame) * view);
    QPen pen(QColor(230, 150, 40), 1.0, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    painter.restore();
}

void Canvas::traceDown(QPointF widgetPos, Qt::KeyboardModifiers modifiers)
{
    // The drawing clicked (in the onion skin or on the Desk) is the one moved; elsewhere, the one moved last.
    Scene const& scene = m_document->scene();
    if (!m_shiftTrace) {
        m_shiftTrace = true;
        emit onionChanged();
    }
    auto hits = [&](int l, int drawing) {
        QRectF const box = scene.layers[l].drawings[drawing].bounds();
        QTransform const toView = traceOf(l, drawing) * worldOf(l, m_frame) * viewTransform();
        return !box.isEmpty() && toView.map(QPolygonF(box)).containsPoint(widgetPos, Qt::OddEvenFill);
    };
    bool found = false;
    for (Scene::DeskItem const& item : scene.desk) {
        int const l = scene.layerIndex(item.layer);
        if (!found && item.shown && l >= 0 && item.drawing >= 0 && item.drawing < scene.layers[l].drawings.size() && hits(l, item.drawing)) {
            m_traceLayer = l;
            m_traceDrawing = item.drawing;
            found = true;
        }
    }
    if (!found && m_layer >= 0 && m_layer < scene.layers.size())
        for (int drawing : onionDrawings())
            if (!found && hits(m_layer, drawing)) {
                m_traceLayer = m_layer;
                m_traceDrawing = drawing;
                found = true;
            }
    if (!found && m_traceLayer < 0) {
        QSet<int> const onion = onionDrawings();
        if (onion.isEmpty()) {
            emit message(tr("Shift and Trace moves the drawings of the onion skin or the Desk: turn the onion skin on, or send drawings to the Desk."));
            return;
        }
        m_traceLayer = m_layer;
        m_traceDrawing = *std::min_element(onion.begin(), onion.end());
    }
    if (m_traceLayer < 0 || m_traceLayer >= scene.layers.size() || m_traceDrawing < 0 || m_traceDrawing >= scene.layers[m_traceLayer].drawings.size())
        return;
    m_traceStart = m_traceLive = scene.layers[m_traceLayer].drawings[m_traceDrawing].trace;
    m_traceFrom = widgetPos;
    m_traceWay = modifiers & Qt::ControlModifier ? 1 : modifiers & Qt::AltModifier ? 2 : 0;
    m_traceCentre = m_traceStart.map(scene.layers[m_traceLayer].drawings[m_traceDrawing].bounds().center());
    m_drag = DragTrace;
    emit onionChanged();
}

void Canvas::traceMove(QPointF widgetPos)
{
    QTransform const toView = worldOf(m_traceLayer, m_frame) * viewTransform();
    if (!toView.isInvertible())
        return;
    QTransform const back = toView.inverted();
    QPointF const from = back.map(m_traceFrom), now = back.map(widgetPos), c = m_traceCentre;
    if (m_traceWay == 0) {
        QPointF const by = now - from;
        m_traceLive = m_traceStart * QTransform::fromTranslate(by.x(), by.y());
    } else if (m_traceWay == 1) {
        double const turn = QLineF(c, now).angleTo(QLineF(c, from));
        m_traceLive = m_traceStart * QTransform::fromTranslate(-c.x(), -c.y()) * QTransform().rotate(turn) * QTransform::fromTranslate(c.x(), c.y());
    } else {
        double const was = QLineF(c, from).length(), is = QLineF(c, now).length();
        double const by = was > 1e-6 ? std::clamp(is / was, 0.05, 20.0) : 1.0;
        m_traceLive = m_traceStart * QTransform::fromTranslate(-c.x(), -c.y()) * QTransform::fromScale(by, by) * QTransform::fromTranslate(c.x(), c.y());
    }
    update();
}

void Canvas::traceUp()
{
    m_drag = DragNone;
    QString const text = m_traceWay == 1 ? tr("Shift And Trace Rotate") : m_traceWay == 2 ? tr("Shift And Trace Scale") : tr("Shift And Trace Move");
    m_document->setTrace(m_traceLayer, m_traceDrawing, m_traceLive, text);
    update();
}
