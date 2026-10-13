// Eclipse - the main window: the layer commands - add, delete, lock, colour, rename, move, attach.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include <QCheckBox>
#include <QDialog>
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
    auto kinds = [&dialog, this](char const* objectName) {
        QWidget* const row = new QWidget(&dialog);
        row->setObjectName(QLatin1String(objectName));
        QRadioButton* const vector = new QRadioButton(tr("Vector"), row);
        QRadioButton* const bitmap = new QRadioButton(tr("Bitmap"), row);
        vector->setChecked(true);
        bitmap->setEnabled(false);
        bitmap->setToolTip(tr("Eclipse draws in vectors; pictures are brought in with File > Import."));
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
    form->addRow(tr("Line Art"), kinds("LayerLineArt"));
    form->addRow(tr("Colour Art"), kinds("LayerColourArt"));
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
    auto addOne = [this, name, wholeScene] {
        addDrawingLayer(name->text(), wholeScene->isChecked());
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
