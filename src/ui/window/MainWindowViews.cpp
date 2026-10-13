// Eclipse - the main window: its views put in their panels; the Camera view and the Timeline with their toolbars.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

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
#include "ui/window/WelcomeDialog.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include "ui/panels/ToolProperties.h"

// ---- views and toolbars -------------------------------------------------------------------
// Each view sits in a panel under the toolbar that belongs to it.

// The frame round a view: its outline is lit while the view is the one in use.
namespace {
class ViewFrame : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setPen(property("active").toBool() ? QColor(0xc8, 0x28, 0x1e) : QColor(0x2a, 0x2a, 0x2a));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};
}  // namespace

static QFrame* viewFrame(QWidget* content, QWidget* parent)
{
    QFrame* const frame = new ViewFrame(parent);
    frame->setObjectName(QStringLiteral("ViewFrame"));
    QVBoxLayout* const layout = new QVBoxLayout(frame);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(content);
    return frame;
}

static QWidget* viewPanel(QToolBar* toolbar, QWidget* view, QWidget* parent)
{
    QWidget* const panel = new QWidget(parent);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (toolbar) {
        toolbar->setIconSize(QSize(24, 24));
        layout->addWidget(toolbar);
    }
    layout->addWidget(view, 1);
    return panel;
}

// A view in a panel of its own, which can be dragged about, tabbed with others, or floated.
QDockWidget* MainWindow::addView(QString const& title, QString const& name, QWidget* content, Qt::DockWidgetArea area)
{
    QDockWidget* const d = new QDockWidget(title, this);
    d->setObjectName(name);
    d->setWidget(viewFrame(content, d));
    addDockWidget(area, d);
    m_docks << d;
    // Wherever a view goes, the tabs of the panels follow.
    auto stale = [this] {
        if (m_tabsStale)
            return;
        m_tabsStale = true;
        QTimer::singleShot(0, m_actions, [this] { refreshViewTabs(); });
    };
    connect(d, &QDockWidget::dockLocationChanged, m_actions, stale);
    connect(d, &QDockWidget::topLevelChanged, m_actions, stale);
    connect(d, &QDockWidget::visibilityChanged, m_actions, stale);
    return d;
}

