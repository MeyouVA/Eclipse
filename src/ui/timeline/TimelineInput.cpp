// Eclipse - the Timeline: the mouse - switches, picking, dragging cells and markers, dropping templates.
#include "ui/timeline/Timeline.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include <QMimeData>
#include <QScrollBar>

bool Timeline::pressSwitch(QPoint pos)
{
    Scene const& scene = m_document->scene();
    if (pos.x() >= kHeaderWidth)
        return false;
    int const column = pos.x() < 3 + 5 * kSwitchWidth ? (pos.x() - 3) / kSwitchWidth : -1;
    if (pos.y() < kRulerHeight) {
        // The row over the layers: every layer at once.
        if (column == Enable) {
            m_document->setAllVisible(true);
        } else if (column == Solo) {
            m_document->clearSolo();
        } else if (column == Lock) {
            bool allLocked = true;
            for (Layer const& l : scene.layers)
                allLocked = allLocked && l.locked;
            m_document->setAllLocked(!allLocked);
        } else if (column == Onion) {
            // Off for every layer.
            QVector<int> on;
            for (int l = 0; l < scene.layers.size(); ++l)
                if (scene.layers[l].onion)
                    on.push_back(l);
            m_document->editLayers(on, tr("Onion Skin Off"), [](Layer& l) { l.onion = false; });
        } else if (headerButtonRect(AddLayer).contains(pos)) {
            emit addLayerMenuRequested(viewport()->mapToGlobal(headerButtonRect(AddLayer).bottomLeft()));
        } else if (headerButtonRect(DeleteLayer).contains(pos)) {
            emit deleteLayerRequested();
        } else if (headerButtonRect(AddPeg).contains(pos)) {
            emit addPegRequested();
        }
        return true;
    }
    int const row = rowAt(pos.y());
    if (row < 0)
        return false;
    int const layer = m_rows[row];
    if (isParamRow(row)) {
        // The row of a parameter has no switches: a press on it picks the layer.
        closeEntry(true);
        clearRange();
        setCurrentCell(layer, m_frame);
        return true;
    }
    if (parameterToggleRect(layer).contains(pos)) {
        setParametersShown(layer, !parametersShown(layer));
        return true;
    }
    if (column == Enable) {
        m_document->setLayerVisible(layer, !scene.layers[layer].visible);
    } else if (column == Solo) {
        m_document->setLayerSolo(layer, !scene.layers[layer].solo);
    } else if (column == Lock) {
        m_document->setLayerLocked(layer, !scene.layers[layer].locked);
    } else if (column == Onion) {
        if (scene.layers[layer].holdsDrawings())
            m_document->setLayerOnion(layer, !scene.layers[layer].onion);
    } else if (column == Colour) {
        emit trackColourRequested(layer);
    } else if (foldRect(layer).contains(pos)) {
        m_document->setLayerCollapsed(layer, !scene.layers[layer].collapsed);
    } else {
        return false;
    }
    return true;
}

void Timeline::pick(QPoint pos, bool press)
{
    int const row = rowAt(pos.y());
    int const layer = row >= 0 ? m_rows[row] : m_layer;
    if (pos.x() < kHeaderWidth) {
        if (press && row >= 0)
            setCurrentCell(layer, m_frame);
        return;
    }
    setCurrentCell(layer, frameAt(pos.x()));
}

Timeline::Drop Timeline::dropAt(QPoint pos, int& targetLayer) const
{
    Scene const& scene = m_document->scene();
    int const row = rowAt(pos.y());
    targetLayer = -1;
    if (pos.x() >= kHeaderWidth || row < 0)
        return DropNone;
    targetLayer = m_rows[row];
    if (targetLayer == m_dragLayer)
        return DropNone;
    int const within = pos.y() - kRulerHeight + verticalScrollBar()->value() - row * kRowHeight;
    if (within < kRowHeight / 4)
        return DropAbove;
    if (within >= kRowHeight - kRowHeight / 4)
        return DropBelow;
    // A layer cannot follow itself, directly or through others.
    return scene.isAncestor(m_dragLayer, targetLayer) ? DropNone : DropOnto;
}

