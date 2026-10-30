// Eclipse - the main window: adding, deleting, duplicating and tagging layers; the Timeline's view modes.
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
        // (Preferences: the line art of a new layer in pixels to begin with.)
        bool const pixels = canBitmap && qstrcmp(objectName, "LayerLineArt") == 0 && QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/lineArtBitmap"), false).toBool();
        vector->setChecked(!pixels);
        bitmap->setChecked(pixels);
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
    QSpinBox* const number = new QSpinBox(&dialog);
    number->setObjectName(QStringLiteral("LayerNumber"));
    number->setRange(1, 100);
    number->setToolTip(tr("How many layers are added: the name of each after the first ends in a number."));
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Number:"), number);
    form->addRow(tr("Line Art"), kinds("LayerLineArt", true));
    form->addRow(tr("Colour Art"), kinds("LayerColourArt", false));
    form->addRow(tr("Overlay Art"), kinds("LayerOverlayArt", true));
    form->addRow(tr("Underlay Art"), kinds("LayerUnderlayArt", true));
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
    auto addOne = [this, name, number, wholeScene, &dialog] {
        QRadioButton* const bitmap = dialog.findChild<QRadioButton*>(QStringLiteral("LayerLineArtBitmap"));
        m_document->beginMacro(tr("Add Drawing Layer"));
        for (int n = 0; n < number->value(); ++n) {
            int const at = addDrawingLayer(n == 0 ? name->text() : QStringLiteral("%1_%2").arg(name->text().trimmed()).arg(n + 1), wholeScene->isChecked());
            if (bitmap && bitmap->isChecked())
                m_document->setLayerBitmap(at, true);
            // Overlay and underlay art: vector or bitmap of their own.
            int sheets = 0;
            if (QRadioButton* const over = dialog.findChild<QRadioButton*>(QStringLiteral("LayerOverlayArtBitmap")); over && over->isChecked())
                sheets |= 1 << Drawing::OverlaySheet;
            if (QRadioButton* const under = dialog.findChild<QRadioButton*>(QStringLiteral("LayerUnderlayArtBitmap")); under && under->isChecked())
                sheets |= 1 << Drawing::UnderlaySheet;
            if (sheets)
                m_document->editLayers({at}, tr("Add Drawing Layer"), [sheets](Layer& l) { l.bitmapArt |= sheets; });
        }
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
    // (Preferences > General: Ask before deleting layers.)
    if (QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/confirmLayerDelete"), false).toBool()
        && QMessageBox::question(this, tr("Delete Layers"), tr("You are about to delete Layer(s)\nPlease Confirm.")) != QMessageBox::Yes)
        return;
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
    // (From the Camera view, with nothing picked in the Timeline: the current layer.)
    if (layers.isEmpty() && (on || how == QLatin1String("untag")) && layer() >= 0 && layer() < m_document->scene().layers.size())
        layers.push_back(layer());
    if (how == QLatin1String("untagAll") || how == QLatin1String("untagOthers")) {
        QVector<int> const keep = how == QLatin1String("untagOthers") ? layers : QVector<int>();
        layers.clear();
        for (int l = 0; l < m_document->scene().layers.size(); ++l)
            if (m_document->scene().layers[l].tagged && !keep.contains(l))
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
    m_timeline->setViewMode(mode == QLatin1String("selected") ? Timeline::ShowSelected : mode == QLatin1String("tagged") ? Timeline::ShowTagged
                            : mode == QLatin1String("controlled") ? Timeline::ShowControlled
                            : mode == QLatin1String("group")      ? Timeline::ShowSameGroup
                                                                  : Timeline::ShowAll);
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
        {"others", "TimelineShowOthers", {int(Layer::ColourCard), int(Layer::Bone), int(Layer::Quadmap), int(Layer::Annotation)}},
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