void MainWindow::createViews()
{
    createCameraView();
    createTimelineView();

    ToolProperties* const properties = new ToolProperties(m_document, m_canvas, m_actions, m_envelopePoints, this);
    connect(properties, &ToolProperties::envelopePointsChanged, this, [this](int count) { m_envelopePoints = count; });
    connect(properties, &ToolProperties::envelopeOutlineChanged, this, [this](bool on) { m_envelopeOutline = on; });
    m_refreshToolProperties = [properties] { properties->refresh(); };
    addView(tr("Tool Properties"), QStringLiteral("ToolPropertiesDock"), scrollingView(properties), Qt::RightDockWidgetArea);

    m_layerPanel = new LayerPanel(m_document, this);
    addView(tr("Layer Properties"), QStringLiteral("LayerPropertiesDock"), scrollingView(m_layerPanel), Qt::RightDockWidgetArea);

    m_substitution = new SubstitutionPanel(m_document, this);
    addView(tr("Drawing Substitution"), QStringLiteral("DrawingSubstitutionDock"), m_substitution, Qt::RightDockWidgetArea);

    m_palette = new PalettePanel(m_document, this);
    addView(tr("Colour"), QStringLiteral("ColourDock"), m_palette, Qt::RightDockWidgetArea);

    m_xsheet = new Xsheet(m_document, this);
    addView(tr("Xsheet"), QStringLiteral("XsheetDock"), m_xsheet, Qt::RightDockWidgetArea);

    // ---- views that are not in the usual arrangement: opened from the Windows menu ----
    createWindowsMenuViews();
    addView(tr("Onion Skin"), QStringLiteral("OnionSkinDock"), scrollingView(createOnionSkinView()), Qt::RightDockWidgetArea)->hide();

    // Library: templates kept in folders, put into the scene by a double click or by dragging.
    m_library = new LibraryView(this);
    connect(m_library, &LibraryView::insertRequested, this, [this](QString const& path) { dropTemplate(path, frame()); });
    connect(m_library, &LibraryView::createRequested, this, &MainWindow::onActionCreateTemplate);
    connect(m_timeline, &Timeline::templateDropped, this, &MainWindow::dropTemplate);
    connect(m_canvas, &Canvas::templateDropped, this, [this](QString const& path) { dropTemplate(path, frame()); });
    addView(tr("Library"), QStringLiteral("LibraryDock"), m_library, Qt::RightDockWidgetArea)->hide();

    // Reference: pictures to look at while drawing (File > Import > To Reference View).
    addView(tr("Reference"), QStringLiteral("ReferenceDock"), makeReferenceView(), Qt::RightDockWidgetArea)->hide();

    addView(tr("Message Log"), QStringLiteral("MessageLogDock"), createMessageLogView(), Qt::RightDockWidgetArea)->hide();
    addView(tr("Scene Markers"), QStringLiteral("SceneMarkersDock"), createSceneMarkersView(), Qt::RightDockWidgetArea)->hide();

    m_nodeView = new NodeView(m_document, this);
    addView(tr("Node View"), QStringLiteral("NodeViewDock"), m_nodeView, Qt::RightDockWidgetArea);

    // Views dropped on one another share a frame, with a tab each.
    setDockNestingEnabled(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    // The views along the bottom run the whole width of the window.
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    for (QDockWidget* d : std::as_const(m_docks)) {
        ViewTitleBar* const bar = new ViewTitleBar(this, d, &m_docks);
        d->setTitleBarWidget(bar);
        m_titleBars << bar;
        // The views that have a menu of their own show it from the menu button of their strip.
        QString const own = d->windowTitle() == tr("Timeline") ? QStringLiteral("menu.timeline")
                            : d->windowTitle() == tr("Xsheet") ? QStringLiteral("menu.xsheet")
                            : d->windowTitle() == tr("Node View") ? QStringLiteral("menu.nodeView") : QString();
        if (!own.isEmpty())
            bar->setViewMenu([this, own] { return m_actions->createMenu(own, this); });
    }
    qApp->installEventFilter(this);
}

// Camera: the middle of the window.
void MainWindow::createCameraView()
{
    m_canvas = new Canvas(m_document, this);
    // The Camera view stays in the middle; it has the same strip of tabs as the rest.
    QWidget* const camera = new QWidget(this);
    QVBoxLayout* const layout = new QVBoxLayout(camera);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    ViewTitleBar* const bar = new ViewTitleBar(tr("Camera"), camera);
    bar->setObjectName(QStringLiteral("CameraTitleBar"));
    bar->setViewMenu([this] { return m_actions->createMenu(QStringLiteral("menu.cameraView"), this); });
    bar->setFixedHeight(26);
    // The view is the Camera view or the Drawing view: a tab for each.
    bar->setTitles({tr("Camera"), tr("Drawing")}, 0);
    connect(bar, &ViewTitleBar::titleChosen, m_canvas, [this](int which) { m_canvas->setDrawingView(which == 1); });
    connect(m_canvas, &Canvas::drawingViewChanged, bar, [bar, this](bool on) {
        bar->setTitles({tr("Camera"), tr("Drawing")}, on ? 1 : 0);
        statusBar()->showMessage(on ? tr("Drawing view: the drawing alone, flat. Turn on the light table to see the other layers.") : tr("Camera view"), 4000);
    });
    layout->addWidget(bar);
    // The view, with a narrow strip down its right edge: both sheets of the drawing shown
    // or only the one worked on, and which of the two that is.
    QWidget* const viewAndStrip = new QWidget(this);
    {
        QWidget* const strip = new QWidget(viewAndStrip);
        strip->setObjectName(QStringLiteral("ArtStrip"));
        strip->setAttribute(Qt::WA_StyledBackground, true);
        strip->setFixedWidth(30);
        QVBoxLayout* const column = new QVBoxLayout(strip);
        column->setContentsMargins(2, 4, 2, 4);
        column->setSpacing(2);
        for (char const* id : {"PreviewAllArt", "SetOverlayArt", "SetLineArt", "SetColourArt", "SetUnderlayArt"}) {
            if (QAction* const a = m_actions->action(QLatin1String(id))) {
                QToolButton* const b = new QToolButton(strip);
                b->setObjectName(QStringLiteral("Art") + QLatin1String(id));
                b->setDefaultAction(a);
                b->setAutoRaise(true);
                b->setIconSize(QSize(20, 20));
                column->addWidget(b);
            }
        }
        column->addStretch(1);
        QHBoxLayout* const across = new QHBoxLayout(viewAndStrip);
        across->setContentsMargins(0, 0, 0, 0);
        across->setSpacing(0);
        across->addWidget(m_canvas, 1);
        across->addWidget(strip);
    }
    connect(m_canvas, &Canvas::showGuidesChanged, this, [this](bool on) { m_actions->setChecked(QStringLiteral("ShowGuides"), on); });
    connect(m_canvas, &Canvas::artChanged, this, [this](int art) {
        m_actions->setChecked(art == Canvas::ColourArt ? QStringLiteral("SetColourArt") : art == Canvas::OverlayArt ? QStringLiteral("SetOverlayArt")
                              : art == Canvas::UnderlayArt ? QStringLiteral("SetUnderlayArt") : QStringLiteral("SetLineArt"), true);
        if (QAction* const a = m_actions->action(QStringLiteral("SwitchArt")))
            a->setText(art == Canvas::ColourArt ? tr("Switch To Line Art") : tr("Switch To Colour Art"));
        updateStatus();
    });
    QWidget* const panel = viewPanel(m_actions->createToolbar(QStringLiteral("CameraViewToolbar"), this), viewAndStrip, this);
    // Along the bottom of the view: how close it is, the tool in use (or a word to
    // the user), and where in the scene one is.  The window has no status bar of its own.
    QWidget* const status = new QWidget(panel);
    status->setObjectName(QStringLiteral("CameraStatusBar"));
    status->setAttribute(Qt::WA_StyledBackground, true);
    status->setFixedHeight(28);
    m_zoomLabel = new QToolButton(status);
    m_zoomLabel->setObjectName(QStringLiteral("ZoomLabel"));
    m_zoomLabel->setMinimumWidth(64);
    m_zoomLabel->setAutoRaise(true);
    m_zoomLabel->setPopupMode(QToolButton::InstantPopup);
    m_zoomLabel->setToolTip(tr("Zoom"));
    {
        QMenu* const zoomMenu = new QMenu(m_zoomLabel);
        connect(zoomMenu->addAction(tr("Fit to View")), &QAction::triggered, this, [this] { m_canvas->fitToWindow(); });
        zoomMenu->addSeparator();
        for (int percent : {25, 50, 75, 100, 150, 200, 400, 800})
            connect(zoomMenu->addAction(QStringLiteral("%1%").arg(percent)), &QAction::triggered, this,
                    [this, percent] { m_canvas->zoomBy(percent / 100.0 / m_canvas->zoom()); });
        m_zoomLabel->setMenu(zoomMenu);
    }
    m_messageLabel = new QLabel(status);
    m_messageLabel->setObjectName(QStringLiteral("MessageLabel"));
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel = new QLabel(status);
    m_statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    m_drawingLabel = new QLabel(status);
    m_drawingLabel->setObjectName(QStringLiteral("DrawingLabel"));
    m_colourLabel = new QLabel(status);
    m_colourLabel->setObjectName(QStringLiteral("ColourLabel"));
    m_colourPatch = new QLabel(status);
    m_colourPatch->setObjectName(QStringLiteral("ColourPatch"));
    m_colourPatch->setFixedSize(12, 12);
    QHBoxLayout* const row = new QHBoxLayout(status);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(1);
    for (char const* id : {"LightTable", "CurrentDrawingOnTop", "CameraMask", "ShowSafeArea"}) {
        if (QAction* const a = m_actions->action(QLatin1String(id))) {
            QToolButton* const b = new QToolButton(status);
            b->setObjectName(QStringLiteral("Status") + QLatin1String(id));
            b->setDefaultAction(a);
            b->setAutoRaise(true);
            b->setIconSize(QSize(18, 18));
            row->addWidget(b);
        }
    }
    row->addSpacing(8);
    row->addWidget(m_zoomLabel);
    row->addSpacing(8);
    row->addWidget(m_drawingLabel);
    row->addWidget(m_messageLabel, 1);
    row->addWidget(m_statusLabel);
    row->addSpacing(14);
    row->addWidget(m_colourLabel);
    row->addSpacing(4);
    row->addWidget(m_colourPatch);
    // Where the pointer is, in fields: twelve from the middle of the picture to its top, and
    // four thirds as wide as they are high (the 4:3 field chart of the animation stand).
    connect(m_canvas, &Canvas::pointerMoved, this, [this](QPointF scenePos) {
        Scene const& scene = m_document->scene();
        double const field = scene.height / 24.0;
        if (field <= 0.0)
            return;
        m_pointer = QPointF((scenePos.x() - scene.width / 2.0) / (field * 4.0 / 3.0), -(scenePos.y() - scene.height / 2.0) / field);
        m_pointerKnown = true;
        updateStatus();
    });
    panel->layout()->addWidget(status);
    connect(m_canvas, &Canvas::zoomChanged, this, [this] { m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100.0))); });
    layout->addWidget(viewFrame(panel, camera), 1);
    setCentralWidget(camera);
}

