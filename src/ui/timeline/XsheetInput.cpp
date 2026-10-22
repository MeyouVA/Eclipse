// Eclipse - the Xsheet: the mouse and the keyboard, typing into cells, and dragging (columns, blocks, exposures).
#include "ui/timeline/Xsheet.h"

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QWheelEvent>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

void Xsheet::pick(QPoint pos)
{
    Scene const& scene = m_document->scene();
    int const column = columnAt(pos.x());
    int const row = frameAt(pos.y());
    bool const inRows = pos.y() >= headerHeight() && row >= 0 && row < scene.frameCount();
    // A click on a header changes only the column, on a frame number only the frame.
    if (column >= 0 && column != m_column)
        setCurrentColumn(column);
    if (inRows && row != m_frame)
        emit cellPicked(m_layer, row);
}

void Xsheet::mousePressEvent(QMouseEvent* event)
{
    QPoint const pos = event->pos();
    endNote(true);
    Scene const& scene = m_document->scene();
    int const column = columnAt(pos.x());
    int const row = frameAt(pos.y());
    bool const inHeader = pos.y() < headerHeight();
    bool const inRows = !inHeader && row >= 0 && row < scene.frameCount();
    bool const shift = event->modifiers() & Qt::ShiftModifier;
    m_dragFrom = pos;
    if (event->button() == Qt::LeftButton && inHeader && column >= 0) {
        QRect const header = headerRect(column);
        if (pos.x() >= header.right() - 3 && m_columns[column].function < 0) {
            m_drag = DragResize;          // its right edge: how wide it is
            m_dragColumn = column;
            m_dragWidth = m_columns[column].width;
            return;
        }
        if (shift) {
            extendSelectionToColumn(m_extent < 0 ? m_frame : m_extent, column);
            return;
        }
        extendSelection(m_frame);
        setCurrentColumn(column);
        if (m_columns[column].function < 0) {
            m_drag = DragColumn;          // dragged along: the column goes elsewhere
            m_dragColumn = column;
            m_dragTarget = -1;
        }
        return;
    }
    if (event->button() == Qt::LeftButton && inRows && column >= 0) {
        int const layer = m_columns[column].layer;
        if (int const marker = sheetMarkerAt(pos); marker >= 0) {
            Scene::SheetMarker const& m = scene.sheetMarkers[marker];
            m_dragMarker = marker;
            m_markerFrame = m.frame;
            m_markerLength = m.length;
            m_markerGrab = row - m.frame;
            m_drag = row == m.frame + m.length - 1 && m.length > 1 ? DragMarkerEnd : DragMarker;
            return;
        }
        if (m_columns[column].function < 0 && scene.layers[layer].kind == Layer::Annotation && m_annotationDrawing) {
            if (column != m_column)
                setCurrentColumn(column);
            QRect const cell = columnCellRect(column, 0);
            m_ink = {QPointF(double(pos.x() - cell.x()) / std::max(1, cell.width()), double(pos.y() - cell.y()) / rowHeight())};
            m_drag = event->modifiers() & Qt::ControlModifier ? DragErase : DragInk;
            return;
        }
        if (m_gestural && !shift && beginGesture(pos))
            return;
        if (shift) {
            extendSelectionToColumn(row, column);     // from the current cell to this one
            return;
        }
    }
    if (event->button() == Qt::RightButton && hasSelection() && row >= selectionFirstFrame() && row <= selectionLastFrame()
        && selectedColumns().contains(column))
        return;                     // the menu, for the frames selected
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        if (shift && event->button() == Qt::LeftButton && pos.x() < kRowHeaderWidth && inRows) {
            extendSelection(row);
            return;
        }
        extendSelection(m_frame);
        pick(pos);
        m_selecting = event->button() == Qt::LeftButton && pos.x() >= kRowHeaderWidth && !inHeader;
    }
}

