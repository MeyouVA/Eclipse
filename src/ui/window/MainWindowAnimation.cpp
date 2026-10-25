// Eclipse - the main window: the commands of the Animation menu - keyframes, ease, substitution, selecting parent and child.
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include "ui/window/MainWindow.h"

#include <QDockWidget>
#include <QPlainTextEdit>

#include "actions/manager/ActionManager.h"
#include <QInputDialog>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/timeline/FunctionView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

void MainWindow::onActionInsertKeyframe()
{
    m_document->insertKeyframe(layer(), frame());
}

// A point of the motion path where the layer is on this frame: it can be pulled about to bend the path.
void MainWindow::onActionInsertControlPoint()
{
    if (m_document->insertControlPoint(layer(), frame()) < 0) {
        if (layer() >= 0 && layer() < m_document->scene().layers.size() && m_document->scene().layers[layer()].separatePosition) {
            statusBar()->showMessage(tr("This layer's position is Separate: it has no path to bend. Turn Separate Position off in the Layer Properties."), 8000);
            return;
        }
        statusBar()->showMessage(tr("A control point goes on a frame between two keyframes that the layer glides between."), 6000);
        return;
    }
    m_actions->setChecked(QStringLiteral("ShowControl"), true);
    m_canvas->onActionShowControl(true);
    statusBar()->showMessage(tr("Control point added. With the Transform tool, drag it to bend the path; Animation > Lock in Time makes it a keyframe."), 8000);
}

// A keyframe on this frame, and a drawing of its own for it: a new pose to draw, in one step.
void MainWindow::onActionKeyframeAndDuplicate()
{
    m_document->beginMacro(tr("Keyframe and Duplicate Drawing"));
    m_document->insertKeyframe(layer(), frame());
    m_document->duplicateDrawing(layer(), frame());
    m_document->endMacro();
}

void MainWindow::onActionDeleteKeyframe()
{
    m_document->removeKeyframe(layer(), frame());
}

void MainWindow::onActionSetMotionKeyframe()
{
    m_document->setKeyTween(layer(), frame(), true);
}

void MainWindow::onActionSetStopMotionKeyframe()
{
    m_document->setKeyTween(layer(), frame(), false);
}

void MainWindow::onActionResetTransformation()
{
    m_document->clearKeyframes(layer());
}

void MainWindow::onActionFlipHorizontalAnimation()
{
    Pose const was = m_document->scene().poseOf(layer(), frame());
    Pose pose = was;
    pose.scaleX = -pose.scaleX;
    m_document->animatePose(layer(), frame(), was, pose, tr("Flip Horizontal"));
}

void MainWindow::onActionFlipVerticalAnimation()
{
    Pose const was = m_document->scene().poseOf(layer(), frame());
    Pose pose = was;
    pose.scaleY = -pose.scaleY;
    m_document->animatePose(layer(), frame(), was, pose, tr("Flip Vertical"));
}

void MainWindow::onActionTopLight()
{
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Top Light"), tr("How strongly the drawings before and after show (per cent):"),
                                             m_canvas->onionOpacity(), 5, 100, 5, &ok);
    if (ok)
        m_canvas->setOnionOpacity(percent);
}

void MainWindow::onActionSubstitutePrevious()
{
    m_document->stepSubstitution(layer(), frame(), -1);
}

void MainWindow::onActionSubstituteNext()
{
    m_document->stepSubstitution(layer(), frame(), +1);
}

void MainWindow::onActionSubstituteSameSet(QString const& direction)
{
    m_document->stepSubstitutionInSet(layer(), frame(), direction.toInt() < 0 ? -1 : 1);
}

void MainWindow::onActionSubstituteLayer(QString const& direction)
{
    Layer const& l = m_document->scene().layers[layer()];
    int const shown = l.drawingAt(frame());
    if (shown == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("There is no drawing on this frame to swap."), 5000);
        return;
    }
    if (!m_document->stepDrawingSet(layer(), frame(), direction.toInt() < 0 ? -1 : 1))
        statusBar()->showMessage(l.drawingSets().size() < 2
                                     ? tr("The layer has one drawing layer only: put drawings in others with Drawing > Drawing Layer.")
                                     : tr("No other drawing layer has a drawing called %1.").arg(l.drawingLabel(shown)),
                                 6000);
}

void MainWindow::onActionSetDrawingLayer()
{
    Layer const& l = m_document->scene().layers[layer()];
    int const shown = l.drawingAt(frame());
    if (!l.holdsDrawings() || shown == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("Choose a cell with a drawing to put it in a drawing layer."), 5000);
        return;
    }
    bool ok = false;
    QStringList sets = l.drawingSets();
    sets.removeAll(QString());
    QString const set = QInputDialog::getItem(this, tr("Drawing Layer"),
                                              tr("The drawing layer drawing %1 belongs to (a set of drawings of the layer - \"front\", \"side\";\n"
                                                 "empty for none). Substitute Layer Next swaps a drawing for the one of its name in the next.")
                                                  .arg(l.drawingLabel(shown)),
                                              QStringList{QString()} + sets, int(sets.indexOf(l.drawings[shown].set)) + 1, true, &ok);
    if (!ok)
        return;
    if (!m_document->setDrawingSet(layer(), frame(), set) && set.trimmed() != l.drawings[shown].set)
        statusBar()->showMessage(tr("The drawing layer %1 has a drawing called %2 already.").arg(set.trimmed(), l.drawingLabel(shown)), 6000);
}

