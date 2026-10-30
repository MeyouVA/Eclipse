// Eclipse - the Function view's graph: the mouse and the keys on it.
#include "ui/timeline/FunctionView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>

#include <cmath>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"

namespace {

// The Function Editor's own keys (Preferences > Keyboard Shortcuts) and the usual ones it answers while it has the focus.
struct LocalKey {
    char const* shortcut;
    char const* command;
    char const* fallback;
};
LocalKey const kLocalKeys[] = {
    {"shortcut.feZoomInX", "zoomInX", "4"},     {"shortcut.feZoomOutX", "zoomOutX", "3"},  {"shortcut.feZoomInY", "zoomInY", "6"},
    {"shortcut.feZoomOutY", "zoomOutY", "5"},   {"shortcut.feToggleGrid", "grid", "G"},    {"shortcut.feInsertPoint", "insert", "I"},
    {"shortcut.cut", "cut", "Ctrl+X"},          {"shortcut.copy", "copy", "Ctrl+C"},       {"shortcut.paste", "paste", "Ctrl+V"},
    {"shortcut.delete", "delete", "Del"},       {"shortcut.selectAll", "selectAll", "Ctrl+A"}, {"shortcut.deselectAll", "deselectAll", "Esc"},
    {"shortcut.feNextKey", "nextKey", "'"},     {"shortcut.fePrevKey", "prevKey", ";"},    {"shortcut.feLeftHandle", "leftHandle", "["},
    {"shortcut.feRightHandle", "rightHandle", "]"}, {"shortcut.feStoppedSegment", "stoppedSegment", ""},
};

}  // namespace

QString FunctionGraph::commandOfKey(QKeyEvent* event) const
{
    QKeySequence const pressed(event->keyCombination());
    if (event->key() == Qt::Key_Backspace)
        return QStringLiteral("delete");
    if (event->key() == Qt::Key_BracketLeft && event->modifiers() == Qt::NoModifier)
        return QStringLiteral("leftHandle");
    if (event->key() == Qt::Key_BracketRight && event->modifiers() == Qt::NoModifier)
        return QStringLiteral("rightHandle");
    for (LocalKey const& k : kLocalKeys) {
        QKeySequence key = QKeySequence(QString::fromLatin1(k.fallback));
        if (m_actions)
            for (ActionManager::Shortcut const& s : m_actions->shortcuts())
                if (s.id == QLatin1String(k.shortcut))
                    key = s.key;
        if (!key.isEmpty() && key.matches(pressed) == QKeySequence::ExactMatch)
            return QString::fromLatin1(k.command);
    }
    return QString();
}

void FunctionGraph::runCommand(QString const& id)
{
    if (id == QLatin1String("zoomInX"))
        zoomFrames(1.0 / 1.25);
    else if (id == QLatin1String("zoomOutX"))
        zoomFrames(1.25);
    else if (id == QLatin1String("zoomInY"))
        zoomValues(1.0 / 1.25);
    else if (id == QLatin1String("zoomOutY"))
        zoomValues(1.25);
    else if (id == QLatin1String("grid"))
        setGridShown(!m_grid);
    else if (id == QLatin1String("insert"))
        insertPoint();
    else if (id == QLatin1String("cut"))
        cutSelection();
    else if (id == QLatin1String("copy"))
        copySelection();
    else if (id == QLatin1String("paste"))
        pasteAtCurrentFrame();
    else if (id == QLatin1String("delete"))
        deleteSelection();
    else if (id == QLatin1String("selectAll"))
        selectAll();
    else if (id == QLatin1String("deselectAll"))
        deselectAll();
    else if (id == QLatin1String("leftHandle"))
        selectHandle(1);
    else if (id == QLatin1String("rightHandle"))
        selectHandle(2);
    else if (id == QLatin1String("nextKey"))
        selectNeighbour(true);
    else if (id == QLatin1String("prevKey"))
        selectNeighbour(false);
    else if (id == QLatin1String("stoppedSegment"))
        toggleStoppedSegment();
}