void MainWindow::createTimelineView()
{
    m_timeline = new Timeline(m_document, this);
    // Over the timeline: playing (with the frame, where playing starts and stops, and
    // the speed), then the commands of the view itself.
    QToolBar* const playback = m_actions->createToolbar(QStringLiteral("PlaybackToolbar"), this);
    m_jog = new QSlider(Qt::Horizontal, this);
    m_jog->setObjectName(QStringLiteral("Jog"));
    m_jog->setFixedWidth(70);
    m_jog->setToolTip(tr("Drag to move through the frames"));
    connect(m_jog, &QSlider::valueChanged, this, [this](int v) { m_timeline->setCurrentFrame(v); });
    auto box = [this](char const* name) {
        QSpinBox* const b = new QSpinBox(this);
        b->setObjectName(QLatin1String(name));
        b->setRange(1, 24);
        b->setKeyboardTracking(false);
        return b;
    };
    m_frameBox = box("Frame");
    m_frameBox->setKeyboardTracking(true);
    m_startBox = box("Start");
    m_stopBox = box("Stop");
    connect(m_frameBox, &QSpinBox::valueChanged, this, [this](int v) { m_timeline->setCurrentFrame(v - 1); });
    connect(m_startBox, &QSpinBox::valueChanged, this, [this](int v) { setPlayRange(v - 1, std::max(v - 1, playStop())); });
    connect(m_stopBox, &QSpinBox::valueChanged, this, [this](int v) { setPlayRange(std::min(m_playStart, v - 1), v - 1); });
    m_fpsBox = new QComboBox(this);
    m_fpsBox->setObjectName(QStringLiteral("FPS"));
    m_fpsBox->setEditable(true);
    m_fpsBox->setInsertPolicy(QComboBox::NoInsert);
    for (char const* rate : {"12", "15", "23.976", "24", "25", "29.97", "30", "48", "50", "60"})
        m_fpsBox->addItem(QLatin1String(rate));
    m_fpsBox->setFixedWidth(72);
    auto applyFps = [this] {
        Scene const& scene = m_document->scene();
        bool ok = false;
        double const fps = m_fpsBox->currentText().toDouble(&ok);
        if (ok && fps >= 1.0 && fps <= 240.0 && fps != scene.fps)
            m_document->setSceneSettings(scene.width, scene.height, fps);
        else
            m_fpsBox->setEditText(QString::number(scene.fps, 'g', 6));
    };
    connect(m_fpsBox, &QComboBox::activated, this, applyFps);
    connect(m_fpsBox->lineEdit(), &QLineEdit::editingFinished, this, applyFps);
    if (playback) {
        playback->setIconSize(QSize(22, 22));
        playback->addWidget(m_jog);
        playback->addWidget(new QLabel(tr("  Frame "), this));
        playback->addWidget(m_frameBox);
        playback->addWidget(new QLabel(tr("  Start "), this));
        playback->addWidget(m_startBox);
        playback->addWidget(new QLabel(tr("  Stop "), this));
        playback->addWidget(m_stopBox);
        playback->addWidget(new QLabel(tr("  FPS "), this));
        playback->addWidget(m_fpsBox);
    }
    QToolBar* const own = m_actions->createToolbar(QStringLiteral("TimelineViewToolbar"), this);
    QWidget* const panel = new QWidget(this);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    QHBoxLayout* const bars = new QHBoxLayout;
    bars->setContentsMargins(0, 0, 0, 0);
    bars->setSpacing(0);
    if (playback)
        bars->addWidget(playback);
    if (own) {
        own->setIconSize(QSize(22, 22));
        bars->addWidget(own, 1);
    }
    layout->addLayout(bars);
    layout->addWidget(m_timeline, 1);
    addView(tr("Timeline"), QStringLiteral("TimelineDock"), panel, Qt::BottomDockWidgetArea);
}

void MainWindow::refreshViewTabs()
{
    m_tabsStale = false;
    for (ViewTitleBar* bar : std::as_const(m_titleBars))
        bar->refresh();
    placeCollapseArrows();
}
