// Eclipse - the main window: the layer commands - add, delete, lock, colour, rename, move, attach.
#include "ui/window/MainWindow.h"

#include <QColorDialog>
#include <QInputDialog>

#include "actions/dialogs/ShortcutsDialog.h"
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
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "audio/lipsync/LipSync.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

void MainWindow::onActionAddDrawingLayer()
{
    int const at = m_document->addLayer(layer());
    m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionAddPeg()
{
    int const at = m_document->addPeg(layer());
    m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionAddColourCard()
{
    m_timeline->setCurrentCell(m_document->addColourCard(), frame());
}

void MainWindow::onActionAddGroup()
{
    m_timeline->setCurrentCell(m_document->addGroup(layer()), frame());
    statusBar()->showMessage(tr("Drag layers onto the group in the Timeline to put them in it."), 6000);
}

void MainWindow::onActionAddBone()
{
    m_timeline->setCurrentCell(m_document->addBone(layer()), frame());
    statusBar()->showMessage(tr("Turn the bone with the Transform tool. Add another while it is selected to carry the chain on from its tip."), 6000);
}

void MainWindow::onActionAddQuadmap()
{
    m_timeline->setCurrentCell(m_document->addQuadmap(layer()), frame());
    m_canvas->setTool(Canvas::Transform);
    statusBar()->showMessage(tr("Attach layers to the quadmap, then drag its corners with the Transform tool."), 6000);
}

void MainWindow::onActionGroupSelection()
{
    int const at = m_document->groupLayers(m_timeline->selectedLayers());
    if (at >= 0)
        m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionAddCamera()
{
    int const at = m_document->addCamera();
    m_timeline->setCurrentCell(at, frame());
    statusBar()->showMessage(m_document->scene().cameraIndex() == at ? tr("The scene is now seen through the camera. Move it with the Transform tool.")
                                                                     : tr("Another camera. The scene is still seen through %1: choose this one in Scene > Camera.")
                                                                           .arg(m_document->scene().layers[m_document->scene().cameraIndex()].name), 8000);
}

void MainWindow::onActionAddParentPeg()
{
    int const at = m_document->addParentPeg(layer());
    if (at >= 0)
        m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionDeleteLayer()
{
    m_document->removeLayers(m_timeline->selectedLayers());
}

void MainWindow::onActionDuplicateLayer()
{
    // From the front one back, so that the copies made do not move the layers still to copy.
    QVector<int> layers = m_timeline->selectedLayers();
    std::sort(layers.begin(), layers.end(), std::greater<int>());
    int made = -1;
    if (layers.size() > 1)
        m_document->beginMacro(tr("Duplicate Layers"));
    for (int l : layers) {
        int const copy = m_document->duplicateLayer(l);
        made = made < 0 ? copy : made + 1;
    }
    if (layers.size() > 1)
        m_document->endMacro();
    if (made >= 0)
        m_timeline->setCurrentCell(made, frame());
}

void MainWindow::setLocked(bool locked)
{
    m_document->editLayers(m_timeline->selectedLayers(), locked ? tr("Lock") : tr("Unlock"), [locked](Layer& l) { l.locked = locked; });
}

void MainWindow::onActionLockAll()
{
    m_document->setAllLocked(true);
}

void MainWindow::onActionUnlockAll()
{
    m_document->setAllLocked(false);
}

void MainWindow::onActionLockAllOthers()
{
    m_document->setAllLocked(true, layer());
}

void MainWindow::onActionToggleLayerEnabled()
{
    // As the current layer goes, so go the others selected.
    bool const enable = !m_document->scene().layers[layer()].visible;
    m_document->editLayers(m_timeline->selectedLayers(), enable ? tr("Enable") : tr("Disable"), [enable](Layer& l) { l.visible = enable; });
}

void MainWindow::onActionEnableAll()
{
    m_document->setAllVisible(true);
}

void MainWindow::onActionDisableAllOthers()
{
    m_document->setAllVisible(false, layer());
}

void MainWindow::onActionToggleSolo()
{
    m_document->setLayerSolo(layer(), !m_document->scene().layers[layer()].solo);
}

void MainWindow::onActionCollapseExpand()
{
    if (m_document->scene().hasChildren(layer()))
        m_document->setLayerCollapsed(layer(), !m_document->scene().layers[layer()].collapsed);
}

void MainWindow::onActionCollapseAll()
{
    m_document->setAllCollapsed(true);
}

void MainWindow::onActionExpandAll()
{
    m_document->setAllCollapsed(false);
}

void MainWindow::pickTrackColour(int l)
{
    if (l < 0 || l >= m_document->scene().layers.size())
        return;
    QColor const now = m_document->scene().layers[l].trackColor;
    QColor const chosen = QColorDialog::getColor(now.isValid() ? now : QColor(120, 120, 120), this, tr("Track Colour"));
    if (!chosen.isValid())
        return;
    // The layers selected with it take the colour too.
    QVector<int> const selected = m_timeline->selectedLayers();
    if (selected.contains(l) && selected.size() > 1)
        m_document->editLayers(selected, tr("Change Track Colour"), [chosen](Layer& layer) { layer.trackColor = chosen; });
    else
        m_document->setLayerColor(l, chosen);
}

void MainWindow::onActionChangeTrackColour()
{
    pickTrackColour(layer());
}

void MainWindow::onActionDefaultTrackColour()
{
    m_document->editLayers(m_timeline->selectedLayers(), tr("Default Track Colour"), [](Layer& l) { l.trackColor = QColor(); });
}

void MainWindow::onActionMergeLayers()
{
    int const at = m_document->mergeLayers(m_timeline->selectedLayers());
    if (at < 0) {
        statusBar()->showMessage(tr("Select two or more drawing layers to merge."), 5000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionToggleLayerOnion()
{
    Scene const& scene = m_document->scene();
    bool const on = !scene.layers[layer()].onion;
    QVector<int> drawingLayers;
    for (int l : m_timeline->selectedLayers())
        if (scene.layers[l].holdsDrawings())
            drawingLayers.push_back(l);
    m_document->editLayers(drawingLayers, on ? tr("Onion Skin On") : tr("Onion Skin Off"), [on](Layer& l) { l.onion = on; });
}

void MainWindow::stepSibling(int direction)
{
    // The next layer, towards the front or the back, attached to the same thing as this one.
    Scene const& scene = m_document->scene();
    int const parent = scene.parentIndex(layer());
    for (int l = layer() + direction; l >= 0 && l < scene.layers.size(); l += direction) {
        if (scene.parentIndex(l) == parent) {
            m_timeline->setCurrentCell(l, frame());
            return;
        }
    }
}

void MainWindow::onActionResetAll()
{
    // On this frame the layer stands as it was made: not moved, turned, scaled or faded.
    Scene const& scene = m_document->scene();
    if (!m_timeline->hasParameters(layer()))
        return;
    Pose rest;
    rest.fx = scene.layers[layer()].poseAt(frame()).fx;     // the amounts of its effects are not a transformation
    if (scene.layers[layer()].poseAt(frame()) == rest)
        return;
    m_document->setKeyframe(layer(), frame(), rest, tr("Reset All"));
}

void MainWindow::stepLayer(int direction)
{
    // Up or down the rows of the Timeline, so past layers that are folded away.
    QVector<int> const rows = m_timeline->rows();
    int const row = int(rows.indexOf(layer())) + direction;
    if (row >= 0 && row < rows.size())
        m_timeline->setCurrentCell(rows[row], frame());
}

void MainWindow::onActionRenameLayer()
{
    renameLayer(layer());
}

void MainWindow::renameLayer(int l)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Layer"), tr("Name:"), QLineEdit::Normal, scene.layers[l].name, &ok);
    if (ok)
        m_document->renameLayer(l, name);
}

void MainWindow::onActionMoveLayerUp()
{
    int const l = layer();
    if (l + 1 < m_document->scene().layers.size()) {
        m_document->moveLayer(l, +1);
        m_timeline->setCurrentCell(l + 1, frame());
    }
}

void MainWindow::onActionMoveLayerDown()
{
    int const l = layer();
    if (l > 0) {
        m_document->moveLayer(l, -1);
        m_timeline->setCurrentCell(l - 1, frame());
    }
}

void MainWindow::onActionAttachTo()
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l < 0 || l >= scene.layers.size())
        return;
    // Everything it could follow without going round in a circle; front layers first.
    QStringList names;
    QVector<int> indices;
    for (int i = int(scene.layers.size()) - 1; i >= 0; --i) {
        if (i == l || scene.isAncestor(l, i))
            continue;
        names << scene.layers[i].name;
        indices << i;
    }
    if (names.isEmpty()) {
        statusBar()->showMessage(tr("There is no other layer to attach %1 to.").arg(scene.layers[l].name), 6000);
        return;
    }
    bool ok = false;
    int const current = int(indices.indexOf(scene.parentIndex(l)));
    QString const chosen = QInputDialog::getItem(this, tr("Attach To"), tr("%1 follows:").arg(scene.layers[l].name), names,
                                                 std::max(0, current), false, &ok);
    if (ok)
        m_document->setParent(l, indices[names.indexOf(chosen)]);
}

void MainWindow::onActionDetach()
{
    m_document->setParent(layer(), -1);
}
