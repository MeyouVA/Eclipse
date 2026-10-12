// Eclipse - the main window: filling cells (Fill Selection, Sequence Fill, Hold Exposure); ungrouping layers.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QSpinBox>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/io/ImportExportPrivate.h"

int MainWindow::fillSelection()
{
    int const l = layer();
    Layer const& now = m_document->scene().layers[l];
    int const drawing = now.holdsDrawings() ? now.drawingAt(frame()) : Layer::kNoDrawing;
    if (drawing == Layer::kNoDrawing)
        return 0;
    int const first = m_timeline->selectionFirstFrame(), count = m_timeline->selectionLastFrame() - first + 1;
    m_document->setCells(l, first, QVector<int>(count, drawing), tr("Fill Selection"));
    return count;
}

int MainWindow::sequenceFill(int start, int increment, int hold, bool cycle)
{
    int const l = layer();
    Layer const& now = m_document->scene().layers[l];
    int const drawings = now.holdsDrawings() ? int(now.drawings.size()) : 0;
    if (drawings == 0 || hold < 1)
        return 0;
    int const first = m_timeline->selectionFirstFrame(), count = m_timeline->selectionLastFrame() - first + 1;
    QVector<int> cells;
    for (int i = 0; i < count; ++i) {
        int drawing = start + (i / hold) * increment;
        if (cycle)
            drawing = ((drawing % drawings) + drawings) % drawings;
        // Past the last drawing (or before the first): the last one that was in range is held.
        cells.push_back(std::clamp(drawing, 0, drawings - 1));
    }
    m_document->setCells(l, first, cells, tr("Sequence Fill"));
    return count;
}

void MainWindow::onActionFillSelection()
{
    if (fillSelection() == 0)
        statusBar()->showMessage(tr("The current cell shows no drawing to fill the selection with."), 5000);
}

void MainWindow::onActionSequenceFill()
{
    Layer const& now = m_document->scene().layers[layer()];
    int const drawings = now.holdsDrawings() ? int(now.drawings.size()) : 0;
    if (drawings == 0) {
        statusBar()->showMessage(tr("This layer has no drawings to fill with."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Sequence Fill"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const start = new QSpinBox(&dialog);
    start->setRange(1, drawings);
    start->setValue(std::max(1, now.drawingAt(frame()) + 1));
    QSpinBox* const increment = new QSpinBox(&dialog);
    increment->setRange(-drawings, drawings);
    increment->setValue(1);
    QSpinBox* const hold = new QSpinBox(&dialog);
    hold->setRange(1, 999);
    QCheckBox* const cycle = new QCheckBox(tr("Start again when the drawings run out"), &dialog);
    cycle->setChecked(true);
    form->addRow(tr("Starting drawing"), start);
    form->addRow(tr("Increment"), increment);
    form->addRow(tr("Hold (frames each)"), hold);
    form->addRow(cycle);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        sequenceFill(start->value() - 1, increment->value(), hold->value(), cycle->isChecked());
}

void MainWindow::onActionHoldExposure()
{
    bool ok = false;
    int const length = QInputDialog::getInt(this, tr("Hold Exposure"), tr("Frames the drawing is held for:"), 2, 1, 999, 1, &ok);
    if (ok)
        onActionSetExposure(length);
}

void MainWindow::onActionUngroupLayers()
{
    // The groups among the layers selected; or, with none, the group the current layer is in.
    Scene const& scene = m_document->scene();
    QVector<int> groups;
    for (int l : m_timeline->selectedLayers())
        if (scene.layers[l].kind == Layer::Group)
            groups.push_back(l);
    if (groups.isEmpty()) {
        int const up = scene.parentIndex(layer());
        if (up >= 0 && scene.layers[up].kind == Layer::Group)
            groups.push_back(up);
    }
    if (groups.isEmpty()) {
        statusBar()->showMessage(tr("Select a group, or a layer that is in one, to ungroup."), 5000);
        return;
    }
    // What was in a group goes to what the group was in; the group itself is gone.
    m_timeline->clearRange();
    m_document->removeLayers(groups);
}

void MainWindow::onActionMoveToParentGroup()
{
    Scene const& scene = m_document->scene();
    int const group = scene.parentIndex(layer());
    if (group < 0 || scene.layers[group].kind != Layer::Group) {
        statusBar()->showMessage(tr("The layer is not in a group."), 5000);
        return;
    }
    // Out of its group, into what that group is in (or into nothing).
    m_document->setParent(layer(), scene.parentIndex(group));
}
