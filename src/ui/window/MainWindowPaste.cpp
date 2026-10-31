// Eclipse - the main window: Paste Special (what of the cells and keyframes is pasted, and how the drawings come) and the paste presets.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"
#include "model/scene/Keyframe.h"
#include "canvas/Canvas.h"

void MainWindow::onActionPasteInPlace()
{
    Canvas* target = m_canvas;
    if (Canvas* const drawing = drawingWindowView(); drawing && drawing->isVisible()) {
        QWidget* const focus = QApplication::focusWidget();
        if ((focus && (focus == drawing || drawing->isAncestorOf(focus))) || QApplication::activeWindow() == drawing->window())
            target = drawing;
    }
    target->pasteInPlace();
}

bool MainWindow::pasteDialog(Timeline::PasteOptions& options, QString const& title)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PasteSpecialDialog"));
    dialog.setWindowTitle(title);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    QTabWidget* const tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);

    // Basic: what is pasted, how often, which way, and what becomes of the drawings.
    QWidget* const basic = new QWidget(tabs);
    QFormLayout* const form = new QFormLayout(basic);
    QCheckBox* const drawings = new QCheckBox(tr("Drawings (the exposure)"), basic);
    drawings->setObjectName(QStringLiteral("PasteDrawings"));
    drawings->setChecked(options.drawings);
    QCheckBox* const keys = new QCheckBox(tr("Keyframes"), basic);
    keys->setObjectName(QStringLiteral("PasteKeyframes"));
    keys->setChecked(options.keyframes);
    QSpinBox* const cycles = new QSpinBox(basic);
    cycles->setObjectName(QStringLiteral("PasteCycles"));
    cycles->setRange(1, 1000);
    cycles->setValue(options.cycles);
    QCheckBox* const reverse = new QCheckBox(tr("From the last frame to the first"), basic);
    reverse->setObjectName(QStringLiteral("PasteReverse"));
    reverse->setToolTip(tr("Determine in which order the source is to be pasted."));
    reverse->setChecked(options.reverse);
    QComboBox* const mode = new QComboBox(basic);
    mode->setObjectName(QStringLiteral("PasteDrawingMode"));
    mode->addItem(tr("Copies of the drawings (as Paste does)"), int(CellBlock::CopyDrawings));
    mode->addItem(tr("Timing only: do not create any new drawings"), int(CellBlock::TimingOnly));
    mode->addItem(tr("Create missing drawings, do not duplicate existing ones"), int(CellBlock::CreateMissing));
    mode->addItem(tr("Create missing drawings and overwrite existing ones"), int(CellBlock::CreateOverwrite));
    mode->addItem(tr("Create drawings, renaming them with an underscore when the name is taken"), int(CellBlock::CreateRenamed));
    mode->setToolTip(tr("The drawings met by name in the layer pasted onto: used as they are, made where missing, overwritten, or made anew under names of their own."));
    mode->setCurrentIndex(std::max(0, mode->findData(options.drawingMode)));
    QCheckBox* const drawingPivot = new QCheckBox(tr("Update Drawing Pivot"), basic);
    drawingPivot->setObjectName(QStringLiteral("PasteDrawingPivot"));
    drawingPivot->setToolTip(tr("A drawing already there under the same name, kept rather than replaced, takes the pivot of the drawing copied."));
    drawingPivot->setChecked(options.updateDrawingPivot);
    form->addRow(drawings);
    form->addRow(tr("Drawings"), mode);
    form->addRow(drawingPivot);
    form->addRow(keys);
    form->addRow(tr("Number of cycles"), cycles);
    form->addRow(reverse);
    tabs->addTab(basic, tr("Basic"));

    // Advanced: the parts of the keyframes, the pivot, layers met by name.
    QWidget* const advanced = new QWidget(tabs);
    QFormLayout* const more = new QFormLayout(advanced);
    struct Part {
        char const* name;
        QString text;
        QString tip;
        int parts;
    };
    QVector<Part> const parts = {{"PastePosition", tr("Position"), tr("Paste the X, Y and Z values."), Keyframe::PartX | Keyframe::PartY | Keyframe::PartZ},
                                 {"PasteRotation", tr("Rotation"), tr("Paste the Rotation values."), Keyframe::PartAngle},
                                 {"PasteScale", tr("Scale"), tr("Paste the Scale values."), Keyframe::PartScaleX | Keyframe::PartScaleY},
                                 {"PasteSkew", tr("Skew"), tr("Paste the Skew values."), Keyframe::PartSkew}};
    QVector<QCheckBox*> boxes;
    for (Part const& part : parts) {
        QCheckBox* const box = new QCheckBox(part.text, advanced);
        box->setObjectName(QLatin1String(part.name));
        box->setToolTip(part.tip);
        box->setChecked((options.parts & part.parts) == part.parts);
        more->addRow(box);
        boxes << box;
    }
    QCheckBox* const pivot = new QCheckBox(tr("Pivot"), advanced);
    pivot->setObjectName(QStringLiteral("PastePivot"));
    pivot->setToolTip(tr("Paste the pivot of the layers copied onto the layers pasted onto."));
    pivot->setChecked(options.pivot);
    more->addRow(pivot);
    QCheckBox* const names = new QCheckBox(tr("Match layers by name"), advanced);
    names->setObjectName(QStringLiteral("PasteMatchNames"));
    names->setToolTip(tr("When pasting the values of several layers (a character's rig) onto the same layers in another order: each onto the layer of its name."));
    names->setChecked(options.matchNames);
    more->addRow(names);
    QCheckBox* const offset = new QCheckBox(tr("Offset Keyframes"), advanced);
    offset->setObjectName(QStringLiteral("PasteOffsetKeys"));
    offset->setToolTip(tr("Offsets the target keyframe values by the amount of the source keyframe values."));
    offset->setChecked(options.offsetKeys);
    more->addRow(offset);
    QCheckBox* const synced = new QCheckBox(tr("Paste Only Drawings from the Current Synced Layer"), advanced);
    synced->setObjectName(QStringLiteral("PasteCurrentSet"));
    synced->setToolTip(tr("When pasting from a source synced layer onto a target synced layer do not paste the drawings from the other source synced layers."));
    synced->setChecked(options.currentSetOnly);
    more->addRow(synced);
    QCheckBox* const gap = new QCheckBox(tr("Fill Gap with Previous Drawing"), advanced);
    gap->setObjectName(QStringLiteral("PasteFillGap"));
    gap->setToolTip(tr("Applies when dragging with the Alt key. Fill the gaps left on the source when dragging out frames."));
    gap->setChecked(options.fillGap);
    more->addRow(gap);
    QCheckBox* const toKey = new QCheckBox(tr("Fill to Next Key Exposure"), advanced);
    toKey->setObjectName(QStringLiteral("PasteFillToKey"));
    toKey->setToolTip(tr("When used with Alt dragging, extend the exposure of the drawing of the previous frame up to the next key exposure or empty frame. "
                         "When used with paste, extend the exposure of the dropped drawing up to the next key exposure or empty frame."));
    toKey->setChecked(options.fillToKey);
    more->addRow(toKey);
    QCheckBox* const edges = new QCheckBox(tr("Force Keyframes at Function's Beginning and End"), advanced);
    edges->setObjectName(QStringLiteral("PasteEdgeKeys"));
    edges->setToolTip(tr("The layers pasted onto get keyframes where the paste begins and ends, so that their motion outside it stays as it was."));
    edges->setChecked(options.edgeKeys);
    more->addRow(edges);
    tabs->addTab(advanced, tr("Advanced"));

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    options.drawings = drawings->isChecked();
    options.keyframes = keys->isChecked();
    options.cycles = cycles->value();
    options.reverse = reverse->isChecked();
    options.drawingMode = mode->currentData().toInt();
    options.parts = Keyframe::AllParts;
    for (int i = 0; i < parts.size(); ++i)
        if (!boxes[i]->isChecked())
            options.parts &= ~parts[i].parts;
    options.pivot = pivot->isChecked();
    options.matchNames = names->isChecked();
    options.offsetKeys = offset->isChecked();
    options.currentSetOnly = synced->isChecked();
    options.updateDrawingPivot = drawingPivot->isChecked();
    options.fillGap = gap->isChecked();
    options.fillToKey = toKey->isChecked();
    options.edgeKeys = edges->isChecked();
    m_timeline->setDragFill(options.fillGap, options.fillToKey);
    return true;
}

