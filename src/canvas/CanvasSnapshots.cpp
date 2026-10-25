// Eclipse - five camera snapshots kept for comparing compositions.
#include "canvas/Canvas.h"

#include "model/document/Commands.h"

void Canvas::onActionTakeSnapshot(QString slot)
{
    bool valid = false;
    int const index = slot.toInt(&valid) - 1;
    if (!valid || index < 0 || index >= 5)
        return;
    m_snapshotSlots[index] = modeImage(m_document->scene(), m_frame, ViewRender);
    m_snapshot = m_snapshotSlots[index];
    emit snapshotChanged();
    emit message(tr("Snapshot %1 taken of frame %2.").arg(index + 1).arg(m_frame + 1));
    update();
}

void Canvas::onActionShowSnapshot(QString slot)
{
    bool valid = false;
    int const index = slot.toInt(&valid) - 1;
    if (!valid || index < 0 || index >= 5)
        return;
    if (m_snapshotSlots[index].isNull()) {
        emit message(tr("Snapshot %1 is empty. Take a snapshot in this slot first.").arg(index + 1));
        return;
    }
    m_snapshot = m_snapshotSlots[index];
    setShowSnapshot(true);
    emit message(tr("Showing snapshot %1. View > Snapshots > Show Scene returns to the current frame.").arg(index + 1));
}
