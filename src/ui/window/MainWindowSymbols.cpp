// Eclipse - the main window: symbols, synced and cloned layers, stamps from drawings.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "ui/panels/ColourEditor.h"
#include <QInputDialog>
#include <QMessageBox>

#include "actions/dialogs/ShortcutsDialog.h"
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
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "audio/lipsync/LipSync.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QSettings>
#include <QStatusBar>

void MainWindow::onActionCreateSymbol()
{
    // The layers selected - and what is attached to them - become a symbol, named after the first of them.
    QVector<int> const picked = m_timeline->selectedLayers();
    Scene const& scene = m_document->scene();
    QString const name = !picked.isEmpty() && picked.first() >= 0 && picked.first() < scene.layers.size() ? scene.layers[picked.first()].name : QString();
    int const at = m_document->createSymbol(picked, name);
    if (at < 0) {
        statusBar()->showMessage(tr("Select the layers to make a symbol of. (A camera and a sound stay in the scene.)"), 6000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
    updateActionStates();
    statusBar()->showMessage(tr("Symbol %1 made. Edit > Symbol > Edit Symbol goes inside it; Insert Symbol shows it on another layer.")
                                 .arg(m_document->scene().layers[at].name), 7000);
}

void MainWindow::onActionCreateEmptySymbol()
{
    int const at = m_document->insertEmptySymbol(QString(), layer());
    if (at < 0)
        return;
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
    statusBar()->showMessage(tr("An empty symbol, on a layer of its own. Edit > Symbol > Edit Symbol goes inside it to draw."), 7000);
}

void MainWindow::onActionInsertSymbol()
{
    Scene const& scene = m_document->scene();
    if (scene.symbols.isEmpty()) {
        statusBar()->showMessage(tr("The scene has no symbols yet: select layers and use Edit > Symbol > Create Symbol."), 6000);
        return;
    }
    QStringList names;
    for (Scene::Symbol const& symbol : scene.symbols)
        names << symbol.name;
    bool ok = names.size() == 1;
    QString const chosen = ok ? names.first() : QInputDialog::getItem(this, tr("Insert Symbol"), tr("Symbol"), names, 0, false, &ok);
    if (!ok)
        return;
    int const id = scene.symbols[std::max(0, int(names.indexOf(chosen)))].id;
    int const at = m_document->insertSymbol(id, layer());
    if (at >= 0) {
        m_timeline->clearRange();
        m_timeline->setCurrentCell(at, frame());
    }
    updateActionStates();
}

void MainWindow::onActionEditSymbol()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || !scene.layers[layer()].showsSymbol()) {
        statusBar()->showMessage(tr("Select a layer that shows a symbol."), 5000);
        return;
    }
    // The frame of the symbol the layer shows here is the frame to begin on inside it.
    int const inside = scene.layers[layer()].drawingAt(frame());
    if (m_playTimer.isActive())
        stop();
    m_canvas->clearSelection();
    GlobalFrame const around{scene.layers[layer()].cells, frame()};
    if (!m_document->enterSymbol(scene.layers[layer()].symbolId))
        return;
    m_globalFrames.push_back(around);
    m_timeline->clearRange();
    m_timeline->setCurrentCell(int(m_document->scene().layers.size()) - 1, std::clamp(inside, 0, m_document->scene().frameCount() - 1));
    updateTitle();
    updateActionStates();
    statusBar()->showMessage(tr("Inside the symbol %1. Edit > Symbol > Leave Symbol goes back to the scene.").arg(m_document->symbolTrail().last()), 7000);
}