void Xsheet::mouseMoveEvent(QMouseEvent* event)
{
    QPoint const pos = event->pos();
    Scene const& scene = m_document->scene();
    if (!(event->buttons() & Qt::LeftButton)) {
        // What a press here would do.
        int const column = columnAt(pos.x());
        Qt::CursorShape shape = Qt::ArrowCursor;
        if (column >= 0 && pos.y() < headerHeight() && pos.x() >= headerRect(column).right() - 3 && m_columns[column].function < 0)
            shape = Qt::SizeHorCursor;
        else if (column >= 0 && sheetMarkerAt(pos) >= 0)
            shape = Qt::SizeVerCursor;
        else if (m_gestural && column >= 0 && hasSelection() && selectedColumns().value(0, -2) == column
                 && pos.x() - columnCellRect(column, 0).x() < 5 && frameAt(pos.y()) >= selectionFirstFrame() && frameAt(pos.y()) <= selectionLastFrame())
            shape = Qt::SizeAllCursor;
        viewport()->setCursor(shape);
        return;
    }
    int const dx = pos.x() - m_dragFrom.x();
    switch (m_drag) {
    case DragResize: {
        int const width = std::clamp(m_dragWidth + dx, 20, 1000);
        m_columns[m_dragColumn].width = width;
        int x = 0;
        for (Column& c : m_columns) {
            c.x = x;
            x += c.width;
        }
        m_contentWidth = x;
        updateScrollBars();
        viewport()->update();
        return;
    }
    case DragColumn: {
        if (std::abs(dx) < 4 && m_dragTarget < 0)
            return;
        int const over = columnAt(std::max(pos.x(), int(kRowHeaderWidth)));
        int target = int(m_columns.size());
        if (over >= 0)
            target = pos.x() < headerRect(over).center().x() ? over : over + 1;
        m_dragTarget = target;
        viewport()->update();
        return;
    }
    case DragBlock: {
        QVector<int> const across = selectedColumns();
        int const over = columnAt(std::clamp(pos.x(), int(kRowHeaderWidth), viewport()->width() - 1));
        int const from = columnAt(m_dragFrom.x());
        m_dragRows = over >= 0 && from >= 0 ? std::clamp(over - from, -across.first(), int(m_columns.size()) - 1 - across.last()) : m_dragRows;
        m_dragFrames = std::clamp(frameAt(pos.y()) - frameAt(m_dragFrom.y()), -selectionFirstFrame(), scene.frameCount() - 1 - selectionLastFrame());
        viewport()->update();
        return;
    }
    case DragExposure:
        moveGesture(pos);
        return;
    case DragInk:
    case DragErase: {
        QRect const cell = columnCellRect(m_column, 0);
        m_ink.push_back(QPointF(std::clamp(double(pos.x() - cell.x()) / std::max(1, cell.width()), 0.0, 1.0), double(pos.y() - cell.y()) / rowHeight()));
        viewport()->update();
        return;
    }
    case DragMarker:
    case DragMarkerEnd: {
        Scene::SheetMarker const& m = scene.sheetMarkers[m_dragMarker];
        int const row = frameAt(pos.y());
        if (m_drag == DragMarker) {
            m_markerFrame = std::clamp(row - m_markerGrab, 0, scene.frameCount() - m.length);
        } else {
            m_markerLength = std::clamp(row - m.frame + 1, 1, scene.frameCount() - m.frame);
        }
        viewport()->update();
        return;
    }
    default:
        break;
    }
    if (m_selecting) {              // down the column, and across to other columns: a block of cells
        int const column = columnAt(std::clamp(pos.x(), int(kRowHeaderWidth), kRowHeaderWidth + m_contentWidth - horizontalScrollBar()->value() - 1));
        extendSelectionToColumn(frameAt(std::max(pos.y(), headerHeight())), column >= 0 ? column : m_extentColumn);
    } else {
        pick(pos);
    }
}

