// Eclipse - the main window: collapse arrows, which view is in use, toolbars, the Windows menu.
#include "ui/window/MainWindow.h"

#include <QComboBox>
#include <QDockWidget>
#include <QToolButton>
#include <QMenu>
#include <QToolBar>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
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
#include <QColorDialog>
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

static Qt::DockWidgetArea areaOfSide(int side)
{
    return side == 0 ? Qt::LeftDockWidgetArea : side == 1 ? Qt::RightDockWidgetArea : side == 2 ? Qt::TopDockWidgetArea : Qt::BottomDockWidgetArea;
}

void MainWindow::createCollapseArrows()
{
    static char const* const kNames[SideCount] = {"CollapseLeft", "CollapseRight", "CollapseTop", "CollapseBottom"};
    for (int side = 0; side < SideCount; ++side) {
        QToolButton* const button = new QToolButton(this);
        button->setObjectName(QLatin1String(kNames[side]));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QToolButton { background: rgba(40, 40, 40, 215); border: 1px solid #202020; border-radius: 2px; padding: 0px; }"
                                             "QToolButton:hover { background: rgba(42, 155, 176, 235); }"));
        connect(button, &QToolButton::clicked, this, [this, side] { toggleSide(side); });
        button->hide();
        m_collapse[side] = button;
    }
    placeCollapseArrows();
}

void MainWindow::placeCollapseArrows()
{
    QWidget* const middle = centralWidget();
    if (!middle || !m_collapse[0] || m_collapsing)
        return;
    QRect const c = middle->geometry();
    for (int side = 0; side < SideCount; ++side) {
        // Open: some panel on that side is there to be seen (in front or behind a tab).
        bool open = false;
        for (QDockWidget* const d : std::as_const(m_docks))
            open = open || (dockWidgetArea(d) == areaOfSide(side) && !d->isHidden() && !d->isFloating());
        // Panels brought back some other way: there is nothing left for the arrow to bring back.
        if (open) {
            m_collapsed[side].clear();
            m_collapsedFront[side].clear();
        }
        QToolButton* const button = m_collapse[side];
        bool const wanted = open || !m_collapsed[side].isEmpty();
        if (!wanted) {
            button->hide();
            continue;
        }
        // The arrow points the way the panels go: away when they are there, back in when they are hidden.
        static Qt::ArrowType const kAway[SideCount] = {Qt::LeftArrow, Qt::RightArrow, Qt::UpArrow, Qt::DownArrow};
        static Qt::ArrowType const kBack[SideCount] = {Qt::RightArrow, Qt::LeftArrow, Qt::DownArrow, Qt::UpArrow};
        button->setArrowType(open ? kAway[side] : kBack[side]);
        button->setToolTip(open ? tr("Hide the panels on this side") : tr("Show the panels on this side again"));
        // Just inside the edge of the middle view, half way along it.
        int const thick = 13, along = 46;
        QRect at;
        if (side == LeftSide)
            at = QRect(c.left(), c.center().y() - along / 2, thick, along);
        else if (side == RightSide)
            at = QRect(c.right() - thick + 1, c.center().y() - along / 2, thick, along);
        else if (side == TopSide)
            at = QRect(c.center().x() - along / 2, c.top(), along, thick);
        else
            at = QRect(c.center().x() - along / 2, c.bottom() - 3, along, thick);      // over the gap below, clear of the messages along the bottom of the view
        // Never off the window: with nothing beyond that edge it stands just inside it.
        if (at.bottom() > height() - 1)
            at.moveBottom(height() - 1);
        button->setGeometry(at);
        button->show();
        button->raise();
    }
}

void MainWindow::toggleSide(int side)
{
    if (side < 0 || side >= SideCount)
        return;
    QList<QDockWidget*> there, front;
    for (QDockWidget* const d : std::as_const(m_docks)) {
        if (dockWidgetArea(d) != areaOfSide(side) || d->isHidden() || d->isFloating())
            continue;
        there << d;
        if (!d->visibleRegion().isEmpty())
            front << d;         // not behind another on its tabs (one behind counts as visible, but none of it is seen)
    }
    m_collapsing = true;
    if (!there.isEmpty()) {
        for (QDockWidget* const d : std::as_const(there))
            d->hide();
        m_collapsed[side] = there;
        m_collapsedFront[side] = front;
    } else {
        QList<QDockWidget*> const back = m_collapsed[side], inFront = m_collapsedFront[side];
        m_collapsed[side].clear();
        m_collapsedFront[side].clear();
        for (QDockWidget* const d : back)
            if (m_docks.contains(d) && !d->isFloating())
                d->show();
        for (QDockWidget* const d : inFront)
            if (m_docks.contains(d) && !d->isFloating())
                d->raise();
    }
    m_collapsing = false;
    placeCollapseArrows();
}

void MainWindow::expandAllSides()
{
    for (int side = 0; side < SideCount; ++side)
        if (!m_collapsed[side].isEmpty())
            toggleSide(side);
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
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
    // The middle view moved or changed size: the collapse arrows go with its edges.
    if (object == centralWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        QTimer::singleShot(0, this, [this] { placeCollapseArrows(); });
    // A click anywhere in a view makes it the view in use.
    if (event->type() == QEvent::MouseButtonPress && object->isWidgetType()) {
        for (QWidget* w = static_cast<QWidget*>(object); w && w != this; w = w->parentWidget()) {
            if (w->objectName() == QLatin1String("ViewFrame") && w->window() == this) {
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

    QToolBar* const workspace = new QToolBar(tr("Workspace"), this);
    workspace->setObjectName(QStringLiteral("WorkspaceToolbar"));
    m_workspaceBox = new QComboBox(this);
    m_workspaceBox->setMinimumWidth(130);
    m_workspaceBox->setToolTip(tr("Workspace: the arrangement of the views"));
    connect(m_workspaceBox, &QComboBox::currentTextChanged, this, [this](QString const& name) { switchWorkspace(name); });
    workspace->addWidget(m_workspaceBox);
    addToolBar(Qt::TopToolBarArea, workspace);
    m_toolbars << workspace;
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
        QAction* const a = menu->addAction(scene.layers[l].name);
        a->setCheckable(true);
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