void Timeline::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragParamLayer >= 0) {
        m_dragParamLayer = -1;
        if (m_dragParamBegun)
            m_document->endMacro();     // however far it was dragged: one step
        m_dragParamBegun = false;
        return;
    }
    if (m_dragEnd) {
        m_dragEnd = false;
        viewport()->update();
        m_document->setFrameCount(m_endFrames);
        return;
    }
    if (m_dragOnion != 0) {
        m_dragOnion = 0;
        return;
    }
    if (m_dragMarker >= 0) {
        int const marker = m_dragMarker;
        m_dragMarker = -1;
        m_document->moveMarker(marker, m_markerFrame);     // over another marker: it goes back
        viewport()->update();
        return;
    }
    m_selecting = m_scrubbing = false;
    if (m_moving) {
        m_moving = false;
        if (m_moveRows || m_moveFrames) {
            moveSelection(m_moveRows, m_moveFrames, QGuiApplication::keyboardModifiers() & Qt::ControlModifier);
        } else {
            // Only a click in the block: that cell, and the block is gone.
            int const layer = m_rows[m_moveRow], frame = m_moveFrame;
            clearRange();
            setCurrentCell(layer, frame);
        }
        m_moveRows = m_moveFrames = 0;
        viewport()->update();
        return;
    }
    int const layer = m_dragLayer;
    Drop const drop = m_dragging ? m_drop : DropNone;
    int const target = m_dropLayer;
    m_dragLayer = -1;
    m_dragging = false;
    m_drop = DropNone;
    viewport()->update();
    if (drop == DropOnto) {
        m_document->setParent(layer, target);
    } else if (drop == DropAbove || drop == DropBelow) {
        dropLayer(layer, target, drop == DropBelow);
    }
}

void Timeline::dropLayer(int layer, int target, bool below)
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || target < 0 || target >= scene.layers.size() || layer == target)
        return;
    int parent = scene.parentIndex(target);
    int beside = target;
    bool behind = below;
    if (below && !scene.layers[target].collapsed) {
        bool any = false;
        int first = -1;
        for (int l = 0; l < scene.layers.size(); ++l)
            if (scene.parentIndex(l) == target) {
                any = true;
                if (l != layer && l > first)
                    first = l;
            }
        if (any) {
            parent = target;
            beside = first >= 0 ? first : layer;      // (the only one attached to it: it stays)
            behind = false;
        }
    }
    int const id = scene.layers[layer].id;
    m_document->beginMacro(tr("Move Layer"));
    bool placed = true;
    if (scene.parentIndex(layer) != parent)
        placed = m_document->setParent(layer, parent);     // (not into what hangs from it)
    if (placed && beside != layer)
        m_document->moveLayerBeside(scene.layerIndex(id), beside, behind);
    m_document->endMacro();
    setCurrentCell(scene.layerIndex(id), m_frame);
}