void MainWindow::onActionBakeDrivers()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    int const baked = m_document->bakeDrivers(layers);
    statusBar()->showMessage(baked > 0 ? tr("%n layer(s) baked: a keyframe on every frame of what they followed; they follow no more.", nullptr, baked)
                                       : tr("No layer selected follows another (Layer Properties > Attribute Controls)."),
                             6000);
}

void MainWindow::onActionSkins()
{
    Scene const& scene = m_document->scene();
    QStringList skins;
    for (Layer const& l : scene.layers)
        if (l.holdsDrawings())
            for (QString const& set : l.drawingSets())
                if (!set.isEmpty() && !skins.contains(set))
                    skins << set;
    if (skins.isEmpty()) {
        statusBar()->showMessage(tr("No drawing is in a drawing layer yet: put drawings in drawing layers (Drawing > Drawing Layer) - each is a skin."), 7000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("SkinsDialog"));
    dialog.setWindowTitle(tr("Skins"));
    QComboBox* const skin = new QComboBox(&dialog);
    skin->setObjectName(QStringLiteral("SkinName"));
    skin->addItems(skins);
    QComboBox* const which = new QComboBox(&dialog);
    which->setObjectName(QStringLiteral("SkinLayers"));
    which->addItems({tr("The whole scene"), tr("The layers selected")});
    QComboBox* const when = new QComboBox(&dialog);
    when->setObjectName(QStringLiteral("SkinFrames"));
    when->addItem(tr("All frames"));
    int const marker = scene.markerAt(frame());
    if (marker >= 0)
        when->addItem(tr("The frames of the marker %1").arg(scene.markers[marker].name));
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(new QLabel(tr("Each drawing is swapped for the drawing of its name in the skin's drawing layer, where there is one."), &dialog));
    form->addRow(tr("Skin"), skin);
    form->addRow(tr("On"), which);
    form->addRow(tr("Over"), when);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QVector<int> layers;
    if (which->currentIndex() == 1) {
        layers = m_timeline->selectedLayers();
        if (layers.isEmpty())
            layers = {layer()};
    } else {
        for (int l = 0; l < scene.layers.size(); ++l)
            layers.push_back(l);
    }
    int first = 0, last = scene.frameCount() - 1;
    if (when->currentIndex() == 1 && marker >= 0) {
        first = scene.markers[marker].frame;
        last = first + std::max(1, scene.markers[marker].length) - 1;
    }
    int const changed = m_document->applySkin(layers, skin->currentText(), first, last);
    statusBar()->showMessage(changed > 0 ? tr("The skin %1: %n frame(s) changed.", nullptr, changed).arg(skin->currentText())
                                         : tr("Nothing to change: no drawing there has one of its name in %1.").arg(skin->currentText()),
                             6000);
}

void MainWindow::onActionSelectParent()
{
    int const parent = m_document->scene().parentIndex(layer());
    if (parent >= 0)
        m_timeline->setCurrentCell(parent, frame());
}

void MainWindow::onActionSelectChild()
{
    // The front-most layer attached to this one.
    Scene const& scene = m_document->scene();
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        if (scene.parentIndex(l) == layer()) {
            m_timeline->setCurrentCell(l, frame());
            return;
        }
    }
}

void MainWindow::onActionHideSelection()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    m_document->beginMacro(tr("Hide Selection"));
    for (int l : std::as_const(layers))
        if (l >= 0 && l < m_document->scene().layers.size())
            m_document->setLayerVisible(l, false);
    m_document->endMacro();
}

void MainWindow::onActionShowSelectionHideOthers()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    m_document->beginMacro(tr("Show Selection And Hide All Others"));
    for (int l = 0; l < m_document->scene().layers.size(); ++l)
        if (m_document->scene().layers[l].visible != layers.contains(l))
            m_document->setLayerVisible(l, layers.contains(l));
    m_document->endMacro();
}

void MainWindow::onActionSelectChildren()
{
    Scene const& scene = m_document->scene();
    // Front to back, as the Timeline lists them; a layer is in when what it is attached to is.
    QVector<int> family{layer()}, children;
    for (bool found = true; found;) {
        found = false;
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            if (!family.contains(l) && family.contains(scene.parentIndex(l))) {
                family.push_back(l);
                children.push_back(l);
                found = true;
            }
        }
    }
    if (children.isEmpty()) {
        statusBar()->showMessage(tr("Nothing is attached to this layer."), 4000);
        return;
    }
    std::sort(children.begin(), children.end(), std::greater<int>());
    m_timeline->setCurrentCell(children.first(), frame());
    m_timeline->setExtraLayers(children.mid(1));
}

