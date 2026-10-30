// Eclipse - the main window: the Camera view docked, floating or shown; the view in use; toolbars; the Windows menu; the Drawing window.
#include "ui/window/MainWindow.h"

#include <QMouseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QToolButton>
#include <QMenu>
#include <QToolBar>
#include <QShortcut>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/CollapseHandle.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "model/geometry/Deform.h"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include "ui/panels/ColourEditor.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QListWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QTreeWidget>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>
#include "actions/dialogs/ShortcutsDialog.h"
#include "ui/views/NodeView.h"
#include "ui/panels/PalettePanel.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "audio/lipsync/LipSync.h"
#include "ui/views/PerspectiveView.h"

Qt::DockWidgetArea MainWindow::cameraDockArea() const
{
    return m_cameraDock ? dockWidgetArea(m_cameraDock) : Qt::NoDockWidgetArea;
}

void MainWindow::setCameraDocked(Qt::DockWidgetArea area, bool tabbed)
{
    if (!m_cameraPanel)
        return;
    ViewTitleBar* const bar = m_cameraPanel->findChild<ViewTitleBar*>(QStringLiteral("CameraTitleBar"));
    if (area == Qt::NoDockWidgetArea) {
        if (!m_cameraDock)
            return;
        // Back into the middle.
        QDockWidget* const dock = m_cameraDock;
        m_cameraDock = nullptr;
        centralWidget()->layout()->addWidget(m_cameraPanel);
        m_cameraPanel->show();
        removeDockWidget(dock);
        dock->deleteLater();
        setMiddleCollapsed(false);
        if (bar)
            bar->setDocked(0);
        return;
    }
    setCameraFloating(false);
    if (!m_cameraDock) {
        // A panel of its own; its strip of tabs and menu is the view's own, so the panel has no title bar of its own.
        m_cameraDock = new QDockWidget(tr("Camera"), this);
        m_cameraDock->setObjectName(QStringLiteral("CameraDock"));
        m_cameraDock->setTitleBarWidget(new QWidget(m_cameraDock));
        m_cameraDock->setFeatures(QDockWidget::DockWidgetMovable);
        m_cameraDock->setWidget(m_cameraPanel);
        m_cameraPanel->show();
    } else {
        removeDockWidget(m_cameraDock);
    }
    addDockWidget(area, m_cameraDock);
    if (tabbed) {
        // As a tab of the first panel shown on that side.
        for (QDockWidget* const d : std::as_const(m_docks))
            if (d != m_cameraDock && !d->isHidden() && !d->isFloating() && dockWidgetArea(d) == area) {
                tabifyDockWidget(d, m_cameraDock);
                break;
            }
    }
    m_cameraDock->show();
    m_cameraDock->raise();
    setMiddleCollapsed(true);
    if (bar)
        bar->setDocked(int(area));
}

void MainWindow::setCameraFloating(bool floating)
{
    if (floating == cameraFloating() || !m_cameraPanel)
        return;
    if (floating && m_cameraDock)
        setCameraDocked(Qt::NoDockWidgetArea);
    ViewTitleBar* const bar = m_cameraPanel->findChild<ViewTitleBar*>(QStringLiteral("CameraTitleBar"));
    if (floating) {
        QSize const size = m_cameraPanel->isVisible() ? m_cameraPanel->size() : QSize(900, 600);
        QPoint const at = centralWidget()->mapToGlobal(QPoint(30, 30));
        QWidget* const out = new QWidget(this, Qt::Window);
        out->setObjectName(QStringLiteral("CameraWindow"));
        out->setWindowTitle(tr("Camera"));
        QVBoxLayout* const layout = new QVBoxLayout(out);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_cameraPanel);
        out->installEventFilter(this);
        out->resize(size);
        out->move(at);
        m_cameraWindow = out;
        m_cameraPanel->show();
        out->show();
        setMiddleCollapsed(true);
    } else {
        QWidget* const out = m_cameraWindow;
        m_cameraWindow = nullptr;
        centralWidget()->layout()->addWidget(m_cameraPanel);
        m_cameraPanel->show();
        out->removeEventFilter(this);
        out->hide();
        out->deleteLater();
        setMiddleCollapsed(false);
    }
    if (bar)
        bar->setFloating(floating);
}

void MainWindow::setCameraShown(bool shown)
{
    if (shown) {
        if (m_cameraWindow) {
            m_cameraWindow->raise();
            m_cameraWindow->activateWindow();
        } else if (m_cameraDock) {
            m_cameraDock->show();
            m_cameraDock->raise();
        } else {
            setMiddleCollapsed(false);
        }
        return;
    }
    setCameraFloating(false);
    setCameraDocked(Qt::NoDockWidgetArea);
    setMiddleCollapsed(true);
}

