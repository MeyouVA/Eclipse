// Eclipse - the Camera view: mouse, tablet, wheel and keys.
#include "canvas/Canvas.h"
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QTabletEvent>
#include <QWheelEvent>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include <QMimeData>
#include "canvas/TextEntry.h"

bool Canvas::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_textEntry && m_textEntry) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            endText(false);
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
    if (event->type() == QEvent::ShortcutOverride && m_bending) {
        int const key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { event->accept(); return true; }
    }
    if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
        && (m_drag != DragNone || m_bending || m_tabletActive)) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::ShortcutOverride && m_polyActive) {
        int const key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    // And while a curve of the Line tool is being bent, Escape lets it go (it is not Deselect All).
    if (event->type() == QEvent::ShortcutOverride && m_bending && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
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
    if (m_tabletActive || m_mouseButton != Qt::NoButton)
        return;
    if (event->button() == Qt::MiddleButton && !m_middlePans)
        return;     // (Preferences: the middle button does not pan)
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        penDown(event->position(), 1.0f, event->button() == Qt::MiddleButton, event->modifiers());
        m_mouseButton = event->button();
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* event)
{
    emit pointerMoved(toScene(event->position()));
    m_hoverFrame = toFrame(event->position());
    m_hoverWidget = event->position();
    if (m_tool == Stamp || (m_brushOutline && (m_tool == Brush || m_tool == Eraser || m_tool == Pencil)))
        update();       // the stamp (or the brush's size) is shown under the pointer
    m_hoverKnown = true;
    if (drawingGuide())
        update();       // its lines follow the pointer
    if (!m_tabletActive && m_drag != DragNone)
        penMove(event->position(), 1.0f, event->modifiers());
    if (m_polyActive && m_drag == DragNone) {
        m_polyHover = snapped(toLayer(m_stroke.points.isEmpty() ? event->position()
                                                               : guidedFrom(layerViewTransform().map(m_stroke.points.last().pos), event->position())));
        update();
    }
}

QStringList Canvas::pictureFiles(QList<QUrl> const& urls)
{
    QList<QByteArray> const readable = QImageReader::supportedImageFormats();
    QStringList files;
    for (QUrl const& url : urls)
        if (url.isLocalFile() && readable.contains(QFileInfo(url.toLocalFile()).suffix().toLower().toLatin1()))
            files << url.toLocalFile();
    return files;
}

void Canvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-eclipse-template"))
        || (!m_drawingView && event->mimeData()->hasUrls() && !pictureFiles(event->mimeData()->urls()).isEmpty()))
        event->acceptProposedAction();
}

void Canvas::dropEvent(QDropEvent* event)
{
    if (!m_drawingView && event->mimeData()->hasUrls()) {
        QStringList const files = pictureFiles(event->mimeData()->urls());
        if (!files.isEmpty()) {
            event->acceptProposedAction();
            emit filesDropped(files, toScene(event->position()));
            return;
        }
    }
    if (!event->mimeData()->hasFormat(QStringLiteral("application/x-eclipse-template")))
        return;
    event->acceptProposedAction();
    emit templateDropped(QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-eclipse-template"))));
}

void Canvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_tabletActive || event->button() != m_mouseButton)
        return;
    if (m_drag != DragNone) {
        if (event->position() != m_lastWidgetPos || m_drag == DragShape)
            penMove(event->position(), 1.0f, event->modifiers());
        penUp();
    }
    m_mouseButton = Qt::NoButton;
    closeMorphMacro();
}

void Canvas::wheelEvent(QWheelEvent* event)
{
    // Zoom about the pointer: the scene point under it stays under it.
    QPointF const anchor = event->position();
    QPointF const scenePoint = toScene(anchor);
    double const factor = std::pow(1.0015, event->angleDelta().y());
    m_zoom = std::clamp(m_zoom * factor, m_zoomMin, m_zoomMax);
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
    if (m_bending && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        m_bending = false;
        m_drag = DragNone;
        keepShape();
        closeMorphMacro();
        update();
        return;
    }
    if (event->key() == Qt::Key_Escape && (m_drag != DragNone || m_bending || m_tabletActive)) {
        // Abandon what is being dragged.
        cancelInput();
        return;
    }
    // Alt and the up or down arrow: the lines selected a little thicker or thinner (Nudge Selection Thickness).
    if (m_tool == Select && hasSelection() && m_drag == DragNone && (event->modifiers() & Qt::AltModifier)
        && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        nudgeSelectionThickness(event->key() == Qt::Key_Up, event->modifiers() & Qt::ShiftModifier);
        return;
    }
    // The arrow keys nudge what the Select tool has selected: a pixel (or what Preferences says), ten times that with Shift.
    if (m_tool == Select && hasSelection() && m_drag == DragNone
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        double const step = m_nudgeStep * (event->modifiers() & Qt::ShiftModifier ? 10.0 : 1.0);
        QPointF const by(event->key() == Qt::Key_Left ? -step : event->key() == Qt::Key_Right ? step : 0.0,
                         event->key() == Qt::Key_Up ? -step : event->key() == Qt::Key_Down ? step : 0.0);
        transformSelection(QTransform::fromTranslate(by.x(), by.y()), tr("Nudge"));
        return;
    }
    // With the Transform tool, they nudge the layer (and the others picked with it).
    if (m_tool == Transform && m_drag == DragNone && !m_drawingView && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        nudgeLayer(QPointF(event->key() == Qt::Key_Left ? -1.0 : event->key() == Qt::Key_Right ? 1.0 : 0.0,
                           event->key() == Qt::Key_Up ? -1.0 : event->key() == Qt::Key_Down ? 1.0 : 0.0), event->modifiers() & Qt::ShiftModifier);
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
