// Eclipse - the main window: the views opened from the Windows menu, and how each is wired to the rest.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
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
#include "ui/views/ScriptView.h"
#include "ui/views/ControllerView.h"
#include "ui/views/Graph3DView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QStatusBar>
#include <QStatusBar>

// The views opened from the Windows menu that are classes of their own: each is made, wired to the
// Timeline and the commands, and put away until it is asked for.
void MainWindow::createWindowsMenuViews()
{
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
            Effect e;
            e.type = Effect::Type(std::clamp(type, 0, 5));
            e.amount = e.type == Effect::Tint ? 0.5 : e.type == Effect::Shadow ? 6.0 : e.type == Effect::Particles ? 2.0 : 8.0;
            e.color = e.type == Effect::Shadow ? QColor(0, 0, 0, 150) : e.type == Effect::Glow ? QColor(255, 240, 160) : QColor(40, 110, 200);
            e.offset = e.type == Effect::Shadow ? QPointF(10, 10) : QPointF();
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
    // (Scripts > the scripts kept, each run on a click: see onShowScripts.)
    // Pose Controllers: poses of a set of layers kept under names, and a slider between them.
    {
        ControllerView* const view = new ControllerView(m_document, m_timeline, this);
        view->setObjectName(QStringLiteral("ControllerView"));
        addView(tr("Pose Controllers"), QStringLiteral("ControllerDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Script Editor: scripts written, kept and run on the scene.
    {
        ScriptView* const view = new ScriptView(m_document, m_timeline, this);
        view->setObjectName(QStringLiteral("ScriptView"));
        addView(tr("Script Editor"), QStringLiteral("ScriptDock"), view, Qt::BottomDockWidgetArea)->hide();
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
        connect(m_timeline, &Timeline::currentCellChanged, view, [this, view](int l, int f) {
            if (!m_playTimer.isActive() || playsIn(QStringLiteral("perspective")))
                view->setCurrentCell(l, f);
        });
        connect(view, &PerspectiveView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
        addView(tr("Perspective"), QStringLiteral("PerspectiveViewDock"), view, Qt::RightDockWidgetArea)->hide();
    }
    // Top and Side: the scene from above and from the side, to set how far each layer stands from the camera.
    for (DepthView::Mode mode : {DepthView::Top, DepthView::Side}) {
        DepthView* const view = new DepthView(m_document, mode, this);
        view->setObjectName(mode == DepthView::Top ? QStringLiteral("TopView") : QStringLiteral("SideView"));
        connect(m_timeline, &Timeline::currentCellChanged, view, [this, view, mode](int l, int f) {
            if (!m_playTimer.isActive() || playsIn(mode == DepthView::Top ? QStringLiteral("top") : QStringLiteral("side")))
                view->setCurrentCell(l, f);
        });
        connect(view, &DepthView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
        addView(mode == DepthView::Top ? tr("Top") : tr("Side"), mode == DepthView::Top ? QStringLiteral("TopViewDock") : QStringLiteral("SideViewDock"), view,
             Qt::RightDockWidgetArea)->hide();
    }
    m_playViews = settings().value(QStringLiteral("play/views")).toStringList();
    m_actions->setChecked(QStringLiteral("PlayInTop"), playsIn(QStringLiteral("top")));
    m_actions->setChecked(QStringLiteral("PlayInSide"), playsIn(QStringLiteral("side")));
    m_actions->setChecked(QStringLiteral("PlayInPerspective"), playsIn(QStringLiteral("perspective")));
    // Function: the animated values of the current layer as curves.
    FunctionView* const function = new FunctionView(m_document, this);
    function->setObjectName(QStringLiteral("FunctionView"));
    connect(m_timeline, &Timeline::currentCellChanged, function, &FunctionView::setCurrentCell);
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
        connect(menu->addAction(name), &QAction::triggered, this, [this, view, name] {
            if (!view->open(name))
                return;
            QFile file(view->folder() + QLatin1Char('/') + name + QStringLiteral(".js"));
            if (!file.open(QIODevice::ReadOnly))
                return;
            QString const said = view->run(QString::fromUtf8(file.readAll()), name);
            statusBar()->showMessage(said.isEmpty() ? tr("Script %1 run.").arg(name) : said.section(QLatin1Char('\n'), -1), 8000);
        });
    }
    if (names.isEmpty())
        menu->addAction(tr("(No scripts yet)"))->setEnabled(false);
    menu->addSeparator();
    connect(menu->addAction(tr("Script Editor...")), &QAction::triggered, this, [this] {
        if (QDockWidget* const dock = findChild<QDockWidget*>(QStringLiteral("ScriptDock")))
            showView(dock);
    });
}
