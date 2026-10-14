// Eclipse - the main window: how it is put together and how its parts are wired to each other.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/PalettePanel.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/timeline/FunctionView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>

MainWindow::~MainWindow()
{
    // The views are taken apart after this: the keyboard focus moving among them, and events on the way to them,
    // are not to come here any more.
    disconnect(qApp, nullptr, this, nullptr);
    qApp->removeEventFilter(this);
    if (m_actions)
        m_actions->setActiveView(nullptr);
    // Nor are the views, panels and the document to tell anyone anything while they go (a dock hidden, a focus moved):
    // what would hear it may already be gone.
    for (QObject* const child : findChildren<QObject*>())
        child->blockSignals(true);
    // The list of things to undo is emptied as the document goes, and says so: no one is to hear it.
    if (m_document)
        m_document->undoStack()->blockSignals(true);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_actions = new ActionManager(this);
    QString error;
    if (!m_actions->load(ActionManager::defaultDirectory(), &error))
        QMessageBox::critical(nullptr, tr("Eclipse"), tr("The menus could not be loaded.\n%1").arg(error));

    m_document = new Document(this);
    createViews();
    createCollapseArrows();
    if (centralWidget())
        centralWidget()->installEventFilter(this);

    m_actions->registerResponder(QStringLiteral("window"), this);
    m_actions->registerResponder(QStringLiteral("camera"), m_canvas);
    m_actions->registerResponder(QStringLiteral("timelineView"), m_timeline);
    m_actions->registerResponder(QStringLiteral("paletteView"), m_palette);
    m_actions->registerResponder(QStringLiteral("xsheetView"), m_xsheet);
    m_actions->registerResponder(QStringLiteral("nodeView"), m_nodeView);
    m_actions->populateMenuBar(QStringLiteral("menu.main"), menuBar());
    createToolbars();
    m_actions->installShortcuts(this);

    // Right-click menus of the views.
    m_canvas->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_canvas, &QWidget::customContextMenuRequested, this, [this](QPoint const& pos) {
        if (QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.cameraView"), this)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(m_canvas->mapToGlobal(pos));
        }
    });
    connect(m_timeline, &Timeline::contextMenuRequested, this, [this](QPoint const& globalPos) {
        if (QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.timeline"), this)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(globalPos);
        }
    });

    connect(m_timeline, &Timeline::currentCellChanged, m_canvas, &Canvas::setCurrentCell);
    // The timeline holds the current cell; the other views follow it and ask it to change.
    connect(m_timeline, &Timeline::currentCellChanged, m_xsheet, &Xsheet::setCurrentCell);
    connect(m_timeline, &Timeline::currentCellChanged, m_layerPanel, &LayerPanel::setCurrentCell);
    connect(m_canvas, &Canvas::animateChanged, m_layerPanel, &LayerPanel::setAnimate);
    connect(m_timeline, &Timeline::currentCellChanged, m_substitution, &SubstitutionPanel::setCurrentCell);
    connect(m_xsheet, &Xsheet::cellPicked, m_timeline, &Timeline::setCurrentCell);
    connect(m_timeline, &Timeline::currentCellChanged, m_nodeView, &NodeView::setCurrentCell);
    connect(m_nodeView, &NodeView::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
    connect(m_nodeView, &NodeView::renameRequested, this, &MainWindow::renameLayer);
    connect(m_nodeView, &NodeView::contextMenuRequested, this, [this](QPoint const& globalPos) {
        if (QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.nodeView"), this)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(globalPos);
        }
    });
    connect(m_xsheet, &Xsheet::contextMenuRequested, this, [this](QPoint const& globalPos) {
        if (QMenu* const menu = m_actions->createMenu(QStringLiteral("menu.xsheet"), this)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(globalPos);
        }
    });
    connect(m_timeline, &Timeline::currentCellChanged, this, [this](int, int frame) {
        QSignalBlocker const block(m_frameBox), jog(m_jog);
        m_frameBox->setValue(frame + 1);
        m_jog->setValue(frame);
        updateStatus();
        // Sound scrubbing: the sound of the frame gone to, when it is not playing.
        if (m_scrub && !m_playTimer.isActive())
            playFrameSound(frame);
    });
    connect(m_document, &Document::changed, this, [this] { m_scrubMix.clear(); });
    connect(m_document, &Document::changed, this, &MainWindow::syncReferences);
    connect(m_timeline, &Timeline::addLayerMenuRequested, this, [this](QPoint const& globalPos) {
        QMenu menu(this);
        for (char const* id : {"InsertDrawing", "InsertPeg", "InsertCamera", "InsertColourCard", "InsertGroup", "InsertBone", "InsertQuadmap", "ImportSound"})
            if (QAction* const a = m_actions->action(QLatin1String(id)))
                menu.addAction(a);
        menu.exec(globalPos);
    });
    connect(m_timeline, &Timeline::deleteLayerRequested, this, &MainWindow::onActionDeleteLayer);
    connect(m_timeline, &Timeline::addPegRequested, this, &MainWindow::onActionAddPeg);
    connect(m_timeline, &Timeline::trackColourRequested, this, &MainWindow::pickTrackColour);
    connect(m_timeline, &Timeline::renameLayerRequested, this, &MainWindow::renameLayer);
    connect(m_canvas, &Canvas::layerPicked, this, [this](int l) { m_timeline->setCurrentCell(l, frame()); });
    connect(m_canvas, &Canvas::showControlChanged, this, [this](bool on) { m_actions->setChecked(QStringLiteral("ShowControl"), on); });
    connect(m_canvas, &Canvas::selectionChanged, this, &MainWindow::updateActionStates);
    connect(m_canvas, &Canvas::toolChanged, this, &MainWindow::updateActionStates);
    connect(m_timeline, &Timeline::currentCellChanged, this, &MainWindow::updateActionStates);
    connect(m_timeline, &Timeline::selectionChanged, this, &MainWindow::updateActionStates);
    connect(m_document, &Document::changed, this, &MainWindow::updateActionStates);
    // Another scene in hand (a symbol gone into or out of, by undo as well): nothing of the old one stays picked.
    connect(m_document, &Document::levelChanged, this, [this] {
        if (m_playTimer.isActive())
            stop();
        m_canvas->clearSelection();
        m_canvas->pickControlPoint(-1, -1);
        m_timeline->clearRange();
        Scene const& scene = m_document->scene();
        m_timeline->setCurrentCell(std::clamp(layer(), 0, std::max(0, int(scene.layers.size()) - 1)), std::clamp(frame(), 0, std::max(0, scene.frameCount() - 1)));
        updateTitle();
        updateActionStates();
    });
    updateActionStates();
    connect(m_canvas, &Canvas::colourPicked, this, [this](int swatch, QColor const& colour) {
        // The colour of the palette it was painted with; else one of the same colour; else
        // it joins the palette.
        Scene const& scene = m_document->scene();
        int id = scene.findSwatch(swatch) ? swatch : Scene::kNoSwatch;
        for (int i = 0; id == Scene::kNoSwatch && i < scene.palette.size(); ++i)
            if (scene.palette[i].color == colour)
                id = scene.palette[i].id;
        if (id == Scene::kNoSwatch)
            id = m_document->addSwatch(tr("Picked"), colour);
        m_palette->setCurrentSwatch(id);
        statusBar()->showMessage(tr("Colour: %1").arg(m_document->scene().findSwatch(id) ? m_document->scene().findSwatch(id)->name : colour.name()), 3000);
    });
    connect(m_canvas, &Canvas::toolChanged, this, [this](int tool) {
        static char const* const ids[] = {"ToolSelect", "ToolTransform", "ToolBrush",  "ToolEraser",    "ToolPaint",  "ToolHand",
                                          "ToolPencil", "ToolLine",      "ToolRectangle", "ToolEllipse", "ToolContourEditor",
                                          "ToolRigging", "ToolIK", "ToolDropper", "ToolZoom", "ToolPolyline",
                                          "ToolPaintUnpainted", "ToolRepaint", "ToolUnpaint", "ToolText", "ToolRotateView", "ToolCutter", "ToolStroke", "ToolCloseGap",
                                          "ToolEditGradient", "ToolReposition", "ToolStamp", "ToolPencilEditor", "ToolPerspective", "ToolInk", "ToolSmoothEditor", "ToolEnvelope", "ToolMorphing", "ToolRepaintBrush"};
        m_actions->setChecked(QLatin1String(ids[tool]), true);
        if (QAction* const a = m_actions->action(QLatin1String(ids[tool]))) {
            m_toolName = a->text().remove(QLatin1Char('&'));
            if (statusBar()->currentMessage().isEmpty())
                m_messageLabel->setText(m_toolName);
        }
    });
    connect(m_document, &Document::changed, this, [this] {
        Scene const& scene = m_document->scene();
        QSignalBlocker const a(m_frameBox), b(m_startBox), c(m_stopBox), d(m_jog), e(m_fpsBox);
        m_frameBox->setMaximum(scene.frameCount());
        m_jog->setRange(0, scene.frameCount() - 1);
        m_jog->setValue(frame());
        // Playing stops at the end of the scene unless it was told to stop sooner.
        if (m_playStop >= scene.frameCount() - 1)
            m_playStop = -1;
        m_playStart = std::min(m_playStart, scene.frameCount() - 1);
        m_startBox->setMaximum(scene.frameCount());
        m_stopBox->setMaximum(scene.frameCount());
        m_startBox->setValue(m_playStart + 1);
        m_stopBox->setValue(playStop() + 1);
        m_fpsBox->setEditText(QString::number(scene.fps, 'g', 6));
        m_timeline->setPlayRange(m_playStart, m_playStop);
        updateStatus();
    });
    connect(m_document->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    // The receiver is the action manager: when the window is torn down the undo stack still
    // signals, and by then the manager may be gone.
    connect(m_document->undoStack(), &QUndoStack::canUndoChanged, m_actions, [this](bool can) {
        if (QAction* const a = m_actions->action(QStringLiteral("EditUndo")))
            a->setEnabled(can);
    });
    connect(m_document->undoStack(), &QUndoStack::canRedoChanged, m_actions, [this](bool can) {
        if (QAction* const a = m_actions->action(QStringLiteral("EditRedo")))
            a->setEnabled(can);
    });
    connect(&m_playTimer, &QTimer::timeout, this, &MainWindow::advanceFrame);
    // Saving by itself (Edit > Preferences): a scene that has a file, has changed, and is not playing.
    connect(&m_autoSave, &QTimer::timeout, this, [this] {
        if (!m_path.isEmpty() && m_document->unsaved() && !m_playTimer.isActive())
            saveTo(m_path);
    });
    m_playTimer.setTimerType(Qt::PreciseTimer);

    // What would go to a status bar shows under the Camera view instead; a message stands
    // there for a while, then the name of the tool is back.
    statusBar()->hide();
    m_toolName = tr("Brush");
    m_messageLabel->setText(m_toolName);
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](QString const& text) {
        m_messageLabel->setText(text.isEmpty() ? m_toolName : text);
        m_messageLabel->setStyleSheet(text.isEmpty() ? QString() : QStringLiteral("color: #e6e6e6;"));
    });

    resize(1280, 800);
    m_document->reset(Scene());
    updateTitle();

    connect(m_palette, &PalettePanel::currentSwatchChanged, m_canvas, &Canvas::setSwatch);
    connect(m_palette, &PalettePanel::currentSwatchChanged, this, [this] { updateStatus(); });
    // The colour in hand goes with the kind of tool: the brush's, the pencil's, the paint's.
    connect(m_canvas, &Canvas::toolChanged, m_palette, [this](int tool) {
        Canvas::Tool const t = Canvas::Tool(tool);
        m_palette->setToolKind(t == Canvas::Brush ? 0 : Canvas::isPaintTool(t) ? 2
                               : t == Canvas::Pencil || t == Canvas::Line || t == Canvas::Rectangle || t == Canvas::Ellipse || t == Canvas::Polyline ? 1 : -1);
    });
    // The onion skin's reach, shown on the ruler of the Timeline and dragged there.
    auto onionToRuler = [this] { m_timeline->setOnion(m_canvas->onionSkin(), m_canvas->onionPrevious(), m_canvas->onionNext()); };
    connect(m_canvas, &Canvas::onionChanged, this, onionToRuler);
    connect(m_timeline, &Timeline::onionRangeChanged, m_canvas, &Canvas::setOnionRange);
    onionToRuler();
    m_canvas->setSwatch(m_palette->currentSwatch());
    connect(m_canvas, &Canvas::message, this, [this](QString const& text) { statusBar()->showMessage(text, 6000); });

    applyBuiltinWorkspace(m_workspace);
    refreshWorkspaceBox();
}

int MainWindow::layer() const
{
    return m_timeline->currentLayer();
}

int MainWindow::frame() const
{
    return m_timeline->currentFrame();
}