void MainWindow::onActionPasteSpecial()
{
    if (!Timeline::canPasteCells()) {
        statusBar()->showMessage(tr("Copy cells in the Timeline or the Xsheet first."), 5000);
        return;
    }
    if (pasteDialog(m_paste, tr("Paste Special")))
        m_timeline->pasteSpecial(m_paste);
}

// The same again, as Paste Special was last set, without asking.
void MainWindow::onActionPasteSpecialAgain()
{
    if (!Timeline::canPasteCells()) {
        statusBar()->showMessage(tr("Copy cells in the Timeline or the Xsheet first."), 5000);
        return;
    }
    m_timeline->pasteSpecial(m_paste);
}

void MainWindow::onActionEditExposurePreset()
{
    onActionEditPastePreset(QStringLiteral("exposure"));
}

void MainWindow::onActionEditPastePreset(QString const& which)
{
    // Edit > Modify Paste Presets: how Paste pastes in each paste mode.
    if (which == QLatin1String("all")) {
        if (pasteDialog(m_pasteAll, tr("Edit All Paste Preset"))) {
            m_pasteAllEdited = true;
            setPasteMode(m_pasteMode);
        }
        return;
    }
    if (which == QLatin1String("keys")) {
        if (m_pasteKeys.drawings) {
            m_pasteKeys.drawings = false;
            m_pasteKeys.keyframes = true;
        }
        if (pasteDialog(m_pasteKeys, tr("Edit Key Frame Paste Preset")))
            setPasteMode(m_pasteMode);
        return;
    }
    if (m_pasteExposure.keyframes && m_pasteExposure.drawingMode == -1) {
        m_pasteExposure.keyframes = false;
        m_pasteExposure.drawingMode = CellBlock::CreateMissing;
    }
    if (pasteDialog(m_pasteExposure, tr("Edit Exposure Paste Preset")))
        setPasteMode(m_pasteMode);
}

