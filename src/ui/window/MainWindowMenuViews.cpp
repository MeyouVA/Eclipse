// Eclipse - the main window: the views opened from the Windows menu, and how each is wired to the rest.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/NodeLibraryView.h"
#include "ui/views/GuidesView.h"
#include "ui/views/CoordView.h"
#include "ui/views/MetadataView.h"
#include "ui/views/PythonConsole.h"
#include "ui/views/ScriptView.h"
#include "ui/views/ControllerView.h"
#include "ui/views/Graph3DView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QUndoView>
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::onActionCameraCone(bool on)
{
    for (DepthView* view : findChildren<DepthView*>())
        view->setCameraCone(on);
    for (PerspectiveView* view : findChildren<PerspectiveView*>())
        view->setCameraCone(on);
    for (QToolButton* button : findChildren<QToolButton*>(QStringLiteral("PlanningShowCamera"))) {
        QSignalBlocker const quiet(button);
        button->setChecked(on);
    }
}

// A planning view with its strip underneath: Show/Hide Camera for that view alone, and Reset View.
QWidget* MainWindow::planningPanel(QWidget* view, std::function<void(bool)> const& showCamera, std::function<void()> const& resetView)
{
    QWidget* const panel = new QWidget(this);
    panel->setObjectName(view->objectName() + QStringLiteral("Panel"));
    QVBoxLayout* const column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(view, 1);
    QWidget* const strip = new QWidget(panel);
    strip->setObjectName(QStringLiteral("PlanningStrip"));
    QHBoxLayout* const across = new QHBoxLayout(strip);
    across->setContentsMargins(4, 2, 4, 2);
    QToolButton* const camera = new QToolButton(strip);
    camera->setObjectName(QStringLiteral("PlanningShowCamera"));
    camera->setText(tr("Camera"));
    camera->setToolTip(tr("Show/Hide Camera"));
    camera->setCheckable(true);
    camera->setChecked(true);
    camera->setFocusPolicy(Qt::NoFocus);
    connect(camera, &QToolButton::toggled, view, showCamera);
    QToolButton* const reset = new QToolButton(strip);
    reset->setObjectName(QStringLiteral("PlanningResetView"));
    reset->setText(tr("Reset View"));
    reset->setToolTip(tr("Reset View: the view back to where it starts"));
    reset->setFocusPolicy(Qt::NoFocus);
    connect(reset, &QToolButton::clicked, view, resetView);
    across->addWidget(camera);
    across->addWidget(reset);
    across->addStretch(1);
    column->addWidget(strip);
    return panel;
}