void MainWindow::onActionSetEase(QString const& which)
{
    Scene const& scene = m_document->scene();
    int const first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    struct Change { int layer, frame; double in, out; };
    QVector<Change> changes;
    for (int l : m_timeline->selectedLayers()) {
        for (Keyframe const& key : scene.layers[l].keys) {
            if (key.frame < first || key.frame > last)
                continue;
            double in = key.easeIn, out = key.easeOut;
            if (which == QLatin1String("in") || which == QLatin1String("both"))
                in = 0.5;
            if (which == QLatin1String("out") || which == QLatin1String("both"))
                out = 0.5;
            if (which == QLatin1String("none"))
                in = out = 0.0;
            if (in != key.easeIn || out != key.easeOut)
                changes.push_back({l, key.frame, in, out});
        }
    }
    if (changes.isEmpty()) {
        statusBar()->showMessage(tr("There is no keyframe to ease on the frames selected (or they are eased so already)."), 5000);
        return;
    }
    m_document->beginMacro(tr("Set Ease"));
    for (Change const& c : std::as_const(changes))
        m_document->setKeyEase(c.layer, c.frame, c.in, c.out);
    m_document->endMacro();
}

int MainWindow::setEaseOnSelection(double easeIn, double easeOut)
{
    Scene const& scene = m_document->scene();
    int const first = m_timeline->selectionFirstFrame(), last = m_timeline->selectionLastFrame();
    struct Change { int layer, frame; double in, out; };
    QVector<Change> changes;
    for (int l : m_timeline->selectedLayers()) {
        for (Keyframe const& key : scene.layers[l].keys) {
            if (key.frame < first || key.frame > last)
                continue;
            double const in = easeIn < 0.0 ? key.easeIn : std::clamp(easeIn, 0.0, 1.0), out = easeOut < 0.0 ? key.easeOut : std::clamp(easeOut, 0.0, 1.0);
            if (in != key.easeIn || out != key.easeOut)
                changes.push_back({l, key.frame, in, out});
        }
    }
    if (changes.isEmpty())
        return 0;
    m_document->beginMacro(tr("Set Ease"));
    for (Change const& c : std::as_const(changes))
        m_document->setKeyEase(c.layer, c.frame, c.in, c.out);
    m_document->endMacro();
    return int(changes.size());
}

// Set Ease on Selected Keyframes: how gently the move arrives at them and leaves them, each as a percentage.
void MainWindow::onActionSetEaseDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("SetEaseDialog"));
    dialog.setWindowTitle(tr("Set Ease on Keyframes"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const useIn = new QCheckBox(tr("Ease In (left)"), &dialog);
    useIn->setObjectName(QStringLiteral("EaseUseIn"));
    useIn->setChecked(true);
    QDoubleSpinBox* const in = new QDoubleSpinBox(&dialog);
    in->setObjectName(QStringLiteral("EaseInPercent"));
    QCheckBox* const useOut = new QCheckBox(tr("Ease Out (right)"), &dialog);
    useOut->setObjectName(QStringLiteral("EaseUseOut"));
    useOut->setChecked(true);
    QDoubleSpinBox* const out = new QDoubleSpinBox(&dialog);
    out->setObjectName(QStringLiteral("EaseOutPercent"));
    for (QDoubleSpinBox* box : {in, out}) {
        box->setRange(0.0, 100.0);
        box->setDecimals(2);
        box->setSuffix(QStringLiteral(" %"));
        box->setValue(33.0);
    }
    connect(useIn, &QCheckBox::toggled, in, &QDoubleSpinBox::setEnabled);
    connect(useOut, &QCheckBox::toggled, out, &QDoubleSpinBox::setEnabled);
    form->addRow(useIn, in);
    form->addRow(useOut, out);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (setEaseOnSelection(useIn->isChecked() ? in->value() / 100.0 : -1.0, useOut->isChecked() ? out->value() / 100.0 : -1.0) == 0)
        statusBar()->showMessage(tr("There is no keyframe to ease on the frames selected (or they are eased so already)."), 5000);
}

void MainWindow::onActionShowLayerProperties()
{
    if (QDockWidget* const dock = dockOf("LayerPropertiesDock")) {
        showView(dock);
        dock->raise();
    }
}

void MainWindow::onActionShowMessageLog()
{
    if (QDockWidget* const dock = dockOf("MessageLogDock")) {
        showView(dock);
        dock->raise();
    }
}

void MainWindow::onActionDebugMode(bool on)
{
    m_debugMode = on;
    m_actions->setChecked(QStringLiteral("DebugMode"), on);
    if (on)
        onActionShowMessageLog();
    if (QPlainTextEdit* const log = findChild<QPlainTextEdit*>(QStringLiteral("MessageLog")))
        log->appendPlainText(on ? tr("Debug Mode is on: every command and every step of undo is told here.") : tr("Debug Mode is off."));
}

void MainWindow::onActionToggleWaveforms(bool on)
{
    m_timeline->setShowWaveforms(on);
}

void MainWindow::stepKeyframe(int direction)
{
    Layer const& l = m_document->scene().layers[layer()];
    int const target = direction < 0 ? l.previousKeyFrame(frame()) : l.nextKeyFrame(frame());
    if (target >= 0)
        m_timeline->setCurrentFrame(target);
}