void MainWindow::onActionLeaveSymbol()
{
    if (!m_document->insideSymbol())
        return;
    if (m_playTimer.isActive())
        stop();
    m_canvas->clearSelection();
    QString const was = m_document->symbolTrail().last();
    int const globalFrame = m_globalFrames.isEmpty() ? -1 : m_globalFrames.takeLast().frame;
    m_document->leaveSymbol();
    // Back on a layer that shows it, if there is one.
    int back = int(m_document->scene().layers.size()) - 1;
    for (int l = 0; l < m_document->scene().layers.size(); ++l) {
        Scene::Symbol const* const symbol = m_document->scene().findSymbol(m_document->scene().layers[l].symbolId);
        if (symbol && symbol->name == was)
            back = l;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(back, std::clamp(globalFrame >= 0 ? globalFrame : frame(), 0, m_document->scene().frameCount() - 1));
    updateTitle();
    updateActionStates();
}

void MainWindow::onActionGlobalFrame(QString const& way)
{
    if (!m_document->insideSymbol() || m_globalFrames.isEmpty()) {
        statusBar()->showMessage(tr("Go to Global Frame works inside a symbol (Edit > Symbol > Edit Symbol)."), 4000);
        return;
    }
    GlobalFrame& around = m_globalFrames.last();
    int const next = std::clamp(around.frame + (way == QLatin1String("previous") ? -1 : 1), 0, std::max(0, int(around.cells.size()) - 1));
    around.frame = next;
    int const inside = around.cells.value(next, -1);
    if (inside >= 0)
        m_timeline->setCurrentCell(layer(), std::clamp(inside, 0, m_document->scene().frameCount() - 1));
    statusBar()->showMessage(inside >= 0 ? tr("Global frame %1: the symbol's frame %2.").arg(next + 1).arg(inside + 1)
                                         : tr("Global frame %1: the symbol is not shown there.").arg(next + 1), 4000);
}

void MainWindow::onActionAddSyncedLayer()
{
    int const at = m_document->addSyncedLayer(layer());
    if (at < 0) {
        statusBar()->showMessage(tr("Select a drawing layer to add a synced layer to."), 5000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
    updateActionStates();
    statusBar()->showMessage(tr("The two layers keep the same timing: change the exposure of one and the other follows, each with drawings of its own."), 8000);
}

void MainWindow::onActionSyncLayerWith()
{
    Scene const& scene = m_document->scene();
    int const current = layer();
    if (current < 0 || current >= scene.layers.size() || !scene.layers[current].holdsDrawings() || scene.layers[current].showsSymbol()) {
        statusBar()->showMessage(tr("Select a drawing layer to sync."), 5000);
        return;
    }
    // The drawing layers it could keep time with; and, when it is synced, the way out.
    QStringList names;
    QVector<int> which;
    if (scene.layers[current].syncGroup != 0) {
        names << tr("(none - remove the sync)");
        which << -1;
    }
    int now = 0;
    for (int l = 0; l < scene.layers.size(); ++l) {
        if (l == current || !scene.layers[l].holdsDrawings() || scene.layers[l].showsSymbol())
            continue;
        if (scene.layers[l].syncGroup != 0 && scene.layers[l].syncGroup == scene.layers[current].syncGroup)
            now = int(names.size());
        names << scene.layers[l].name;
        which << l;
    }
    if (which.isEmpty()) {
        statusBar()->showMessage(tr("There is no other drawing layer to sync with."), 5000);
        return;
    }
    bool ok = false;
    QString const picked = QInputDialog::getItem(this, tr("Sync Layer With"), tr("Keep the timing of \"%1\" the same as that of:").arg(scene.layers[current].name),
                                                 names, now, false, &ok);
    if (!ok || names.indexOf(picked) < 0)
        return;
    m_document->syncLayerWith(current, which[names.indexOf(picked)]);
    updateActionStates();
}

void MainWindow::onActionCloneLayer()
{
    Scene const& scene = m_document->scene();
    if (layer() >= 0 && layer() < scene.layers.size() && scene.layers[layer()].holdsDrawings() && !scene.layers[layer()].showsSymbol()) {
        int const at = m_document->addCloneLayer(layer(), false);
        if (at >= 0) {
            m_timeline->clearRange();
            m_timeline->setCurrentCell(at, frame());
            updateActionStates();
            statusBar()->showMessage(tr("The two layers share their drawings: what is drawn on one is on the other. Each has a timing of its own."), 8000);
        }
        return;
    }
    int const at = m_document->cloneLayer(layer());
    if (at < 0) {
        statusBar()->showMessage(tr("Select a drawing layer to clone."), 5000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
    updateActionStates();
    statusBar()->showMessage(tr("The two layers show the same drawings (a symbol): change them with Edit > Symbol > Edit Symbol and both change. Each keeps a timing of its own."), 8000);
}

void MainWindow::onActionCloneLayerTiming()
{
    int const at = m_document->addCloneLayer(layer());
    if (at < 0) {
        statusBar()->showMessage(tr("Select a drawing layer to clone."), 5000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
    updateActionStates();
    statusBar()->showMessage(tr("The two layers are the same drawings and the same timing: what is drawn or timed on one is so on the other. Each has its own place."), 8000);
}

void MainWindow::onActionMakeCloneIndependent()
{
    if (layer() < 0 || layer() >= m_document->scene().layers.size() || m_document->scene().layers[layer()].cloneGroup == 0) {
        statusBar()->showMessage(tr("Select a layer that is a clone."), 5000);
        return;
    }
    m_document->makeCloneIndependent(layer());
    updateActionStates();
}

void MainWindow::onActionDuplicateSymbol()
{
    if (!m_document->duplicateSymbol(layer()))
        statusBar()->showMessage(tr("Select a layer that shows a symbol."), 5000);
}

void MainWindow::onActionExpandSymbol()
{
    int const at = m_document->expandSymbol(layer());
    if (at < 0) {
        statusBar()->showMessage(tr("Select a symbol without group effects, opacity changes or deformation to expand it."), 7000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionCreateStampFromDrawings()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || !scene.layers[layer()].holdsDrawings()) {
        statusBar()->showMessage(tr("Choose a drawing layer, and select the frames whose drawings are to make the stamp."), 5000);
        return;
    }
    Layer const& from = scene.layers[layer()];
    // Each drawing once, in the order of the frames.
    QVector<int> taken;
    for (int f = m_timeline->selectionFirstFrame(); f <= m_timeline->selectionLastFrame(); ++f) {
        int const index = from.drawingAt(f);
        if (index != Layer::kNoDrawing && !taken.contains(index))
            taken.push_back(index);
    }
    QVector<Drawing> drawings;
    for (int index : std::as_const(taken))
        drawings.push_back(from.drawings[index]);
    m_canvas->createStampFrom(drawings);
}
