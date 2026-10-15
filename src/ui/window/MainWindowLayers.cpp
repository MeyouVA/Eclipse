// Eclipse - the main window: the layer commands - add, delete, lock, colour, rename, move, attach.
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

#include <QColorDialog>
#include <QInputDialog>
#include <QMessageBox>

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
#include <QSettings>
#include <QStatusBar>

int MainWindow::addDrawingLayer(QString const& name, bool wholeScene)
{
    m_document->beginMacro(tr("Add Drawing Layer"));
    int const at = m_document->addLayer(layer());
    QString const wanted = name.trimmed();
    if (!wanted.isEmpty() && wanted != m_document->scene().layers[at].name)
        m_document->renameLayer(at, wanted);
    if (wholeScene) {
        m_document->newDrawing(at, 0);
        m_document->setExposure(at, 0, m_document->scene().frameCount());
    }
    m_document->endMacro();
    m_timeline->setCurrentCell(at, frame());
    return at;
}

void MainWindow::onActionAddDrawingLayer()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AddDrawingLayer"));
    dialog.setWindowTitle(tr("Add Drawing Layer"));
    QLineEdit* const name = new QLineEdit(tr("Drawing"), &dialog);
    name->setObjectName(QStringLiteral("LayerName"));
    name->selectAll();
    // What each sheet of its drawings is made of.  Eclipse draws in vectors.
    auto kinds = [&dialog, this](char const* objectName, bool canBitmap) {
        QWidget* const row = new QWidget(&dialog);
        row->setObjectName(QLatin1String(objectName));
        QRadioButton* const vector = new QRadioButton(tr("Vector"), row);
        QRadioButton* const bitmap = new QRadioButton(tr("Bitmap"), row);
        vector->setChecked(true);
        bitmap->setObjectName(QLatin1String(objectName) + QStringLiteral("Bitmap"));
        bitmap->setEnabled(canBitmap);
        bitmap->setToolTip(canBitmap ? tr("A bitmap layer: what is drawn is painted in pixels, and the eraser rubs pixels out.")
                                     : tr("The colour art is drawn in vectors."));
        QHBoxLayout* const across = new QHBoxLayout(row);
        across->setContentsMargins(0, 0, 0, 0);
        across->addWidget(vector);
        across->addWidget(bitmap);
        across->addStretch(1);
        return row;
    };
    QCheckBox* const wholeScene = new QCheckBox(tr("Extend Exposure for Scene Duration"), &dialog);
    wholeScene->setObjectName(QStringLiteral("LayerWholeScene"));
    wholeScene->setToolTip(tr("The layer starts with an empty drawing held from the first frame to the last."));
    QFormLayout* const form = new QFormLayout;
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Line Art"), kinds("LayerLineArt", true));
    form->addRow(tr("Colour Art"), kinds("LayerColourArt", false));
    form->addRow(wholeScene);
    QPushButton* const addAndClose = new QPushButton(tr("Add and Close"), &dialog);
    addAndClose->setObjectName(QStringLiteral("LayerAddAndClose"));
    addAndClose->setDefault(true);
    QPushButton* const add = new QPushButton(tr("Add"), &dialog);
    add->setObjectName(QStringLiteral("LayerAdd"));
    QPushButton* const close = new QPushButton(tr("Close"), &dialog);
    close->setObjectName(QStringLiteral("LayerClose"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(addAndClose);
    buttons->addWidget(add);
    buttons->addWidget(close);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addLayout(buttons);
    auto addOne = [this, name, wholeScene, &dialog] {
        QRadioButton* const bitmap = dialog.findChild<QRadioButton*>(QStringLiteral("LayerLineArtBitmap"));
        m_document->beginMacro(tr("Add Drawing Layer"));
        int const at = addDrawingLayer(name->text(), wholeScene->isChecked());
        if (bitmap && bitmap->isChecked())
            m_document->setLayerBitmap(at, true);
        m_document->endMacro();
        name->setFocus();
        name->selectAll();
    };
    connect(add, &QPushButton::clicked, &dialog, addOne);
    connect(addAndClose, &QPushButton::clicked, &dialog, [&dialog, addOne] {
        addOne();
        dialog.accept();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
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

// `how` is "tag", "untag" (the layers selected) or "untagAll".
void MainWindow::onActionTagLayers(QString const& how)
{
    bool const on = how == QLatin1String("tag");
    QVector<int> layers = m_timeline->selectedLayers();
    if (how == QLatin1String("untagAll")) {
        layers.clear();
        for (int l = 0; l < m_document->scene().layers.size(); ++l)
            if (m_document->scene().layers[l].tagged)
                layers.push_back(l);
    }
    bool any = false;
    for (int l : std::as_const(layers))
        any = any || m_document->scene().layers[l].tagged != on;
    if (any)
        m_document->editLayers(layers, on ? tr("Tag Layers") : tr("Untag Layers"), [on](Layer& l) { l.tagged = on; });
}

void MainWindow::onActionLockTimelineRuler()
{
    bool const on = !m_timeline->rulerLocked();
    m_timeline->setRulerLocked(on);
    m_actions->setChecked(QStringLiteral("LockTimelineRuler"), on);
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/lockTimelineRuler"), on);
    statusBar()->showMessage(on ? tr("The Timeline's ruler is locked: the end of the scene stays where it is.")
                                : tr("The Timeline's ruler is unlocked: drag the end of the scene to make it longer or shorter."), 5000);
}

// `mode` is "all", "selected" or "tagged": which layers the Timeline shows.
void MainWindow::onActionTimelineViewMode(QString const& mode)
{
    m_timeline->setViewMode(mode == QLatin1String("selected") ? Timeline::ShowSelected : mode == QLatin1String("tagged") ? Timeline::ShowTagged : Timeline::ShowAll);
}

// Timeline > Show: a kind of layer gets its rows, or loses them (the layers are in the scene all the same).
void MainWindow::onActionTimelineShowKind(QString const& which)
{
    struct Entry {
        char const* name;
        char const* action;
        QVector<int> kinds;
    };
    Entry const entries[] = {
        {"drawings", "TimelineShowDrawings", {int(Layer::DrawingLayer)}},
        {"pegs", "TimelineShowPegs", {int(Layer::Peg)}},
        {"sound", "TimelineShowSound", {int(Layer::Sound)}},
        {"groups", "TimelineShowGroups", {int(Layer::Group)}},
        {"cameras", "TimelineShowCameras", {int(Layer::Camera)}},
        {"others", "TimelineShowOthers", {int(Layer::ColourCard), int(Layer::Bone), int(Layer::Quadmap)}},
    };
    for (Entry const& entry : entries) {
        if (which != QLatin1String(entry.name))
            continue;
        bool const shown = !m_timeline->kindShown(entry.kinds.first());
        for (int kind : entry.kinds)
            m_timeline->setKindShown(kind, shown);
        m_actions->setChecked(QLatin1String(entry.action), shown);
    }
}

// ---- symbols ------------------------------------------------------------------------------------

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
    if (!m_document->enterSymbol(scene.layers[layer()].symbolId))
        return;
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
    m_document->leaveSymbol();
    // Back on a layer that shows it, if there is one.
    int back = int(m_document->scene().layers.size()) - 1;
    for (int l = 0; l < m_document->scene().layers.size(); ++l) {
        Scene::Symbol const* const symbol = m_document->scene().findSymbol(m_document->scene().layers[l].symbolId);
        if (symbol && symbol->name == was)
            back = l;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(back, std::clamp(frame(), 0, m_document->scene().frameCount() - 1));
    updateTitle();
    updateActionStates();
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
    // A drawing layer: a clone that shares its drawings and has a timing of its own, drawn on directly.
    // (A layer that shows a symbol: another layer that shows the same symbol.)
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
        statusBar()->showMessage(tr("Select a layer that shows a symbol."), 5000);
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

// `as` is "key", "breakdown", "inbetween" or "none".
void MainWindow::onActionMarkDrawing(QString const& as)
{
    int const mark = as == QLatin1String("key") ? Drawing::KeyMark : as == QLatin1String("breakdown") ? Drawing::BreakdownMark
                     : as == QLatin1String("inbetween") ? Drawing::InBetweenMark : Drawing::NoMark;
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drawingAt(frame()) == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("There is no drawing on this frame to mark."), 4000);
        return;
    }
    m_document->markDrawing(layer(), frame(), mark);
}

// Which layers show their neighbouring drawings while another is worked on: "add" / "remove" the
// layers selected, "removeUnselected", "addAll", "removeAll".
void MainWindow::onActionOnionLayers(QString const& which)
{
    Scene const& scene = m_document->scene();
    QVector<int> const selected = m_timeline->selectedLayers();
    bool const on = which == QLatin1String("add") || which == QLatin1String("addAll");
    QVector<int> layers;
    for (int l = 0; l < scene.layers.size(); ++l) {
        bool const mine = selected.contains(l);
        bool const wanted = which == QLatin1String("addAll") || which == QLatin1String("removeAll") ? true
                            : which == QLatin1String("removeUnselected") ? !mine : mine;
        if (wanted && scene.layers[l].holdsDrawings() && scene.layers[l].onion != on)
            layers.push_back(l);
    }
    if (!layers.isEmpty())
        m_document->editLayers(layers, on ? tr("Onion Skin On") : tr("Onion Skin Off"), [on](Layer& l) { l.onion = on; });
}

// The frames selected in the Timeline, of the layers selected, marked with a colour by name ("none": unmarked).
void MainWindow::onActionMarkFrame(QString const& colour)
{
    m_document->markFrames(m_timeline->selectedLayers(), m_timeline->selectionFirstFrame(), m_timeline->selectionLastFrame(),
                           colour == QLatin1String("none") ? QColor() : QColor(colour));
}

// All the keyframes of the layers selected: "clear" takes them away, "stop" makes them stop-motion
// (each holds until the next), "motion" makes them motion keyframes (each glides to the next).
void MainWindow::onActionLayerKeys(QString const& how)
{
    Scene const& scene = m_document->scene();
    bool const clear = how == QLatin1String("clear"), tween = how == QLatin1String("motion");
    QVector<int> layers;
    for (int l : m_timeline->selectedLayers()) {
        bool differs = false;
        for (Keyframe const& key : scene.layers[l].keys)
            differs = differs || clear || key.tween != tween;
        if (differs)
            layers.push_back(l);
    }
    if (layers.isEmpty())
        return;
    m_document->editLayers(layers, clear ? tr("Clear All Values") : tween ? tr("Set Motion Keyframes") : tr("Set Stop-Motion Keyframes"), [clear, tween](Layer& l) {
        if (clear)
            l.keys.clear();
        for (Keyframe& key : l.keys)
            key.tween = tween;
    });
}

void MainWindow::onActionRenameWithPrefix()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drawings.isEmpty()) {
        statusBar()->showMessage(tr("This layer has no drawings to rename."), 4000);
        return;
    }
    bool ok = false;
    QString const prefix = QInputDialog::getText(this, tr("Rename Drawing with Prefix"), tr("What every drawing of the layer is to be called, before its number:"),
                                                 QLineEdit::Normal, scene.layers[layer()].name + QLatin1Char('-'), &ok);
    if (ok)
        m_document->renameDrawingsWithPrefix(layer(), prefix);
}

void MainWindow::onActionRenameByFrame()
{
    if (!m_document->renameDrawingsByFrame(layer()))
        statusBar()->showMessage(tr("The drawings of this layer are named after their frames already, or it has none."), 5000);
}

void MainWindow::onActionRenameDrawing()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drawingAt(frame()) == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("There is no drawing on this frame to rename."), 4000);
        return;
    }
    Layer const& current = scene.layers[layer()];
    int const index = current.drawingAt(frame());
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Drawing"), tr("Name (empty: its number, %1):").arg(index + 1), QLineEdit::Normal,
                                               current.drawings[index].name, &ok);
    if (ok && name.trimmed() != current.drawings[index].name && !m_document->renameDrawing(layer(), frame(), name))
        QMessageBox::warning(this, tr("Eclipse"), tr("Another drawing of this layer is called \"%1\".").arg(name.trimmed()));
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