void Xsheet::mouseReleaseEvent(QMouseEvent*)
{
    Drag const drag = m_drag;
    m_drag = NoDrag;
    m_selecting = false;
    Scene const& scene = m_document->scene();
    switch (drag) {
    case DragResize: {
        int const layer = layerOfColumn(m_dragColumn);
        int const width = m_columns.value(m_dragColumn).width;
        if (layer >= 0 && width != m_dragWidth)
            m_document->editLayers({layer}, tr("Xsheet Column Width"), [width](Layer& l) { l.columnWidth = width; l.columnCollapsed = false; });
        else
            rebuildColumns();
        break;
    }
    case DragColumn: {
        int const from = layerOfColumn(m_dragColumn);
        int const target = m_dragTarget;
        if (from < 0 || target < 0 || target == m_dragColumn || target == m_dragColumn + 1)
            break;
        // To just in front of the layer of the column it goes before (left is in front), or just behind the last.
        int to = 0;
        if (target < m_columns.size()) {
            int const l = m_columns[target].layer;
            to = from > l ? l + 1 : l;
        } else {
            int const l = m_columns.last().layer;
            to = from > l ? l : l - 1;
        }
        if (to != from && to >= 0 && to < scene.layers.size()) {
            m_document->editScene(tr("Move Xsheet Column"), [from, to](Scene& s) { s.layers.move(from, to); });
            emit cellPicked(to, m_frame);
        }
        break;
    }
    case DragBlock: {
        if (m_dragRows == 0 && m_dragFrames == 0)
            break;
        QVector<int> const across = selectedColumns();
        QVector<int> from, to;
        for (int c : across) {
            if (m_columns[c].function >= 0)
                continue;
            int const t = c + m_dragRows;
            from.push_back(m_columns[c].layer);
            to.push_back(t >= 0 && t < m_columns.size() && m_columns[t].function < 0 && scene.layers[m_columns[t].layer].holdsDrawings() ? m_columns[t].layer : -1);
        }
        if (from.isEmpty())
            break;
        int const first = selectionFirstFrame(), last = selectionLastFrame();
        bool const copy = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
        m_document->moveCells(from, first, last, to, first + m_dragFrames, copy);
        // The block stays selected where it went.
        int const toColumn = std::clamp(across.first() + m_dragRows, 0, int(m_columns.size()) - 1);
        int const farColumn = std::clamp(across.last() + m_dragRows, 0, int(m_columns.size()) - 1);
        setCurrentColumn(toColumn);
        emit cellPicked(m_columns[toColumn].layer, first + m_dragFrames);
        extendSelectionToColumn(last + m_dragFrames, farColumn);
        break;
    }
    case DragExposure:
        endGesture(true);
        break;
    case DragInk:
        drawAnnotation(m_ink);
        m_ink.clear();
        break;
    case DragErase: {
        // The pen marks the eraser went over go.
        QVector<QPointF> const path = m_ink;
        m_ink.clear();
        if (m_layer < 0 || m_layer >= scene.layers.size())
            break;
        QRect const cell = columnCellRect(m_column, 0);
        double const across = std::max(1, cell.width()), down = rowHeight();
        auto near = [&](QPointF a, QPointF b) { return QLineF(a.x() * across, a.y() * down, b.x() * across, b.y() * down).length() < 6.0; };
        QVector<AnnotationInk> left;
        for (AnnotationInk const& mark : scene.layers[m_layer].ink) {
            bool hit = false;
            for (QPointF const& a : mark.points)
                for (QPointF const& b : path)
                    hit = hit || near(a, b);
            if (!hit)
                left.push_back(mark);
        }
        if (left.size() != scene.layers[m_layer].ink.size())
            m_document->editLayers({m_layer}, tr("Erase Annotation"), [left](Layer& l) { l.ink = left; });
        break;
    }
    case DragMarker:
    case DragMarkerEnd:
        moveSheetMarker(m_dragMarker, m_markerFrame, m_markerLength);
        m_dragMarker = -1;
        break;
    default:
        break;
    }
    viewport()->update();
}

void Xsheet::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    int const column = columnAt(event->pos().x());
    if (column < 0)
        return;
    if (event->pos().y() < headerHeight()) {
        if (m_columns[column].function < 0 && event->pos().x() < headerRect(column).right() - 3)
            emit renameRequested(m_columns[column].layer);
        return;
    }
    m_drag = NoDrag;
    pick(event->pos());
    Scene const& scene = m_document->scene();
    if (m_columns[column].function < 0 && scene.layers[m_columns[column].layer].kind == Layer::Annotation) {
        if (!m_annotationDrawing)
            beginNote();
        return;
    }
    beginTyping();
}

void Xsheet::keyPressEvent(QKeyEvent* event)
{
    Scene const& scene = m_document->scene();
    Qt::KeyboardModifiers const mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    QString const text = event->text();
    if (mods == Qt::NoModifier && text.size() == 1 && (text[0].isLetterOrNumber() || text[0] == QLatin1Char('.') || text[0] == QLatin1Char('-'))) {
        if (functionOfColumn(m_column) >= 0 || (m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].holdsDrawings())) {
            beginTyping(text);
            return;
        }
    }
    bool const shift = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_F2:
        if (m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].kind == Layer::Annotation && functionOfColumn(m_column) < 0)
            beginNote();
        else
            beginTyping();
        return;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        int const step = event->key() == Qt::Key_Up ? -1 : 1;
        if (shift) {
            extendSelectionToColumn((m_extent < 0 ? m_frame : m_extent) + step, m_extentColumn);
        } else if (m_frame + step >= 0 && m_frame + step < scene.frameCount()) {
            emit cellPicked(m_layer, m_frame + step);
        }
        return;
    }
    case Qt::Key_Left:
    case Qt::Key_Right: {
        int const step = event->key() == Qt::Key_Left ? -1 : 1;
        if (shift)
            extendSelectionToColumn(m_extent < 0 ? m_frame : m_extent, std::clamp((m_extentColumn < 0 ? m_column : m_extentColumn) + step, 0, int(m_columns.size()) - 1));
        else
            setCurrentColumn(m_column + step);
        keepInView();
        return;
    }
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void Xsheet::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0)
            zoomIn();
        else if (event->angleDelta().y() < 0)
            zoomOut();
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void Xsheet::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->pos().y() < headerHeight()) {
        emit headerMenuRequested(event->globalPos());
        return;
    }
    m_menuMarker = sheetMarkerAt(event->pos());
    if (m_menuMarker >= 0) {
        emit sheetMarkerMenuRequested(event->globalPos());
        return;
    }
    emit contextMenuRequested(event->globalPos());
}