void Timeline::mousePressEvent(QMouseEvent* event)
{
    QPoint const pos = event->pos();
    if (event->button() == Qt::LeftButton) {
        if (pos.x() >= kHeaderWidth && sceneEndRect().contains(pos) && !m_rulerLocked) {
            m_dragEnd = true;
            m_endFrames = m_document->scene().frameCount();
            return;
        }
        // A handle of the onion skin, at the foot of the ruler.
        if (m_onionOn && pos.x() >= kHeaderWidth - 4) {
            for (int side : {-1, 1}) {
                if (onionHandleRect(side < 0).adjusted(-2, -2, 2, 2).contains(pos)) {
                    m_dragOnion = side;
                    return;
                }
            }
        }
        // A scene marker taken by its band along the top of the ruler: it is dragged to other frames.
        for (int i = 0; pos.x() >= kHeaderWidth && i < m_document->scene().markers.size(); ++i) {
            if (markerRect(i).contains(pos)) {
                Scene::Marker const& marker = m_document->scene().markers[i];
                m_dragMarker = i;
                m_markerFrame = marker.frame;
                m_markerGrab = (pos.x() - kHeaderWidth + horizontalScrollBar()->value()) / m_cellWidth - marker.frame;
                return;
            }
        }
        int const row = rowAt(pos.y());
        if (pos.y() >= kRulerHeight && isParamRow(row) && pos.x() >= kHeaderWidth - kParamWidth && pos.x() < kHeaderWidth) {
            // On a value: dragged left or right it goes down or up (a double-click types one).
            closeEntry(true);
            clearRange();
            if (m_rows[row] != m_layer)
                setCurrentCell(m_rows[row], m_frame);
            m_dragParamLayer = m_rows[row];
            m_dragParam = Parameter(m_params[row]);
            m_dragParamX = pos.x();
            m_dragParamFrom = parameter(m_dragParamLayer, m_dragParam);
            m_dragParamBegun = false;
            return;
        }
        if (pressSwitch(pos))
            return;
        bool const shift = event->modifiers() & Qt::ShiftModifier;
        if (pos.x() >= kSwitches && pos.x() < kHeaderWidth && row >= 0) {
            if (event->modifiers() & Qt::ControlModifier) {
                toggleLayer(m_rows[row]);
                return;
            }
            if (shift) {
                // Shift on a name: the layers from the current one to this one.
                extendSelection(m_rows[row], m_extentRow >= 0 ? m_extentFrame : m_frame);
                if (m_extentRow < 0 && m_rows[row] != m_layer) {
                    m_extentRow = row;
                    m_extentFrame = m_frame;
                    viewport()->update();
                    emit selectionChanged();
                }
                return;
            }
            m_dragLayer = m_rows[row];
            m_dragOrigin = pos;
        }
        if (pos.x() >= kHeaderWidth && row >= 0) {
            if (shift) {
                extendSelection(m_rows[row], frameAt(pos.x()));
                return;
            }
            // Pressed in the block selected: it is taken hold of, to be dragged elsewhere.
            int const here = rowOfLayer(m_layer), frame = frameAt(pos.x());
            if (m_extentRow >= 0 && here >= 0 && row >= std::min(here, m_extentRow) && row <= std::max(here, m_extentRow)
                && frame >= selectionFirstFrame() && frame <= selectionLastFrame()) {
                m_moving = true;
                m_moveRow = row;
                m_moveFrame = frame;
                m_moveRows = m_moveFrames = 0;
                return;
            }
            m_selecting = true;   // dragging on selects a block
        } else if (pos.x() >= kHeaderWidth) {
            m_scrubbing = true;   // on the ruler: dragging on goes through the frames
        }
        clearRange();
        pick(pos, true);
    } else if (event->button() == Qt::RightButton && pos.x() >= kSwitches) {
        // The menu acts on the block when the pointer is in it; else on the cell under the pointer.
        int const row = rowAt(pos.y());
        int const frame = frameAt(pos.x());
        bool inBlock = false;
        if (m_extentRow >= 0 && row >= 0) {
            int const here = rowOfLayer(m_layer);
            inBlock = row >= std::min(here, m_extentRow) && row <= std::max(here, m_extentRow)
                      && (pos.x() < kHeaderWidth || (frame >= selectionFirstFrame() && frame <= selectionLastFrame()));
        }
        if (!inBlock)
            pick(pos, true);
    }
}

// What a template dragged out of the Library is carried as (see LibraryView::kMimeType).
static char const kTemplateMime[] = "application/x-eclipse-template";

void Timeline::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kTemplateMime)))
        event->acceptProposedAction();
}

void Timeline::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(QLatin1String(kTemplateMime)))
        event->acceptProposedAction();
}

void Timeline::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasFormat(QLatin1String(kTemplateMime)))
        return;
    // Over the cells: from the frame under the pointer.  Over the names: from the current frame.
    QPoint const pos = event->position().toPoint();
    int const frame = pos.x() >= kHeaderWidth ? std::clamp(frameAt(pos.x()), 0, m_document->scene().frameCount() - 1) : m_frame;
    event->acceptProposedAction();
    // (Over the cells it goes onto the layers of the same names, when the scene has them; over the names it comes as new layers.)
    emit templateDropped(QString::fromUtf8(event->mimeData()->data(QLatin1String(kTemplateMime))), frame, pos.x() >= kHeaderWidth);
}

void Timeline::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
}

void Timeline::deleteSelection()
{
    if (m_extentRow < 0)
        m_document->clearCell(m_layer, m_frame);
    else
        m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Clear Cells"));
}