// Column Properties: what the current layer is as a column of the Timeline and the Xsheet, in one window.
void MainWindow::onActionColumnProperties()
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l < 0 || l >= scene.layers.size())
        return;
    Layer const was = scene.layers[l];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ColumnPropertiesDialog"));
    dialog.setWindowTitle(tr("Column Properties"));
    QLineEdit* const name = new QLineEdit(was.name, &dialog);
    name->setObjectName(QStringLiteral("ColumnName"));
    static char const* const kKinds[] = {QT_TR_NOOP("Drawing"), QT_TR_NOOP("Peg"), QT_TR_NOOP("Camera"), QT_TR_NOOP("Sound"),
                                         QT_TR_NOOP("Colour-Card"), QT_TR_NOOP("Group"), QT_TR_NOOP("Bone"), QT_TR_NOOP("Quadmap")};
    QLabel* const kind = new QLabel(tr(kKinds[was.kind]) + (was.showsSymbol() ? tr(" (shows a symbol)") : QString()), &dialog);
    kind->setObjectName(QStringLiteral("ColumnKind"));
    QColor colour = was.trackColor;
    QPushButton* const colourButton = new QPushButton(&dialog);
    colourButton->setObjectName(QStringLiteral("ColumnColour"));
    QCheckBox* const defaultColour = new QCheckBox(tr("Default colour"), &dialog);
    defaultColour->setObjectName(QStringLiteral("ColumnDefaultColour"));
    auto showColour = [colourButton, defaultColour, &colour] {
        colourButton->setEnabled(!defaultColour->isChecked());
        colourButton->setText(defaultColour->isChecked() || !colour.isValid() ? QObject::tr("(default)") : colour.name());
        colourButton->setStyleSheet(defaultColour->isChecked() || !colour.isValid() ? QString() : QStringLiteral("background-color: %1;").arg(colour.name()));
    };
    defaultColour->setChecked(!colour.isValid());
    connect(defaultColour, &QCheckBox::toggled, &dialog, showColour);
    connect(colourButton, &QPushButton::clicked, &dialog, [&] {
        QColor const chosen = QColorDialog::getColor(colour.isValid() ? colour : QColor(90, 120, 160), &dialog, tr("Track Colour"));
        if (chosen.isValid())
            colour = chosen;
        showColour();
    });
    showColour();
    QCheckBox* const enabled = new QCheckBox(tr("Enabled (in the picture)"), &dialog);
    enabled->setObjectName(QStringLiteral("ColumnEnabled"));
    enabled->setChecked(was.visible);
    QCheckBox* const locked = new QCheckBox(tr("Locked"), &dialog);
    locked->setObjectName(QStringLiteral("ColumnLocked"));
    locked->setChecked(was.locked);
    QCheckBox* const bitmap = new QCheckBox(tr("Bitmap layer"), &dialog);
    bitmap->setObjectName(QStringLiteral("ColumnBitmap"));
    bitmap->setChecked(was.bitmap);
    bitmap->setVisible(was.holdsDrawings() && !was.showsSymbol());
    // The other art layers, each a bitmap one of its own or not.
    QCheckBox* artBitmap[Drawing::SheetCount] = {};
    QHBoxLayout* const artRow = new QHBoxLayout;
    for (auto const& [sheet, label, id] : {std::tuple<int, char const*, char const*>{Drawing::UnderlaySheet, QT_TR_NOOP("Underlay"), "ColumnBitmapUnderlay"},
                                           {Drawing::ColourSheet, QT_TR_NOOP("Colour Art"), "ColumnBitmapColour"},
                                           {Drawing::OverlaySheet, QT_TR_NOOP("Overlay"), "ColumnBitmapOverlay"}}) {
        QCheckBox* const box = new QCheckBox(tr(label), &dialog);
        box->setObjectName(QLatin1String(id));
        box->setToolTip(tr("What is drawn on this art layer is painted in pixels, not kept as lines."));
        box->setChecked(was.bitmapSheet(sheet));
        box->setVisible(was.holdsDrawings() && !was.showsSymbol());
        artBitmap[sheet] = box;
        artRow->addWidget(box);
    }
    artRow->addStretch(1);
    auto artBits = [&artBitmap] {
        int bits = 0;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            if (artBitmap[sheet] && artBitmap[sheet]->isChecked())
                bits |= 1 << sheet;
        return bits;
    };
    QSpinBox* const fineness = new QSpinBox(&dialog);
    fineness->setObjectName(QStringLiteral("ColumnBitmapResolution"));
    fineness->setRange(25, 400);
    fineness->setSingleStep(25);
    fineness->setSuffix(tr(" %"));
    fineness->setValue(was.bitmapResolution);
    fineness->setToolTip(tr("How fine the pixels of a bitmap layer are: 100 % is a pixel for each unit of the scene, 200 % two across each. "
                            "Drawings already painted keep the pixels they have."));
    fineness->setVisible(was.holdsDrawings() && !was.showsSymbol());
    QLabel* const drawings = new QLabel(&dialog);
    drawings->setObjectName(QStringLiteral("ColumnDrawings"));
    if (was.holdsDrawings())
        drawings->setText(tr("%n drawing(s)", nullptr, int(was.drawings.size())) + (was.syncGroup ? tr(", synced") : QString())
                          + (was.cloneGroup ? (was.cloneTiming ? tr(", a clone (drawings and timing)") : tr(", a clone (drawings)")) : QString()));
    QFormLayout* const form = new QFormLayout;
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Type:"), kind);
    form->addRow(tr("Colour:"), colourButton);
    form->addRow(QString(), defaultColour);
    form->addRow(QString(), enabled);
    form->addRow(QString(), locked);
    form->addRow(QString(), bitmap);
    if (was.holdsDrawings() && !was.showsSymbol()) {
        form->addRow(tr("Bitmap art layers:"), artRow);
        form->addRow(tr("Bitmap resolution:"), fineness);
    } else {
        delete artRow;
    }
    if (was.holdsDrawings())
        form->addRow(tr("Drawings:"), drawings);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QColor const newColour = defaultColour->isChecked() ? QColor() : colour;
    bool const nameChanged = !name->text().trimmed().isEmpty() && name->text().trimmed() != was.name;
    bool const rest = newColour != was.trackColor || enabled->isChecked() != was.visible || locked->isChecked() != was.locked
                      || (bitmap->isVisible() || was.bitmap) && bitmap->isChecked() != was.bitmap || fineness->value() != was.bitmapResolution
                      || (was.holdsDrawings() && !was.showsSymbol() && artBits() != was.bitmapArt);
    if (!nameChanged && !rest)
        return;
    m_document->beginMacro(tr("Column Properties"));
    if (nameChanged)
        m_document->renameLayer(l, name->text().trimmed());
    if (rest) {
        bool const on = enabled->isChecked(), lock = locked->isChecked(), pixels = bitmap->isChecked() && was.holdsDrawings() && !was.showsSymbol();
        int const resolution = fineness->value();
        int const artPixels = was.holdsDrawings() && !was.showsSymbol() ? artBits() : was.bitmapArt;
        m_document->editLayers({l}, tr("Column Properties"), [newColour, on, lock, pixels, resolution, artPixels](Layer& layer) {
            layer.bitmapResolution = resolution;
            layer.bitmapArt = artPixels;
            layer.trackColor = newColour;
            layer.visible = on;
            layer.locked = lock;
            layer.bitmap = pixels;
        });
    }
    m_document->endMacro();
}