// ---- typing ----

void Xsheet::beginTyping(QString const& start)
{
    endTyping(false);
    Scene const& scene = m_document->scene();
    int const function = functionOfColumn(m_column);
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_frame < 0 || m_frame >= scene.frameCount() || m_column < 0
        || (function < 0 && !scene.layers[m_layer].holdsDrawings()))
        return;
    m_entry = new QLineEdit(viewport());
    m_entry->setObjectName(QStringLiteral("XsheetEntry"));
    m_entry->setFrame(false);
    if (start.isEmpty()) {
        m_entry->setText(function >= 0 ? functionText(m_layer, function, m_frame)
                                       : scene.layers[m_layer].drawingLabel(scene.layers[m_layer].drawingAt(m_frame)));
        m_entry->selectAll();
    } else {
        m_entry->setText(start);
        m_entry->end(false);
    }
    m_entry->setGeometry(columnCellRect(m_column, m_frame).adjusted(0, 0, -1, -1));
    m_entry->installEventFilter(this);
    connect(m_entry, &QLineEdit::returnPressed, this, [this] { endTyping(true); });
    m_entry->show();
    m_entry->setFocus();
}

void Xsheet::endTyping(bool keep)
{
    if (!m_entry)
        return;
    QLineEdit* const entry = m_entry;
    m_entry = nullptr;
    QString const typed = entry->text();
    entry->removeEventFilter(this);
    entry->hide();
    entry->setObjectName(QString());     // on its way out: not the entry to look for any more
    entry->deleteLater();
    if (!keep) {
        setFocus();
        return;
    }
    int const layer = m_layer, frame = m_frame, function = functionOfColumn(m_column);
    int const frames = m_document->scene().frameCount();
    if (function >= 0) {
        bool ok = false;
        double const value = typed.trimmed().toDouble(&ok);
        if (ok)
            setFunctionValue(layer, function, frame, function == Timeline::Opacity ? value / 100.0 : value);
        if (frame + 1 < m_document->scene().frameCount()) {
            emit cellPicked(layer, frame + 1);
            beginTyping();
        } else {
            setFocus();
        }
        return;
    }
    // Held for as many frames as the preference says (as far as the scene goes).
    int const hold = std::max(1, std::min(m_defaultHold, frames - frame));
    if (hold > 1 || m_insert)
        m_document->beginMacro(tr("Type Drawing"));
    if (m_insert)
        for (int k = 0; k < hold; ++k)
            m_document->insertBlankCell(layer, frame);
    for (int k = 0; k < hold; ++k)
        m_document->exposeDrawing(layer, frame + k, typed);
    if (hold > 1 || m_insert)
        m_document->endMacro();
    // On to the frame below, to type the next.
    if (frame + hold < m_document->scene().frameCount()) {
        setCurrentCell(layer, frame + hold);
        emit cellPicked(layer, frame + hold);
        beginTyping();
    } else {
        setFocus();
    }
}

void Xsheet::setFunctionValue(int layer, int function, int frame, double value)
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || function < 0 || function >= Timeline::ParameterCount)
        return;
    for (Driver const& d : scene.layers[layer].drivers)
        if (d.value == function)
            return;         // it follows another layer: nothing to type in
    if (function == Timeline::Fov) {
        if (scene.layers[layer].kind == Layer::Camera)
            m_document->setFov(layer, value, frame);
        return;
    }
    if (function == Timeline::Opacity)
        value = std::clamp(value, 0.0, 1.0);
    Pose pose = scene.layers[layer].poseAt(frame);
    Timeline::setParameterOf(pose, Timeline::Parameter(function), value);
    m_document->setPartKeyframe(layer, frame, 1 << function, pose, tr("Set Xsheet Entry"));
}

