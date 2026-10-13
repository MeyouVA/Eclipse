// Eclipse - the main window: the commands on cells - exposure, pasting, scene markers, drawings.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
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
    QVector<int> const layers = m_timeline->selectedLayers();
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
    onExposure(tr("Clear Exposure and Pull"), [this](int l, int f) { m_document->clearExposureAndPull(l, f); });
}

void MainWindow::onActionFillEmptyCells()
{
    onExposure(tr("Fill Empty Cells"), [this](int l, int) { m_document->fillEmptyCells(l); });
}

void MainWindow::onActionPasteCycle()
{
    bool ok = false;
    int const times = QInputDialog::getInt(this, tr("Paste Cycle"), tr("Number of cycles:"), 2, 1, 1000, 1, &ok);
    if (ok)
        m_timeline->pasteCycle(times);
}

void MainWindow::onActionPasteSpecial()
{
    if (!Timeline::canPasteCells()) {
        statusBar()->showMessage(tr("Copy cells in the Timeline or the Xsheet first."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Paste Special"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const drawings = new QCheckBox(tr("Drawings (the exposure)"), &dialog);
    drawings->setObjectName(QStringLiteral("PasteDrawings"));
    drawings->setChecked(m_pasteDrawings);
    QCheckBox* const keys = new QCheckBox(tr("Keyframes"), &dialog);
    keys->setObjectName(QStringLiteral("PasteKeyframes"));
    keys->setChecked(m_pasteKeys);
    QSpinBox* const cycles = new QSpinBox(&dialog);
    cycles->setRange(1, 1000);
    cycles->setValue(m_pasteCycles);
    QCheckBox* const reverse = new QCheckBox(tr("From the last frame to the first"), &dialog);
    reverse->setChecked(m_pasteReverse);
    form->addRow(drawings);
    form->addRow(keys);
    form->addRow(tr("Number of cycles"), cycles);
    form->addRow(reverse);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_pasteDrawings = drawings->isChecked();
    m_pasteKeys = keys->isChecked();
    m_pasteCycles = cycles->value();
    m_pasteReverse = reverse->isChecked();
    m_timeline->pasteSpecial(m_pasteDrawings, m_pasteKeys, m_pasteCycles, m_pasteReverse);
}

// The same again, as Paste Special was last set, without asking.
void MainWindow::onActionPasteSpecialAgain()
{
    if (!Timeline::canPasteCells()) {
        statusBar()->showMessage(tr("Copy cells in the Timeline or the Xsheet first."), 5000);
        return;
    }
    m_timeline->pasteSpecial(m_pasteDrawings, m_pasteKeys, m_pasteCycles, m_pasteReverse);
}

void MainWindow::onActionPasteReverse()
{
    m_timeline->pasteReversed();
}

void MainWindow::onActionCreateCycle()
{
    bool ok = false;
    int const times = QInputDialog::getInt(this, tr("Create Cycle"), tr("Number of cycles (with the one selected):"), 2, 2, 1000, 1, &ok);
    if (ok)
        m_timeline->createCycle(times);
}

void MainWindow::onActionCreateSceneMarker()
{
    // Over the frames selected in the Timeline.
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Create Scene Marker"), tr("Name:"), QLineEdit::Normal, QString(), &ok);
    if (ok)
        m_document->addMarker(m_timeline->selectionFirstFrame(), m_timeline->selectionLastFrame() - m_timeline->selectionFirstFrame() + 1, name, QColor());
}

void MainWindow::onActionEditSceneMarker()
{
    Scene const& scene = m_document->scene();
    int const index = scene.markerAt(frame());
    if (index < 0)
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Edit Scene Marker"), tr("Name:"), QLineEdit::Normal, scene.markers[index].name, &ok);
    if (!ok)
        return;
    QColor const colour = QColorDialog::getColor(scene.markers[index].color, this, tr("Marker Colour"));
    m_document->editMarker(index, name, colour);
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
    // Hold the previous drawing over this frame and move on, so repeated presses keep extending.
    m_document->extendExposure(layer(), frame());
    stepFrame(+1);
}

void MainWindow::onActionClearExposure()
{
    m_document->clearCell(layer(), frame());
}
