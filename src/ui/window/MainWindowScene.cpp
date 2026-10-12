// Eclipse - the main window: the Scene menu - frames, length, settings; bringing in a sound.
#include "ui/window/MainWindow.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

void MainWindow::onActionImportSound()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import Sound"), QFileInfo(m_path).path(),
                                                      tr("Sound Files (*.wav *.mp3 *.aif *.aiff *.ogg *.flac *.m4a *.aac *.mp4 *.mov);;All Files (*)"));
    if (path.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    std::shared_ptr<SoundData> const sound = Sound::load(path, &error);
    QApplication::restoreOverrideCursor();
    if (!sound) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not read %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    int const at = m_document->addSound(path, frame());
    m_timeline->setCurrentCell(at, frame());
    // Say so when the sound runs past the end: the scene is not made longer behind the user's back.
    double const needed = frame() + sound->seconds() * m_document->scene().fps;
    if (needed > m_document->scene().frameCount())
        statusBar()->showMessage(tr("The sound is %1 frames long and runs past the end of the scene. Use Scene > Scene Length to make room.")
                                     .arg(int(std::ceil(sound->seconds() * m_document->scene().fps))), 10000);
}

void MainWindow::onActionMoveSoundHere()
{
    m_document->setSoundStart(layer(), frame());
}

void MainWindow::addFrames(int at)
{
    bool ok = false;
    int const count = QInputDialog::getInt(this, tr("Add Frames"), tr("Number of frames to add:"), 1, 1, 100000, 1, &ok);
    if (ok)
        m_document->insertFrames(at, count);
}

void MainWindow::onActionAddFramesAtStart()
{
    addFrames(0);
}

void MainWindow::onActionAddFramesAtEnd()
{
    addFrames(m_document->scene().frameCount());
}

void MainWindow::onActionAddFramesBeforeSelection()
{
    addFrames(m_timeline->selectionFirstFrame());
}

void MainWindow::onActionAddFramesAfterSelection()
{
    addFrames(m_timeline->selectionLastFrame() + 1);
}

void MainWindow::onActionRemoveSelectedFrames()
{
    int const first = m_timeline->selectionFirstFrame();
    m_document->removeFrames(first, m_timeline->selectionLastFrame());
    m_timeline->clearRange();
    m_timeline->setCurrentFrame(first);
}

void MainWindow::onActionSceneLength()
{
    bool ok = false;
    int const count = QInputDialog::getInt(this, tr("Scene Length"), tr("Number of frames:"), m_document->scene().frameCount(), 1,
                                           100000, 1, &ok);
    if (ok)
        m_document->setFrameCount(count);
}

void MainWindow::onActionSceneSettings()
{
    Scene const& scene = m_document->scene();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Scene Settings"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const width = new QSpinBox(&dialog);
    width->setRange(16, 16384);
    width->setValue(scene.width);
    QSpinBox* const height = new QSpinBox(&dialog);
    height->setRange(16, 16384);
    height->setValue(scene.height);
    QDoubleSpinBox* const fps = new QDoubleSpinBox(&dialog);
    fps->setRange(1.0, 240.0);
    fps->setDecimals(3);
    fps->setValue(scene.fps);
    form->addRow(tr("Width"), width);
    form->addRow(tr("Height"), height);
    form->addRow(tr("Frames per second"), fps);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        m_document->setSceneSettings(width->value(), height->value(), fps->value());
        m_canvas->fitToWindow();
    }
}
