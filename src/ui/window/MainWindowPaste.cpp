// Eclipse - the main window: Paste Special (what of the cells and keyframes is pasted, and how the drawings come) and the paste presets.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
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
    form->addRow(drawings);
    form->addRow(tr("Drawings"), mode);
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
    // Edit > Modify Paste Presets > Exposure: how Paste pastes in the Exposures Only mode.
    if (m_pasteExposure.keyframes && m_pasteExposure.drawingMode == -1) {
        m_pasteExposure.keyframes = false;
        m_pasteExposure.drawingMode = CellBlock::CreateMissing;
    }
    if (pasteDialog(m_pasteExposure, tr("Edit Exposure Paste Preset")) && m_pasteExposuresOnly)
        m_timeline->setPastePreset(&m_pasteExposure);
}

void MainWindow::onActionPasteModeExposure(bool on)
{
    // Paste Mode: Exposures Only - the plain Paste pastes as the Exposure preset says (no keyframes, by default).
    if (on && m_pasteExposure.keyframes && m_pasteExposure.drawingMode == -1) {
        m_pasteExposure.keyframes = false;
        m_pasteExposure.drawingMode = CellBlock::CreateMissing;
    }
    m_pasteExposuresOnly = on;
    m_timeline->setPastePreset(on ? &m_pasteExposure : nullptr);
    m_actions->setChecked(QStringLiteral("PasteModeExposure"), on);
    statusBar()->showMessage(on ? tr("Paste Mode: Exposures Only") : tr("Paste Mode: Normal"), 3000);
}

void MainWindow::onActionPasteModeCycle()
{
    onActionPasteModeExposure(!m_pasteExposuresOnly);
}