void MainWindow::setActiveView(QWidget* frame)
{
    if (frame == m_activeView)
        return;
    for (QWidget* w : {m_activeView, frame}) {
        if (!w)
            continue;
        w->setProperty("active", w == frame);
        w->update();
    }
    m_activeView = frame;
    m_actions->setActiveView(frame);
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
    if (m_cameraPickPrompt && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress)) {
        // Select in Camera: the next click in the Camera view picks the layer; Escape (or a click elsewhere) lets go.
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishSelectInCamera(-1);
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* const press = static_cast<QMouseEvent*>(event);
            if (object == m_canvas && press->button() == Qt::LeftButton) {
                int const picked = m_canvas->layerAt(press->position());
                if (picked < 0) {
                    statusBar()->showMessage(tr("Nothing there: click a layer's artwork in the Camera view (Escape to stop)."), 4000);
                    return true;
                }
                finishSelectInCamera(picked);
                return true;
            }
            QWidget* const pressed = qobject_cast<QWidget*>(object);
            if (pressed && pressed != m_cameraPickPrompt && !m_cameraPickPrompt->isAncestorOf(pressed) && pressed != m_canvas && !m_canvas->isAncestorOf(pressed))
                finishSelectInCamera(-1);
        }
    }
    // (When the user last did anything: saving by itself waits for a pause.)
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::KeyPress:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::Wheel:
        m_lastInput.start();
        break;
    case QEvent::MouseMove:
        if (static_cast<QMouseEvent*>(event)->buttons() != Qt::NoButton)
            m_lastInput.start();
        break;
    default:
        break;
    }
    if (m_focusOnEnter && event->type() == QEvent::Enter && !QApplication::activePopupWidget() && !QApplication::activeModalWidget())
        if (QWidget* const over = qobject_cast<QWidget*>(object))
            for (QWidget* const view : {static_cast<QWidget*>(m_canvas), static_cast<QWidget*>(m_timeline), static_cast<QWidget*>(m_xsheet),
                                        static_cast<QWidget*>(m_nodeView)})
                if (view && view->isVisible() && (over == view || view->isAncestorOf(over)) && !view->hasFocus()
                    && !view->isAncestorOf(QApplication::focusWidget())) {
                    view->setFocus(Qt::MouseFocusReason);
                    break;
                }
    if ((event->type() == QEvent::Resize || event->type() == QEvent::Move) && !m_placePending
        && (object == centralWidget() || (qobject_cast<QDockWidget*>(object) && m_docks.contains(static_cast<QDockWidget*>(object))))) {
        m_placePending = true;
        QTimer::singleShot(0, this, [this] {
            m_placePending = false;
            placeCollapseArrows();
        });
    }
    // The window of the floating middle view closed: the view goes back into the middle.
    if (event->type() == QEvent::Close && object == m_cameraWindow && m_cameraWindow) {
        event->ignore();
        QTimer::singleShot(0, this, [this] { setCameraFloating(false); });
        return true;
    }
    // A click anywhere in a view makes it the view in use.
    if (event->type() == QEvent::MouseButtonPress && object->isWidgetType()) {
        for (QWidget* w = static_cast<QWidget*>(object); w && w != this; w = w->parentWidget()) {
            if (w->objectName() == QLatin1String("ViewFrame") && (w->window() == this || w->window() == m_cameraWindow
                || m_docks.contains(qobject_cast<QDockWidget*>(w->window())))) {
                setActiveView(w);
                break;
            }
        }
    }
    return QMainWindow::eventFilter(object, event);
}

QDockWidget* MainWindow::dockOf(char const* objectName) const
{
    for (QDockWidget* d : m_docks)
        if (d->objectName() == QLatin1String(objectName))
            return d;
    return nullptr;
}

void MainWindow::createToolbars()
{
    auto add = [this](char const* id, Qt::ToolBarArea area) {
        QToolBar* const bar = m_actions->createToolbar(QLatin1String(id), this);
        if (bar) {
            addToolBar(area, bar);
            m_toolbars << bar;
        }
        return bar;
    };
    add("FileToolbar", Qt::TopToolBarArea);
    add("EditToolbar", Qt::TopToolBarArea);
    add("DrawingToolToolbar", Qt::LeftToolBarArea);
    if (QToolBar* const advanced = add("AdvancedAnimationToolbar", Qt::TopToolBarArea))
        advanced->hide();       // (shown from Windows > Toolbars)

    QToolBar* const workspace = new QToolBar(tr("Workspace"), this);
    workspace->setObjectName(QStringLiteral("WorkspaceToolbar"));
    m_workspaceBox = new QComboBox(this);
    m_workspaceBox->setMinimumWidth(130);
    m_workspaceBox->setToolTip(tr("Workspace: the arrangement of the views"));
    connect(m_workspaceBox, &QComboBox::currentTextChanged, this, [this](QString const& name) { switchWorkspace(name); });
    workspace->addWidget(m_workspaceBox);
    addToolBar(Qt::TopToolBarArea, workspace);
    m_toolbars << workspace;
    createFlipToolbar();
}