void Timeline::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragParamLayer >= 0) {
        int const moved = event->pos().x() - m_dragParamX;
        if (!m_dragParamBegun && std::abs(moved) < 3)
            return;     // not far enough to mean it
        if (m_dragParamLayer >= m_document->scene().layers.size())
            return;
        if (!m_dragParamBegun) {
            m_dragParamBegun = true;
            m_document->beginMacro(tr("Set %1").arg(rowName(m_dragParamLayer, m_dragParam)));
        }
        // A pixel is one of what places and angles are counted in, a hundredth of a scale or of the opacity.
        double step = m_dragParam == ScaleX || m_dragParam == ScaleY || m_dragParam == Opacity ? 0.01 : m_dragParam == Fov ? 0.5 : 1.0;
        if (isEffectRow(m_dragParam))
            step = rowInPercent(m_dragParamLayer, m_dragParam) ? 0.01 : 0.1;
        else if (isBoneRow(m_dragParam) || isWarpRow(m_dragParam))
            step = 1.0;
        else if (isControllerRow(m_dragParam))
            step = 0.02;
        setParameter(m_dragParamLayer, m_dragParam, m_dragParamFrom + moved * step);
        viewport()->update();
        return;
    }
    if (m_dragEnd) {
        int const x = event->pos().x() - kHeaderWidth + horizontalScrollBar()->value();
        m_endFrames = std::max(1, (x + m_cellWidth / 2) / m_cellWidth);
        viewport()->update();
        return;
    }
    if (m_dragOnion != 0) {
        int const at = int(std::floor(double(event->pos().x() - kHeaderWidth + horizontalScrollBar()->value()) / m_cellWidth));
        int previous = m_onionPrev, next = m_onionNext;
        if (m_dragOnion < 0)
            previous = std::clamp(m_frame - at, 0, 6);
        else
            next = std::clamp(at - m_frame, 0, 6);
        if (previous != m_onionPrev || next != m_onionNext) {
            m_onionPrev = previous;
            m_onionNext = next;
            viewport()->update();
            emit onionRangeChanged(previous, next);
        }
        return;
    }
    if (m_dragMarker >= 0) {
        Scene const& scene = m_document->scene();
        int const length = m_dragMarker < scene.markers.size() ? scene.markers[m_dragMarker].length : 1;
        int const frame = (event->pos().x() - kHeaderWidth + horizontalScrollBar()->value()) / m_cellWidth - m_markerGrab;
        m_markerFrame = std::clamp(frame, 0, std::max(0, scene.frameCount() - length));
        viewport()->update();
        return;
    }
    if (m_dragLayer >= 0 && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragging && (event->pos() - m_dragOrigin).manhattanLength() >= 6)
            m_dragging = true;
        if (m_dragging) {
            m_drop = dropAt(event->pos(), m_dropLayer);
            viewport()->update();
        }
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if (m_moving) {
        int const y = std::clamp(event->pos().y(), int(kRulerHeight), kRulerHeight + int(m_rows.size()) * kRowHeight - verticalScrollBar()->value() - 1);
        int const row = rowAt(y);
        m_moveRows = row < 0 ? 0 : row - m_moveRow;
        m_moveFrames = frameAt(std::max(event->pos().x(), int(kHeaderWidth))) - m_moveFrame;
        clampMove(m_moveRows, m_moveFrames);
        viewport()->update();
        return;
    }
    if (m_scrubbing) {
        // Along the ruler: through the frames, like dragging the playhead.
        setCurrentCell(m_layer, frameAt(std::max(event->pos().x(), int(kHeaderWidth))));
    } else if (m_selecting) {
        // Over the cells: a block from where the drag began to here.
        int const y = std::clamp(event->pos().y(), int(kRulerHeight), kRulerHeight + int(m_rows.size()) * kRowHeight - verticalScrollBar()->value() - 1);
        int const row = rowAt(y);
        if (row >= 0)
            extendSelection(m_rows[row], frameAt(std::max(event->pos().x(), int(kHeaderWidth))));
    }
}

void Timeline::mouseDoubleClickEvent(QMouseEvent* event)
{
    int const row = rowAt(event->pos().y());
    if (isParamRow(row)) {
        if (event->pos().x() >= kHeaderWidth - kParamWidth && event->pos().x() < kHeaderWidth)
            editParameter(m_rows[row], Parameter(m_params[row]));
        return;
    }
    if (row >= 0 && nameRect(m_rows[row]).contains(event->pos()) && !foldRect(m_rows[row]).contains(event->pos()))
        emit renameLayerRequested(m_rows[row]);
}

void Timeline::wheelEvent(QWheelEvent* event)
{
    // Ctrl and the wheel: wider or narrower frames.
    if (event->modifiers() & Qt::ControlModifier) {
        setCellWidth(m_cellWidth + (event->angleDelta().y() > 0 ? 1 : -1));
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}
