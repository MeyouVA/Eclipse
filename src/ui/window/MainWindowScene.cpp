// Eclipse - the main window: the Scene menu - frames, length, settings; bringing in a sound.
#include <QLabel>
#include "ui/window/MainWindow.h"
#include "ui/panels/ColourSpaceBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QSpinBox>
#include <QSignalBlocker>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
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
    QDoubleSpinBox* const fov = new QDoubleSpinBox(&dialog);
    fov->setObjectName(QStringLiteral("SceneFov"));
    fov->setDecimals(3);
    fov->setRange(std::min(m_document->minimumFov(), scene.fov), std::max(m_document->maximumFov(), scene.fov));
    fov->setSuffix(tr(" deg"));
    fov->setToolTip(tr("The angle of view of the camera size: cameras that have not been given another angle take it."));
    fov->setValue(scene.fov);
    bool fovEdited = false;
    connect(fov, &QDoubleSpinBox::valueChanged, &dialog, [this, fov, &fovEdited, original = scene.fov](double value) {
        fovEdited = true;
        if (value == original)
            return;
        double const limited = std::clamp(value, m_document->minimumFov(), m_document->maximumFov());
        if (limited != value) {
            QSignalBlocker const blocker(fov);
            fov->setValue(limited);
        }
    });
    // The sizes there are to choose from: one picked fills in the width, the height and the rate.
    QComboBox* const presets = new QComboBox(&dialog);
    presets->setObjectName(QStringLiteral("ScenePresets"));
    presets->addItem(tr("(as typed below)"));
    QVector<Resolution> const known = WelcomeDialog::resolutions(ActionManager::defaultDirectory());
    for (Resolution const& r : known) {
        presets->addItem(tr("%1   (%2 x %3, %4 fps)").arg(r.name).arg(r.width).arg(r.height).arg(r.fps));
        if (r.width == scene.width && r.height == scene.height && r.fps == scene.fps && std::abs(r.fov - scene.fov) < 1e-6 && presets->currentIndex() == 0)
            presets->setCurrentIndex(presets->count() - 1);
    }
    connect(presets, &QComboBox::activated, &dialog, [known, width, height, fps, fov](int index) {
        if (index < 1 || index > known.size())
            return;
        width->setValue(known[index - 1].width);
        height->setValue(known[index - 1].height);
        fps->setValue(known[index - 1].fps);
        fov->setValue(known[index - 1].fov);
    });
    QLabel* const shape = new QLabel(&dialog);
    shape->setObjectName(QStringLiteral("SceneAspect"));
    auto showShape = [shape, width, height] { shape->setText(QString::number(double(width->value()) / std::max(1, height->value()), 'f', 3)); };
    connect(width, &QSpinBox::valueChanged, shape, showShape);
    connect(height, &QSpinBox::valueChanged, shape, showShape);
    showShape();
    form->addRow(tr("Resolution"), presets);
    form->addRow(tr("Width"), width);
    form->addRow(tr("Height"), height);
    form->addRow(tr("Aspect ratio"), shape);
    form->addRow(tr("Frames per second"), fps);
    form->addRow(tr("Field of view"), fov);
    QComboBox* const projection = new QComboBox(&dialog);
    projection->setObjectName(QStringLiteral("SceneProjection"));
    projection->addItems({tr("Perspective"), tr("Orthographic")});
    projection->setToolTip(tr("Perspective: nearer layers look bigger, as a camera sees.  Orthographic: depth only puts layers in front of and behind one another."));
    projection->setCurrentIndex(scene.orthographic ? 1 : 0);
    form->addRow(tr("Projection"), projection);
    QComboBox* const fit = new QComboBox(&dialog);
    fit->setObjectName(QStringLiteral("SceneFit"));
    fit->addItems({tr("Vertical Fit"), tr("Horizontal Fit")});
    fit->setToolTip(tr("Whether the field of view is measured from the top of the frame to the bottom, or from side to side."));
    fit->setCurrentIndex(scene.fovAcross ? 1 : 0);
    form->addRow(tr("Field of view fit"), fit);
    connect(presets, &QComboBox::activated, &dialog, [known, fit, projection](int index) {
        if (index <= 0 || index - 1 >= known.size())
            return;
        fit->setCurrentIndex(known[index - 1].across ? 1 : 0);
        if (known[index - 1].orthographic)
            projection->setCurrentIndex(1);
    });
    // Colour: the colour space layers are put together in, and how palettes and texture swatches are read.
    QGroupBox* const colour = new QGroupBox(tr("Colour Space Management"), &dialog);
    colour->setObjectName(QStringLiteral("SceneColourManagement"));
    colour->setCheckable(true);
    colour->setChecked(scene.colourManaged);
    colour->setToolTip(tr("Off: colours are used as they are (No Colour Space Management)."));
    QFormLayout* const colourForm = new QFormLayout(colour);
    QComboBox* const working = ColourSpaceBox::make(colour, QStringLiteral("SceneWorkingSpace"), scene.workingSpace);
    working->setToolTip(tr("Defines the colour space used for compositing scene elements and effects together."));
    colourForm->addRow(tr("Working Colour Space"), working);
    QComboBox* const textures = ColourSpaceBox::make(colour, QStringLiteral("SceneTextureSpace"), scene.textureSpace);
    textures->setToolTip(tr("This colour space is used to read texture swatches from palettes before converting them to the scene working colour space."));
    colourForm->addRow(tr("Texture Swatches Colour Space"), textures);
    QCheckBox* const srgbPalettes = new QCheckBox(tr("Use Palettes Definition in sRGB Colour Space"), colour);
    srgbPalettes->setObjectName(QStringLiteral("ScenePalettesInSrgb"));
    srgbPalettes->setChecked(scene.palettesInSrgb);
    srgbPalettes->setToolTip(tr("When enabled, bitmap drawings and colour swatches in palettes are read using the sRGB colour space before being converted to the working colour space."));
    colourForm->addRow(srgbPalettes);
    form->addRow(colour);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        m_document->beginMacro(tr("Scene Settings"));
        m_document->setSceneSettings(width->value(), height->value(), fps->value(), fovEdited ? fov->value() : scene.fov);
        m_document->setOrthographic(projection->currentIndex() == 1);
        m_document->setFovAcross(fit->currentIndex() == 1);
        m_document->setColourManagement(colour->isChecked(), ColourSpaceBox::chosen(working), ColourSpaceBox::chosen(textures), srgbPalettes->isChecked());
        m_document->endMacro();
        m_canvas->fitToWindow();
    }
}