void MainWindow::createWindowsMenuViews()
{
    auto planningMenu = [this](QWidget* view, QString const& responder) {
        m_actions->registerResponder(responder, view);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this, [this, view](QPoint const& point) {
            view->setFocus();
            if (QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.scenePlanning"), this)) {
                if (!qobject_cast<PerspectiveView*>(view))
                    for (char const* id : {"LookAtSelected", "FaceSelectedToDraw", "MoveViewForward", "MoveViewBackward", "PerspectiveClippingPlanes"})
                        menu->removeAction(m_actions->action(QLatin1String(id)));
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->popup(view->mapToGlobal(point));
            }
        });
    };
    // Node Library: the kinds of layer and effect there are, to add from.
    {
        NodeLibraryView* const view = new NodeLibraryView(this);
        view->setObjectName(QStringLiteral("NodeLibraryView"));
        connect(view, &NodeLibraryView::actionRequested, this, [this](QString const& id) { m_actions->trigger(id); });
        connect(view, &NodeLibraryView::effectRequested, this, [this](int type) {
            Scene const& scene = m_document->scene();
            if (layer() < 0 || layer() >= scene.layers.size() || !scene.layers[layer()].holdsDrawings()) {
                statusBar()->showMessage(tr("An effect goes on a drawing layer: select one first."), 6000);
                return;
            }
            Effect const e = Effect::made(Effect::Type(std::clamp(type, 0, int(Effect::TypeCount) - 1)));
            QVector<Effect> effects = scene.layers[layer()].effects;
            effects.push_back(e);
            m_document->setEffects(layer(), effects, tr("Add Effect"));
        });
        addView(tr("Node Library"), QStringLiteral("NodeLibraryDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Guides: lines across the picture to draw and snap to.
    {
        GuidesView* const view = new GuidesView(m_document, m_canvas, this);
        view->setObjectName(QStringLiteral("GuidesView"));
        addView(tr("Guides"), QStringLiteral("GuidesDock"), scrollingView(view), Qt::RightDockWidgetArea)->hide();
    }
    // Coord. and Control Points: the keyframes of the current layer as numbers.
    {
        CoordView* const view = new CoordView(m_document, this);
        view->setObjectName(QStringLiteral("CoordView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, &CoordView::setCurrentCell);
        connect(view, &CoordView::framePicked, m_timeline, &Timeline::setCurrentFrame);
        addView(tr("Coord. and Control Points"), QStringLiteral("CoordDock"), view, Qt::BottomDockWidgetArea)->hide();
    }
    // Metadata: names and values kept with the scene or a layer.
    {
        MetadataView* const view = new MetadataView(m_document, this);
        view->setObjectName(QStringLiteral("MetadataView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, &MetadataView::setCurrentCell);
        addView(tr("Metadata"), QStringLiteral("MetadataDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    {
        ControllerView* const view = new ControllerView(m_document, m_timeline, this);
        view->setCameraHost(m_canvas);
        view->setObjectName(QStringLiteral("ControllerView"));
        addView(tr("Pose Controllers"), QStringLiteral("ControllerDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Script Editor: scripts written, kept and run on the scene.
    {
        ScriptView* const view = new ScriptView(m_document, m_timeline, this);
        view->setObjectName(QStringLiteral("ScriptView"));
        addView(tr("Script Editor"), QStringLiteral("ScriptDock"), view, Qt::BottomDockWidgetArea)->hide();
        connect(view, &ScriptView::scriptsChanged, this, &MainWindow::refreshScriptCommands);
        refreshScriptCommands();
    }
    // Undo List: every step that can be undone (and done again), the current one picked; picking another goes there.
    {
        QUndoView* const view = new QUndoView(m_document->undoStack(), this);
        view->setObjectName(QStringLiteral("UndoList"));
        view->setEmptyLabel(tr("<the scene as it was opened>"));
        addView(tr("Undo List"), QStringLiteral("UndoListDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Python Console: lines of Python run on the scene, by the Python installed on the computer.
    {
        PythonConsole* const view = new PythonConsole(m_document, m_timeline, this);
        view->setObjectName(QStringLiteral("PythonConsole"));
        addView(tr("Python Console"), QStringLiteral("PythonDock"), view, Qt::BottomDockWidgetArea)->hide();
    }
    // 3D Graph: the layers as they hang together, with where each stands in space.
    {
        Graph3DView* const view = new Graph3DView(m_document, this);
        view->setObjectName(QStringLiteral("Graph3DView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, &Graph3DView::setCurrentCell);
        connect(view, &Graph3DView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
        addView(tr("3D Graph"), QStringLiteral("Graph3DDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Preview Manager: the frames rendered once, to play from.
    {
        PreviewManagerView* const view = new PreviewManagerView(m_document, m_canvas, this);
        view->setObjectName(QStringLiteral("PreviewManagerView"));
        addView(tr("Preview Manager"), QStringLiteral("PreviewManagerDock"), scrollingView(view), Qt::RightDockWidgetArea)->hide();
    }
    // Lip-Sync: the sound frame by frame with the shape of the mouth on each, and the chart of shapes.
    {
        LipSyncView* const view = new LipSyncView(m_document, this);
        view->setObjectName(QStringLiteral("LipSyncView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, &LipSyncView::setCurrentCell);
        connect(view, &LipSyncView::framePicked, m_timeline, &Timeline::setCurrentFrame);
        // A frame picked there is heard, so that the mouth can be matched to the sound - unless scrubbing has played it already.
        connect(view, &LipSyncView::framePicked, this, [this](int f) {
            if (!m_scrub && !m_playTimer.isActive())
                playFrameSound(f);
        });
        connect(view, &LipSyncView::mouthChosen, this, &MainWindow::onActionMouthShape);
        connect(view, &LipSyncView::detectRequested, this, &MainWindow::onActionAutoLipSync);
        connect(view, &LipSyncView::mapRequested, this, &MainWindow::onActionMapLipSync);
        connect(view, &LipSyncView::clearRequested, this, &MainWindow::onActionClearLipSync);
        addView(tr("Lip-Sync"), QStringLiteral("LipSyncDock"), view, Qt::BottomDockWidgetArea)->hide();
    }
    // Perspective: the layers as sheets standing in space, to turn about and look at.
    {
        PerspectiveView* const view = new PerspectiveView(m_document, this);
        view->setObjectName(QStringLiteral("PerspectiveView"));
        view->setDrawingCanvas(m_canvas);
        connect(view, &PerspectiveView::message, this, [this](QString const& text) { statusBar()->showMessage(text, 8000); });
        connect(m_timeline, &Timeline::currentCellChanged, view, [this, view](int l, int f) {
            if (!m_playTimer.isActive() || playsIn(QStringLiteral("perspective")))
                view->setCurrentCell(l, f);
        });
        connect(view, &PerspectiveView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
        planningMenu(view, QStringLiteral("perspectiveView"));
        QWidget* const panel = planningPanel(view, [view](bool on) { view->setCameraCone(on); }, [view] { view->onActionResetView(); });
        addView(tr("Perspective"), QStringLiteral("PerspectiveViewDock"), panel, Qt::RightDockWidgetArea)->hide();
    }
    // Top and Side: the scene from above and from the side, to set how far each layer stands from the camera.
    for (DepthView::Mode mode : {DepthView::Top, DepthView::Side}) {
        DepthView* const view = new DepthView(m_document, mode, this);
        view->setAnimationCanvas(m_canvas);
        connect(view, &DepthView::message, this, [this](QString const& text) { statusBar()->showMessage(text, 6000); });
        view->setObjectName(mode == DepthView::Top ? QStringLiteral("TopView") : QStringLiteral("SideView"));
        planningMenu(view, mode == DepthView::Top ? QStringLiteral("topView") : QStringLiteral("sideView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, [this, view, mode](int l, int f) {
            if (!m_playTimer.isActive() || playsIn(mode == DepthView::Top ? QStringLiteral("top") : QStringLiteral("side")))
                view->setCurrentCell(l, f);
        });
        connect(view, &DepthView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
        QWidget* const panel = planningPanel(view, [view](bool on) { view->setCameraCone(on); }, [view] { view->onActionResetView(); });
        addView(mode == DepthView::Top ? tr("Top") : tr("Side"), mode == DepthView::Top ? QStringLiteral("TopViewDock") : QStringLiteral("SideViewDock"), panel,
             Qt::RightDockWidgetArea)->hide();
    }
    m_playViews = settings().value(QStringLiteral("play/views")).toStringList();
    m_actions->setChecked(QStringLiteral("PlayInTop"), playsIn(QStringLiteral("top")));
    m_actions->setChecked(QStringLiteral("PlayInSide"), playsIn(QStringLiteral("side")));
    m_actions->setChecked(QStringLiteral("PlayInPerspective"), playsIn(QStringLiteral("perspective")));
    // Function: the animated values of the current layer as curves.
    FunctionView* const function = new FunctionView(m_document, this);
    function->setObjectName(QStringLiteral("FunctionView"));
    connect(m_timeline, &Timeline::currentCellChanged, function, [this, function](int layer, int frame) {
        // (Another current layer changes what is selected without the Timeline saying so.)
        if (layer != function->graph()->currentLayer())
            function->setOtherLayers(m_timeline->selectedLayers());
        function->setCurrentCell(layer, frame);
    });
    connect(m_timeline, &Timeline::selectionChanged, function, [this, function] { function->setOtherLayers(m_timeline->selectedLayers()); });
    connect(function, &FunctionView::framePicked, m_timeline, &Timeline::setCurrentFrame);
    addView(tr("Function"), QStringLiteral("FunctionDock"), function, Qt::BottomDockWidgetArea)->hide();
}

// Scripts: the scripts kept by the Script Editor, each run on the scene by a click; and the editor itself.
void MainWindow::onShowScripts(QMenu* menu)
{
    menu->clear();
    ScriptView* const view = findChild<ScriptView*>(QStringLiteral("ScriptView"));
    if (!view)
        return;
    QStringList const names = view->names();
    for (QString const& name : names) {
        // (Its shortcut is shown; the shortcut itself belongs to the window - see refreshScriptCommands.)
        QKeySequence const key = view->shortcutOf(name);
        QString const text = key.isEmpty() ? name : name + QLatin1Char('\t') + key.toString(QKeySequence::NativeText);
        connect(menu->addAction(text), &QAction::triggered, this, [this, name] { runScript(name); });
    }
    if (names.isEmpty())
        menu->addAction(tr("(No scripts yet)"))->setEnabled(false);
    menu->addSeparator();
    connect(menu->addAction(tr("Script Editor...")), &QAction::triggered, this, [this] {
        if (QDockWidget* const dock = findChild<QDockWidget*>(QStringLiteral("ScriptDock")))
            showView(dock);
    });
}

void MainWindow::runScript(QString const& name)
{
    ScriptView* const view = findChild<ScriptView*>(QStringLiteral("ScriptView"));
    if (!view)
        return;
    QString const said = view->runSaved(name);
    statusBar()->showMessage(said.isEmpty() ? tr("Script %1 run.").arg(name) : said.section(QLatin1Char('\n'), -1), 8000);
}

// The scripts given a shortcut of their own, and those with a button on the Scripts toolbar: made again whenever they change.
void MainWindow::refreshScriptCommands()
{
    ScriptView* const view = findChild<ScriptView*>(QStringLiteral("ScriptView"));
    if (!view)
        return;
    for (QAction* const old : findChildren<QAction*>(QStringLiteral("ScriptCommand"), Qt::FindDirectChildrenOnly)) {
        removeAction(old);
        delete old;
    }
    if (!m_scriptBar) {
        m_scriptBar = new QToolBar(tr("Scripts"), this);
        m_scriptBar->setObjectName(QStringLiteral("ScriptsToolbar"));
        addToolBar(Qt::TopToolBarArea, m_scriptBar);
    }
    m_scriptBar->clear();
    for (QString const& name : view->names()) {
        QKeySequence const key = view->shortcutOf(name);
        bool const button = view->onToolbar(name);
        if (key.isEmpty() && !button)
            continue;
        QAction* const a = new QAction(name, this);
        a->setObjectName(QStringLiteral("ScriptCommand"));
        a->setData(name);
        a->setToolTip(key.isEmpty() ? tr("Runs the script %1").arg(name) : tr("Runs the script %1 (%2)").arg(name, key.toString(QKeySequence::NativeText)));
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::ApplicationShortcut);
            addAction(a);
        }
        if (button)
            m_scriptBar->addAction(a);
        connect(a, &QAction::triggered, this, [this, name] { runScript(name); });
    }
    m_scriptBar->setVisible(!m_scriptBar->actions().isEmpty());
}