bool Xsheet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_entry && m_entry) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            endTyping(false);
            return true;
        }
        if (event->type() == QEvent::FocusOut) {
            endTyping(false);
            return false;
        }
    }
    if (watched == m_note && m_note) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* const key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                endNote(false);
                return true;
            }
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && (key->modifiers() & Qt::ControlModifier)) {
                endNote(true);
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            endNote(true);
            return false;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

// ---- gestural drag ----

bool Xsheet::beginGesture(QPoint pos)
{
    Scene const& scene = m_document->scene();
    int const column = columnAt(pos.x());
    int const f = frameAt(pos.y());
    if (column < 0 || f < 0 || f >= scene.frameCount())
        return false;
    // The left edge of the block selected: the block goes where it is dragged.
    QVector<int> const across = selectedColumns();
    if (hasSelection() && !across.isEmpty() && column == across.first() && f >= selectionFirstFrame() && f <= selectionLastFrame()
        && pos.x() - columnCellRect(column, f).x() < 5) {
        m_drag = DragBlock;
        m_dragRows = m_dragFrames = 0;
        return true;
    }
    // The bottom edge of an exposure: it is held on (or cut short) to where it is let go.
    int const l = m_columns[column].layer;
    if (m_columns[column].function >= 0 || !scene.layers[l].holdsDrawings() || scene.layers[l].locked)
        return false;
    Layer const& layer = scene.layers[l];
    int const d = layer.drawingAt(f);
    if (d == Layer::kNoDrawing || (f + 1 < scene.frameCount() && layer.drawingAt(f + 1) == d && !layer.isKey(f + 1)))
        return false;
    if (columnCellRect(column, f).bottom() - pos.y() > 3)
        return false;
    int const start = layer.exposureStart(f);
    if (start < 0)
        return false;
    extendSelection(m_frame);
    if (column != m_column)
        setCurrentColumn(column);
    m_exposureLayer = l;
    m_exposureStart = start;
    m_exposureEnd = f + 1;
    m_exposureCells = layer.cells;
    m_exposureBegun = false;
    m_drag = DragExposure;
    return true;
}

void Xsheet::moveGesture(QPoint pos)
{
    Scene const& scene = m_document->scene();
    if (m_exposureLayer < 0 || m_exposureLayer >= scene.layers.size())
        return;
    int const last = std::clamp(frameAt(pos.y()), m_exposureStart, scene.frameCount() - 1);
    int const drawing = m_exposureCells.value(m_exposureStart, Layer::kNoDrawing);
    int const through = std::max(last + 1, m_exposureEnd);
    QVector<int> cells;
    for (int f = m_exposureStart; f < through && f < m_exposureCells.size(); ++f)
        cells.push_back(f <= last ? drawing : f < m_exposureEnd ? int(Layer::kNoDrawing) : m_exposureCells[f]);
    if (cells == m_exposureCells.mid(m_exposureStart, cells.size()) && !m_exposureBegun)
        return;
    if (!m_exposureBegun) {
        m_exposureBegun = true;
        m_document->beginMacro(tr("Extend Exposure"));
    }
    m_document->setCells(m_exposureLayer, m_exposureStart, cells, tr("Extend Exposure"));
    emit cellPicked(m_exposureLayer, last);
    viewport()->update();
}

void Xsheet::endGesture(bool)
{
    if (m_exposureBegun)
        m_document->endMacro();
    m_exposureLayer = -1;
    m_exposureBegun = false;
    m_exposureCells.clear();
}

// ---- the clipboard ----

void Xsheet::copy()
{
    Timeline::clipboard() = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame());
    Canvas::setArtworkCopiedLast(false);
}

void Xsheet::cut()
{
    copy();
    m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Cut Cells"));
}

void Xsheet::paste()
{
    if (Canvas::artworkCopiedLast()) {
        emit pasteArtwork();
        return;
    }
    // From the left-most column selected, onto as many layers' columns to the right as there are.
    QVector<int> layers;
    QVector<int> const across = selectedColumns();
    for (int c = across.isEmpty() ? 0 : across.first(); c < m_columns.size(); ++c)
        if (m_columns[c].function < 0)
            layers.push_back(m_columns[c].layer);
    if (layers.isEmpty())
        layers.push_back(m_layer);
    m_document->pasteCells(layers, selectionFirstFrame(), Timeline::clipboard());
}
