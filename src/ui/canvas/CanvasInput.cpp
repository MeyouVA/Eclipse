// Eclipse - the Camera view: mouse, tablet, wheel and keys.
#include "ui/canvas/Canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include <QMimeData>
#include <QLineEdit>

bool Canvas::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_textEntry && m_textEntry) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            endText(false);
            return true;
        }
        // Shift+Enter: a new line of words.
        if (event->type() == QEvent::KeyPress && (static_cast<QKeyEvent*>(event)->modifiers() & Qt::ShiftModifier)
            && (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Return || static_cast<QKeyEvent*>(event)->key() == Qt::Key_Enter)) {
            m_textEntry->insert(QString(QChar(kLineBreak)));
            return true;
        }
        // Typing is not to set off the commands that have letters as their shortcuts.
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

bool Canvas::event(QEvent* event)
{
    // While a polyline is being laid down, Enter and Escape end it: they are not to set
    // off whatever command has them as its shortcut (Enter plays the scene).
    if (event->type() == QEvent::ShortcutOverride && m_polyActive) {
        int const key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    // The last point of a polyline (its first click has put the point down already).
    if (m_polyActive && event->button() == Qt::LeftButton)
        finishPolyline(true);
}

void Canvas::mousePressEvent(QMouseEvent* event)
{
    if (m_tabletActive)
        return;
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        penDown(event->position(), 1.0f, event->button() == Qt::MiddleButton, event->modifiers());
}

void Canvas::mouseMoveEvent(QMouseEvent* event)
{
    emit pointerMoved(toScene(event->position()));
    m_hoverFrame = toFrame(event->position());
    m_hoverKnown = true;
    if (drawingGuide())
        update();       // its lines follow the pointer
    if (!m_tabletActive && m_drag != DragNone)
        penMove(event->position(), 1.0f, event->modifiers());
    if (m_polyActive && m_drag == DragNone) {
        m_polyHover = snapped(toLayer(event->position()));
        update();
    }
}

void Canvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-eclipse-template")))
        event->acceptProposedAction();
}

void Canvas::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(QStringLiteral("application/x-eclipse-template")))
        return;
    event->acceptProposedAction();
    emit templateDropped(QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-eclipse-template"))));
}

void Canvas::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_tabletActive && m_drag != DragNone)
        penUp();
}

void Canvas::tabletEvent(QTabletEvent* event)
{
    float const pressure = float(std::clamp(event->pressure(), 0.05, 1.0));
    switch (event->type()) {
    case QEvent::TabletPress:
        m_tabletActive = true;
        setFocus();
        // The eraser end of the pen erases when the brush is selected.
        if (event->pointerType() == QPointingDevice::PointerType::Eraser && m_tool == Brush) {
            m_tool = Eraser;
            penDown(event->position(), pressure, false, event->modifiers());
            m_tool = Brush;
        } else {
            penDown(event->position(), pressure, event->button() == Qt::MiddleButton, event->modifiers());
        }
        break;
    case QEvent::TabletMove:
        if (m_drag != DragNone)
            penMove(event->position(), pressure, event->modifiers());
        break;
    case QEvent::TabletRelease:
        if (m_drag != DragNone)
            penUp();
        m_tabletActive = false;
        break;
    default:
        break;
    }
    event->accept();
}

void Canvas::wheelEvent(QWheelEvent* event)
{
    // Zoom about the pointer: the scene point under it stays under it.
    QPointF const anchor = event->position();
    QPointF const scenePoint = toScene(anchor);
    double const factor = std::pow(1.0015, event->angleDelta().y());
    m_zoom = std::clamp(m_zoom * factor, 0.02, 64.0);
    emit zoomChanged();
    m_fitted = false;
    QPointF const moved = viewTransform().map(scenePoint);
    m_pan += anchor - moved;
    update();
}

void Canvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceDown = true;
        updateCursor();
        return;
    }
    if (m_polyActive && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Escape)) {
        finishPolyline(event->key() != Qt::Key_Escape);
        return;
    }
    if (event->key() == Qt::Key_Escape && m_drag != DragNone && m_drag != DragPan) {
        // Abandon what is being dragged.
        m_drag = DragNone;
        m_dragTransform = QTransform();
        m_stroke = Stroke();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void Canvas::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spaceDown = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