bool FunctionGraph::event(QEvent* event)
{
    // Its own keys go to it before the window's commands get them.
    if (event->type() == QEvent::ShortcutOverride && !commandOfKey(static_cast<QKeyEvent*>(event)).isEmpty()) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void FunctionGraph::keyPressEvent(QKeyEvent* event)
{
    QString const command = commandOfKey(event);
    if (command.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    runCommand(command);
}

bool FunctionGraph::pointUnder(QPointF pos, FunctionPoint& p) const
{
    double best = 6.0;
    bool found = false;
    for (FunctionPoint const& point : points()) {
        double const d = QLineF(pointOf(point), pos).length();
        if (d <= best) {
            best = d;
            p = point;
            found = true;
        }
    }
    return found;
}

bool FunctionGraph::handleUnder(QPointF pos, FunctionPoint& p, bool& out) const
{
    for (FunctionPoint const& point : m_selection)
        for (bool side : {true, false}) {
            QPointF const h = handleAt(point, side);
            if (!h.isNull() && QLineF(h, pos).length() <= 6.0) {
                p = point;
                out = side;
                return true;
            }
        }
    return false;
}

void FunctionGraph::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_actions)
        return;
    QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.functionCanvas"), this);
    if (!menu)
        return;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void FunctionGraph::mouseDoubleClickEvent(QMouseEvent* event)
{
    // The middle button twice: all of the graph again.
    if (event->button() == Qt::MiddleButton) {
        m_drag = NoDrag;
        resetView();
        return;
    }
    // On the graph where there is no point: a keyframe on that frame, with the pose the layer has there.
    Scene const& scene = m_document->scene();
    FunctionPoint under;
    if (event->button() != Qt::LeftButton || m_layer < 0 || m_layer >= scene.layers.size() || pointUnder(event->position(), under))
        return;
    m_drag = NoDrag;
    int const frame = frameOfX(event->position().x());
    if (scene.layers[m_layer].keyIndex(frame) < 0)
        m_document->setKeyframe(m_layer, frame, scene.layers[m_layer].poseAt(frame), tr("Add Keyframe"));
    emit framePicked(frame);
}

void FunctionGraph::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    m_pressAt = m_lastAt = event->position();
    m_moved = false;
    if (event->button() == Qt::MiddleButton) {
        m_drag = Sliding;
        return;
    }
    FunctionPoint point;
    bool out = false;
    if (event->button() == Qt::RightButton) {
        // On a point not selected: that point's frame chosen first, as a click does (the menu then works on it).
        if (pointUnder(event->position(), point) && !m_selection.contains(point)) {
            QVector<FunctionPoint> there;
            for (FunctionPoint const& p : points())
                if (p.frame == point.frame)
                    there.push_back(p);
            there.removeAll(point);
            there.prepend(point);
            setSelection(there);
        }
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    // A handle of a point selected.
    if (handleUnder(event->position(), point, out)) {
        m_selection.removeAll(point);
        m_selection.prepend(point);
        m_handle = out ? 2 : 1;
        m_drag = Handle;
        m_grip = point;
        m_gripOut = out;
        m_dragCurve = curveAt(point);
        emit selectionChanged();
        update();
        return;
    }
    if (pointUnder(event->position(), point)) {
        Qt::KeyboardModifiers const mods = event->modifiers();
        if (mods & Qt::ControlModifier) {
            // Ctrl: that point alone, in or out.
            if (m_selection.contains(point)) {
                m_selection.removeAll(point);
                m_handle = 0;
                emit selectionChanged();
                update();
                return;
            }
            m_selection.push_back(point);
        } else if (mods & Qt::ShiftModifier) {
            if (!m_selection.contains(point))
                m_selection.push_back(point);
        } else if (!m_selection.contains(point)) {
            // A click: the points on that frame of every curve drawn.
            m_selection.clear();
            for (FunctionPoint const& p : points())
                if (p.frame == point.frame)
                    m_selection.push_back(p);
        }
        m_selection.removeAll(point);
        m_selection.prepend(point);
        m_handle = 0;
        m_drag = Points;
        m_grip = point;
        m_dragBy = 0;
        m_dragDelta = 0.0;
        // Sideways as far as the next points of the same curves that stay where they are.
        boundsOfSelection(m_byLow, m_byHigh);
        emit selectionChanged();
        emit framePicked(point.frame);
        update();
        return;
    }
    // Nowhere: a box round points (or, as a click, that frame).
    m_drag = Box;
    m_box = QRectF(m_pressAt, m_pressAt);
}

void FunctionGraph::mouseMoveEvent(QMouseEvent* event)
{
    QPointF const pos = event->position();
    if (m_drag == Sliding) {
        // The graph goes with the pointer: the frames (when not all are shown) and the values.
        QRectF const r = plot();
        QPointF const d = pos - m_lastAt;
        m_lastAt = pos;
        if (m_span > 0.0) {
            double const whole = std::max(2, m_document->scene().frameCount()) - 1.0;
            m_first = std::clamp(m_first - d.x() * m_span / r.width(), 0.0, whole - m_span);
        }
        double const by = d.y() * (m_high - m_low) / r.height();
        m_low += by;
        m_high += by;
        m_zoomed = true;
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton) || m_drag == NoDrag)
        return;
    if ((pos - m_pressAt).manhattanLength() >= 3)
        m_moved = true;
    if (!m_moved)
        return;
    if (m_drag == Points) {
        m_preview.clear();
        m_previewLayers.clear();
        int by = std::clamp(frameOfX(pos.x()) - m_grip.frame, m_byLow, m_byHigh);
        double delta = valueOfY(pos.y()) - valueOf(m_grip.layer, m_grip.value, m_grip.frame);
        // Shift: one way only, whichever the pointer has gone further.
        if (event->modifiers() & Qt::ShiftModifier) {
            if (std::abs(pos.x() - m_pressAt.x()) > std::abs(pos.y() - m_pressAt.y()))
                delta = 0.0;
            else
                by = 0;
        }
        m_dragBy = by;
        m_dragDelta = delta;
        makePreview();
    } else if (m_drag == Handle) {
        m_preview.clear();
        m_previewLayers.clear();
        m_dragCurve = pulledCurve(m_grip, m_gripOut, pos, event->modifiers() & Qt::AltModifier);
        makePreview();
    } else if (m_drag == Box) {
        m_box = QRectF(m_pressAt, pos).normalized();
    }
    update();
}

void FunctionGraph::mouseReleaseEvent(QMouseEvent* event)
{
    Drag const was = m_drag;
    m_drag = NoDrag;
    m_preview.clear();
    m_previewLayers.clear();
    if (was == Points && m_moved && (m_dragBy != 0 || m_dragDelta != 0.0))
        commitMove(m_dragBy, m_dragDelta);
    else if (was == Handle && m_moved)
        commitHandle();
    else if (was == Box) {
        bool const adding = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
        if (m_moved) {
            QVector<FunctionPoint> chosen = adding ? m_selection : QVector<FunctionPoint>();
            for (FunctionPoint const& p : points())
                if (m_box.contains(pointOf(p)) && !chosen.contains(p))
                    chosen.push_back(p);
            setSelection(chosen);
        } else {
            if (!adding)
                setSelection({});
            emit framePicked(frameOfX(event->position().x()));
        }
    }
    update();
}