void MainWindow::onShowToolbars(QMenu* menu)
{
    menu->clear();
    for (QToolBar* bar : std::as_const(m_toolbars)) {
        QAction* const a = menu->addAction(bar->windowTitle());
        a->setCheckable(true);
        a->setChecked(bar->isVisible());
        connect(a, &QAction::toggled, bar, &QToolBar::setVisible);
    }
}

void MainWindow::onShowCameras(QMenu* menu)
{
    // The cameras of the scene: the one it is seen through is ticked.
    menu->clear();
    Scene const& scene = m_document->scene();
    QVector<int> const cameras = scene.cameras();
    for (int l : cameras) {
        bool const available = scene.layers[l].cameraAvailable;
        QAction* const a = menu->addAction(available ? scene.layers[l].name : tr("%1 (not available)").arg(scene.layers[l].name));
        a->setCheckable(true);
        a->setEnabled(available);
        a->setChecked(l == scene.cameraIndex());
        int const id = scene.layers[l].id;
        connect(a, &QAction::triggered, this, [this, id] { m_document->setActiveCamera(m_document->scene().layerIndex(id)); });
    }
    if (cameras.isEmpty())
        menu->addAction(tr("No camera in the scene"))->setEnabled(false);
}

void MainWindow::onActionResetAllExceptZ()
{
    // As Reset All, but the layer stays as near or as far as it stands.
    Scene const& scene = m_document->scene();
    if (!m_timeline->hasParameters(layer()))
        return;
    Pose const now = scene.layers[layer()].poseAt(frame());
    Pose rest;
    rest.fx = now.fx;
    rest.z = now.z;
    if (now == rest)
        return;
    m_document->setKeyframe(layer(), frame(), rest, tr("Reset All Except Z"));
}

void MainWindow::onShowWindows(QMenu* menu)
{
    QAction* const camera = menu->addAction(tr("Camera"));
    camera->setCheckable(true);
    camera->setChecked(cameraShown());
    connect(camera, &QAction::toggled, this, &MainWindow::setCameraShown);
    QAction* const drawing = menu->addAction(tr("Drawing"));
    drawing->setCheckable(true);
    drawing->setChecked(drawingWindowView() && drawingWindowView()->window()->isVisible());
    connect(drawing, &QAction::toggled, this, &MainWindow::setDrawingWindowShown);
    for (QDockWidget* dock : std::as_const(m_docks)) {
        QAction* const a = menu->addAction(dock->windowTitle());
        a->setCheckable(true);
        a->setChecked(dock->isVisible());
        connect(a, &QAction::toggled, this, [this, dock](bool on) {
            if (on)
                showView(dock);
            else
                dock->hide();
        });
    }
}

void MainWindow::showView(QDockWidget* dock)
{
    if (!dock || dock->isVisible())
        return;
    // A view taken out of the arrangement has no place: it goes behind Tool Properties.
    if (!restoreDockWidget(dock)) {
        QDockWidget* const tool = dockOf("ToolPropertiesDock");
        if (tool && tool != dock && tool->isVisible() && !tool->isFloating())
            tabifyDockWidget(tool, dock);
        else
            addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    dock->show();
    dock->raise();
}

void MainWindow::onActionToggleFullScreen(bool on)
{
    if (on)
        showFullScreen();
    else
        showNormal();
}

// Scene > Default Display: the whole scene, or the part under one group or peg, in the Camera view.
void MainWindow::onShowDisplays(QMenu* menu)
{
    menu->clear();
    Scene const& scene = m_document->scene();
    int const shown = scene.display != 0 ? scene.layerIndex(scene.display) : -1;
    QAction* const whole = menu->addAction(tr("Whole Scene"));
    whole->setCheckable(true);
    whole->setChecked(shown < 0);
    connect(whole, &QAction::triggered, this, [this] { m_document->setDisplay(-1); });
    menu->addSeparator();
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        Layer const& layer = scene.layers[l];
        if (layer.kind != Layer::Group && layer.kind != Layer::Peg)
            continue;
        QAction* const a = menu->addAction(layer.name);
        a->setCheckable(true);
        a->setChecked(l == shown);
        int const id = layer.id;
        connect(a, &QAction::triggered, this, [this, id] { m_document->setDisplay(m_document->scene().layerIndex(id)); });
    }
}

