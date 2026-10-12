// Eclipse - the main window: the commands of the Animation menu - keyframes, ease, substitution, selecting parent and child.
#include "ui/window/MainWindow.h"

#include <QDockWidget>
#include <QInputDialog>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
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
    Pose pose = m_document->scene().layers[layer()].poseAt(frame());
    pose.scaleX = -pose.scaleX;
    m_document->setKeyframe(layer(), frame(), pose, tr("Flip Horizontal"));
}

void MainWindow::onActionFlipVerticalAnimation()
{
    Pose pose = m_document->scene().layers[layer()].poseAt(frame());
    pose.scaleY = -pose.scaleY;
    m_document->setKeyframe(layer(), frame(), pose, tr("Flip Vertical"));
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
