// Eclipse - the main window: the commands on cells - exposure, pasting, scene markers, drawings.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QCheckBox>
#include <QComboBox>
#include "ui/panels/ColourEditor.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/LipSyncView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

void MainWindow::onExposure(QString const& text, std::function<void(int layer, int frame)> const& command)
{
    QVector<int> const layers = exposureLayers();
    int const at = m_timeline->selectionFirstFrame();
    if (layers.size() > 1)
        m_document->beginMacro(text);
    for (int l : layers)
        command(l, at);
    if (layers.size() > 1)
        m_document->endMacro();
}

void MainWindow::onActionIncreaseExposure()
{
    onExposure(tr("Increase Exposure"), [this](int l, int f) { m_document->increaseExposure(l, f); });
}

void MainWindow::onActionAddKeyExposure()
{
    onExposure(tr("Add Key Exposure"), [this](int l, int f) { m_document->addKeyExposure(l, f); });
}

void MainWindow::onActionRemoveKeyExposure()
{
    onExposure(tr("Remove Key Exposure"), [this](int l, int f) { m_document->removeKeyExposure(l, f); });
}

void MainWindow::onActionRemoveDuplicateKeyExposure()
{
    // Over the frames selected - or, with one cell only, over the whole of the layer.
    bool const range = m_timeline->selectionLastFrame() > m_timeline->selectionFirstFrame();
    int const first = range ? m_timeline->selectionFirstFrame() : 0, last = range ? m_timeline->selectionLastFrame() : m_document->scene().frameCount() - 1;
    onExposure(tr("Remove Duplicate Key Exposure"), [this, first, last](int l, int) { m_document->removeDuplicateKeyExposures(l, first, last); });
}

void MainWindow::onActionDecreaseExposure()
{
    onExposure(tr("Decrease Exposure"), [this](int l, int f) { m_document->decreaseExposure(l, f); });
}

void MainWindow::onActionSetExposure(int length)
{
    onExposure(tr("Set Exposure"), [this, length](int l, int f) { m_document->setExposure(l, f, length); });
}

void MainWindow::onActionInsertBlankCell()
{
    onExposure(tr("Insert Blank Cell"), [this](int l, int f) { m_document->insertBlankCell(l, f); });
}

void MainWindow::onActionClearExposureAndPull()
{
    int const count = m_timeline->selectionLastFrame() - m_timeline->selectionFirstFrame() + 1;
    onExposure(tr("Clear Exposure and Pull"), [this, count](int l, int f) { m_document->clearExposureAndPull(l, f, count); });
}

void MainWindow::onActionFillEmptyCells()
{
    onExposure(tr("Fill Empty Cells"), [this](int l, int) { m_document->fillEmptyCells(l); });
}

void MainWindow::onActionPasteCycle()
{
    int times = 2, kind = 0;
    if (cycleDialog(tr("Paste Cycle"), tr("Number of cycles:"), 1, &times, &kind))
        m_timeline->pasteCycle(times, kind);
}

void MainWindow::onActionPasteReverse()
{
    m_timeline->pasteReversed();
}

void MainWindow::onActionCreateCycle()
{
    int times = 2, kind = 0;
    if (cycleDialog(tr("Create Cycle"), tr("Number of cycles (with the one selected):"), 2, &times, &kind))
        m_timeline->createCycle(times, kind);
}

// What there is to remember about the marker on the current frame.
void MainWindow::onActionSceneMarkerNote()
{
    Scene const& scene = m_document->scene();
    int const index = scene.markerAt(frame());
    if (index < 0) {
        statusBar()->showMessage(tr("There is no scene marker on this frame."), 4000);
        return;
    }
    bool ok = false;
    QString const note = QInputDialog::getMultiLineText(this, tr("Scene Marker Note"), tr("Note for \"%1\":").arg(scene.markers[index].name), scene.markers[index].note, &ok);
    if (ok)
        m_document->setMarkerNote(index, note);
}

void MainWindow::onActionDeleteSceneMarker()
{
    m_document->removeMarker(m_document->scene().markerAt(frame()));
}

void MainWindow::onActionDeleteDrawing()
{
    // Of every layer selected, the drawing shown on the first frame selected.
    QVector<int> const layers = m_timeline->selectedLayers();
    int const at = m_timeline->selectionFirstFrame();
    if (layers.size() > 1)
        m_document->beginMacro(tr("Delete Drawings"));
    for (int l : layers)
        m_document->deleteDrawing(l, at);
    if (layers.size() > 1)
        m_document->endMacro();
}

void MainWindow::onActionCreateEmptyDrawing()
{
    m_document->newDrawing(layer(), frame());
}

void MainWindow::onActionDuplicateDrawing()
{
    m_document->duplicateDrawing(layer(), frame());
}

void MainWindow::onActionExtendExposure()
{
    int const first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    bool occupied = false;
    for (int l : exposureLayers())
        occupied = occupied || m_document->scene().layers[l].drawingAt(first) != Layer::kNoDrawing;
    if (first == last && occupied) {
        onActionExtendExposureTo();
        return;
    }
    onExposure(tr("Extend Exposure"), [this, last](int l, int firstFrame) { m_document->extendExposureTo(l, firstFrame, last); });
}

void MainWindow::onActionClearExposure()
{
    int const count = m_timeline->selectionLastFrame() - m_timeline->selectionFirstFrame() + 1;
    onExposure(tr("Clear Exposure"), [this, count](int l, int first) { m_document->setCells(l, first, QVector<int>(count, Layer::kNoDrawing), tr("Clear Exposure")); });
}

bool MainWindow::cycleDialog(QString const& title, QString const& label, int least, int* times, int* kind)
{
    // How many times round, and how: the same again, on from where it ended (a walk), or there and back.
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CycleDialog"));
    dialog.setWindowTitle(title);
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const count = new QSpinBox(&dialog);
    count->setObjectName(QStringLiteral("CycleCount"));
    count->setRange(least, 1000);
    count->setValue(std::max(least, *times));
    QComboBox* const type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("CycleType"));
    type->addItems({tr("Repeat"), tr("Repeat and Offset"), tr("Ping Pong")});
    type->setToolTip(tr("Repeat: the same again. Repeat and Offset: each time the keyframes go on from where the last ended (a walk). Ping Pong: there and back again."));
    type->setCurrentIndex(std::clamp(*kind, 0, 2));
    form->addRow(label, count);
    form->addRow(tr("Cycle Type"), type);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *times = count->value();
    *kind = type->currentIndex();
    return true;
}

void MainWindow::onActionSubstituteFrame(QString const& direction)
{
    m_document->stepFrameSubstitution(layer(), frame(), direction.toInt() < 0 ? -1 : 1);
}

void MainWindow::onActionGoToDrawing(QString const& which)
{
    // The nth drawing the layer shows, by the order they come along the layer: its first frame.
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size())
        return;
    Layer const& l = scene.layers[layer()];
    QVector<int> met;
    for (int f = 0; f < l.cells.size(); ++f) {
        int const d = l.cells[f];
        if (d == Layer::kNoDrawing || met.contains(d))
            continue;
        met.push_back(d);
        if (met.size() == which.toInt()) {
            m_timeline->setCurrentCell(layer(), f);
            return;
        }
    }
    statusBar()->showMessage(tr("The layer shows fewer than %1 drawings.").arg(which), 3000);
}

void MainWindow::onActionInsertInbetween()
{
    if (m_document->insertInbetweenDrawing(layer(), frame()) < 0)
        statusBar()->showMessage(tr("Insert Action Inbetween Drawing: choose a drawing held two frames or more."), 4000);
}