Canvas* MainWindow::drawingWindowView() const
{
    QWidget* const window = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly);
    return window ? window->findChild<Canvas*>(QStringLiteral("DrawingWindowView")) : nullptr;
}

void MainWindow::setDrawingWindowShown(bool on)
{
    QWidget* window = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly);
    if (!on) {
        if (window)
            window->hide();
        return;
    }
    if (!window) {
        window = new QWidget(this, Qt::Window);
        window->setObjectName(QStringLiteral("DrawingWindow"));
        window->setWindowTitle(tr("Drawing"));
        Canvas* const view = new Canvas(m_document, window);
        view->setObjectName(QStringLiteral("DrawingWindowView"));
        view->setDrawingView(true);
        view->setLeader(m_canvas);
        view->setCurrentCell(layer(), frame());
        if (QAction* const paste = m_actions->action(QStringLiteral("PasteInPlace")))
            window->addAction(paste);
        for (char const* id : {"EditCut", "EditCopy", "EditPaste"}) {
            QAction* const action = m_actions->action(QLatin1String(id));
            if (!action)
                continue;
            QShortcut* const shortcut = new QShortcut(action->shortcut(), window);
            connect(action, &QAction::changed, shortcut, [action, shortcut] { shortcut->setKey(action->shortcut()); });
            QString const command = QLatin1String(id);
            connect(shortcut, &QShortcut::activated, view, [view, command] {
                if (command == QLatin1String("EditCut")) view->cut();
                else if (command == QLatin1String("EditCopy")) view->copy();
                else view->paste();
            });
        }
        connect(m_timeline, &Timeline::currentCellChanged, view, &Canvas::setCurrentCell);
        connect(view, &Canvas::message, this, [this](QString const& text) { statusBar()->showMessage(text, 6000); });
        // A small bar of its own: the light table, and the zoom of this view.
        QToolBar* const bar = new QToolBar(window);
        bar->setObjectName(QStringLiteral("DrawingWindowBar"));
        bar->setIconSize(QSize(18, 18));
        auto put = [&](char const* id, QString const& text) {
            QAction* const a = bar->addAction(text);
            if (QAction* const known = m_actions->action(QLatin1String(id)))
                a->setIcon(known->icon());
            a->setObjectName(QLatin1String(id) + QStringLiteral("Here"));
            return a;
        };
        QAction* const light = put("LightTable", tr("Light Table"));
        light->setCheckable(true);
        connect(light, &QAction::toggled, view, &Canvas::onActionLightTable);
        connect(put("ZoomIn", tr("Zoom In")), &QAction::triggered, view, &Canvas::onActionZoomIn);
        connect(put("ZoomOut", tr("Zoom Out")), &QAction::triggered, view, &Canvas::onActionZoomOut);
        connect(put("ResetZoom", tr("Reset Zoom")), &QAction::triggered, view, &Canvas::onActionResetZoom);
        QVBoxLayout* const layout = new QVBoxLayout(window);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(bar);
        QString const side = settings().value(QStringLiteral("prefs/drawingThumbnails"), QStringLiteral("left")).toString();
        if (side == QLatin1String("none")) {
            layout->addWidget(view, 1);
        } else {
            SubstitutionPanel* const thumbnails = new SubstitutionPanel(m_document, window);
            thumbnails->setObjectName(QStringLiteral("DrawingWindowThumbnails"));
            thumbnails->setCurrentCell(layer(), frame());
            connect(m_timeline, &Timeline::currentCellChanged, thumbnails, &SubstitutionPanel::setCurrentCell);
            bool const across = side == QLatin1String("top") || side == QLatin1String("bottom");
            if (across)
                thumbnails->setMaximumHeight(170);
            else
                thumbnails->setMaximumWidth(190);
            QBoxLayout* const row = new QBoxLayout(across ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
            row->setSpacing(0);
            bool const first = side == QLatin1String("left") || side == QLatin1String("top");
            if (first)
                row->addWidget(thumbnails);
            row->addWidget(view, 1);
            if (!first)
                row->addWidget(thumbnails);
            layout->addLayout(row, 1);
        }
        window->resize(700, 520);
        window->move(mapToGlobal(QPoint(width() - 760, 80)));
    }
    window->show();
    window->raise();
}