void MainWindow::setPasteMode(int mode)
{
    // All: as Paste always pastes (or as its preset says, once edited); Exposures Only: no keyframes; Key Frames Only: no drawings.
    m_pasteMode = std::clamp(mode, 0, 2);
    if (m_pasteMode == 1 && m_pasteExposure.keyframes && m_pasteExposure.drawingMode == -1) {
        m_pasteExposure.keyframes = false;
        m_pasteExposure.drawingMode = CellBlock::CreateMissing;
    }
    if (m_pasteMode == 2 && m_pasteKeys.drawings) {
        m_pasteKeys.drawings = false;
        m_pasteKeys.keyframes = true;
    }
    m_timeline->setPastePreset(m_pasteMode == 1 ? &m_pasteExposure : m_pasteMode == 2 ? &m_pasteKeys : m_pasteAllEdited ? &m_pasteAll : nullptr);
    m_actions->setChecked(QStringLiteral("PasteModeAll"), m_pasteMode == 0);
    m_actions->setChecked(QStringLiteral("PasteModeExposure"), m_pasteMode == 1);
    m_actions->setChecked(QStringLiteral("PasteModeKeys"), m_pasteMode == 2);
}

void MainWindow::onActionPasteModeExposure(bool on)
{
    setPasteMode(on ? 1 : 0);
    statusBar()->showMessage(on ? tr("Paste Mode: Exposures Only") : tr("Paste Mode: Normal"), 3000);
}

void MainWindow::onActionPasteMode(QString const& mode)
{
    int const which = mode == QLatin1String("exposure") ? 1 : mode == QLatin1String("keys") ? 2 : 0;
    setPasteMode(which);
    statusBar()->showMessage(which == 1 ? tr("Paste Mode: Exposures Only") : which == 2 ? tr("Paste Mode: Key Frames Only") : tr("Paste Mode: All"), 3000);
}

void MainWindow::onActionPasteModeCycle()
{
    onActionPasteMode(QLatin1String(m_pasteMode == 0 ? "exposure" : m_pasteMode == 1 ? "keys" : "all"));
}
