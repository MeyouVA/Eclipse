// Eclipse - the main window: workspaces - the arrangements of the views.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QDockWidget>
#include <QShowEvent>

#include <utility>
#include <QInputDialog>
#include <QMenu>
#include <QSettings>
#include <QToolBar>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/ViewTitleBar.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"
#include "ui/views/DepthView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QComboBox>
#include <QScreen>

QStringList MainWindow::builtinWorkspaces()
{
    return {QStringLiteral("Default"), QStringLiteral("Hand-Drawn"), QStringLiteral("Animating")};
}

QStringList MainWindow::workspaces() const
{
    QStringList names = builtinWorkspaces();
    QSettings s = settings();
    s.beginGroup(QStringLiteral("workspaces"));
    for (QString const& name : s.childKeys())
        if (!names.contains(name))
            names << name;
    return names;
}

void MainWindow::applyBuiltinWorkspace(QString const& name)
{
    QDockWidget* const timeline = dockOf("TimelineDock");
    QDockWidget* const tool = dockOf("ToolPropertiesDock");
    QDockWidget* const layerProps = dockOf("LayerPropertiesDock");
    QDockWidget* const colour = dockOf("ColourDock");
    QDockWidget* const xsheet = dockOf("XsheetDock");
    // Start from nothing: a dock keeps its old place otherwise.
    for (QDockWidget* d : std::as_const(m_docks)) {
        d->setFloating(false);
        removeDockWidget(d);
    }
    auto place = [this](QDockWidget* d, Qt::DockWidgetArea area) {
        addDockWidget(area, d);
        d->show();
    };
    auto tab = [this](QDockWidget* first, QDockWidget* second) {
        tabifyDockWidget(first, second);
        second->show();
    };
    for (QToolBar* bar : std::as_const(m_toolbars))
        bar->show();

    if (name == QLatin1String("Hand-Drawn")) {
        // Paper animation: the exposure sheet beside the drawing, colours and tool
        // settings on the other side.
        place(tool, Qt::LeftDockWidgetArea);
        place(colour, Qt::LeftDockWidgetArea);
        place(xsheet, Qt::RightDockWidgetArea);
        place(timeline, Qt::BottomDockWidgetArea);
        tab(tool, layerProps);
        tool->raise();
        resizeDocks({xsheet, tool}, {330, 250}, Qt::Horizontal);
        resizeDocks({timeline}, {150}, Qt::Vertical);
    } else if (name == QLatin1String("Animating")) {
        // Moving layers: a tall timeline, the numbers of the layer at hand.
        place(layerProps, Qt::RightDockWidgetArea);
        place(colour, Qt::RightDockWidgetArea);
        place(timeline, Qt::BottomDockWidgetArea);
        tab(colour, tool);
        tab(timeline, xsheet);
        colour->raise();
        timeline->raise();
        resizeDocks({layerProps}, {270}, Qt::Horizontal);
        resizeDocks({timeline}, {300}, Qt::Vertical);
    } else {
        // The usual arrangement of animation software: Tool Properties with the Library behind it over
        // the colours (with the Node View behind them) down the right; the Timeline along the
        // bottom with the Node Library behind it.  The other views are opened from the Windows menu.
        place(tool, Qt::RightDockWidgetArea);
        place(colour, Qt::RightDockWidgetArea);
        place(timeline, Qt::BottomDockWidgetArea);
        tab(tool, dockOf("LibraryDock"));
        tab(timeline, dockOf("NodeLibraryDock"));
        tool->raise();
        colour->raise();
        timeline->raise();
        // About a quarter of the width for the panels at the right and of the height for
        // the timeline: 520 and 243 pixels on a 1920 x 1080 screen.
        // Before the window is shown it has not its size yet: it will fill the screen.
        QSize const size = isVisible() && !isMinimized() ? this->size() : screen()->availableGeometry().size();
        resizeDocks({tool}, {std::max(360, size.width() * 520 / 1920)}, Qt::Horizontal);
        resizeDocks({tool, colour}, {359, 376}, Qt::Vertical);
        resizeDocks({timeline}, {std::max(190, size.height() * 243 / 1080)}, Qt::Vertical);
    }
    // The drawings of the layer sit behind its properties, where those are shown.
    QDockWidget* const substitution = dockOf("DrawingSubstitutionDock");
    if (name == QLatin1String("Hand-Drawn") || name == QLatin1String("Animating")) {
        tabifyDockWidget(layerProps, substitution);
        substitution->show();
    }
    (name == QLatin1String("Animating") ? layerProps : tool)->raise();
    // And the graph of the scene behind the colours.
    QDockWidget* const nodes = dockOf("NodeViewDock");
    tabifyDockWidget(colour, nodes);
    nodes->show();
    colour->raise();
}

void MainWindow::loadWorkspace(QString const& name)
{
    QByteArray const saved = m_persistWorkspaces ? settings().value(QStringLiteral("workspaces/") + name).toByteArray() : QByteArray();
    if (saved.isEmpty() || !restoreState(saved, kLayoutVersion))
        applyBuiltinWorkspace(builtinWorkspaces().contains(name) ? name : QStringLiteral("Default"));
    if (m_persistWorkspaces)
        loadWorkspaceExtras(name);
}

void MainWindow::saveWorkspaceExtras(QString const& name)
{
    QSettings s = settings();
    QString const at = QStringLiteral("workspaceExtras/") + name + QLatin1Char('/');
    s.setValue(at + QStringLiteral("cameraFloating"), cameraFloating());
    s.setValue(at + QStringLiteral("cameraDock"), int(cameraDockArea()));
    if (m_cameraWindow)
        s.setValue(at + QStringLiteral("cameraGeometry"), m_cameraWindow->saveGeometry());
    QWidget* const drawing = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly);
    s.setValue(at + QStringLiteral("drawingWindow"), drawing && drawing->isVisible());
    if (drawing)
        s.setValue(at + QStringLiteral("drawingGeometry"), drawing->saveGeometry());
}

void MainWindow::loadWorkspaceExtras(QString const& name)
{
    QSettings const s = settings();
    QString const at = QStringLiteral("workspaceExtras/") + name + QLatin1Char('/');
    bool const floating = s.value(at + QStringLiteral("cameraFloating"), false).toBool();
    int const docked = floating ? 0 : s.value(at + QStringLiteral("cameraDock"), 0).toInt();
    setCameraDocked(docked == Qt::LeftDockWidgetArea || docked == Qt::RightDockWidgetArea || docked == Qt::BottomDockWidgetArea || docked == Qt::TopDockWidgetArea
                        ? Qt::DockWidgetArea(docked) : Qt::NoDockWidgetArea);
    setCameraFloating(floating);
    if (floating && m_cameraWindow && s.contains(at + QStringLiteral("cameraGeometry")))
        m_cameraWindow->restoreGeometry(s.value(at + QStringLiteral("cameraGeometry")).toByteArray());
    bool const drawing = s.value(at + QStringLiteral("drawingWindow"), false).toBool();
    setDrawingWindowShown(drawing);
    if (QWidget* const window = findChild<QWidget*>(QStringLiteral("DrawingWindow"), Qt::FindDirectChildrenOnly); drawing && window
        && s.contains(at + QStringLiteral("drawingGeometry")))
        window->restoreGeometry(s.value(at + QStringLiteral("drawingGeometry")).toByteArray());
}

void MainWindow::saveCurrentWorkspace()
{
    // Like the scene, a workspace is as the user left it: there is no separate "save".
    // (Panels hidden by a collapse arrow are brought back first: the arrow would not be there to do it next time.)
    expandAllSides();
    if (m_persistWorkspaces) {
        settings().setValue(QStringLiteral("workspaces/") + m_workspace, saveState(kLayoutVersion));
        saveWorkspaceExtras(m_workspace);
    }
}

void MainWindow::switchWorkspace(QString const& name)
{
    if (name == m_workspace || !workspaces().contains(name))
        return;
    saveCurrentWorkspace();
    m_workspace = name;
    loadWorkspace(name);
    refreshWorkspaceBox();
}

void MainWindow::refreshWorkspaceBox()
{
    if (!m_workspaceBox)
        return;
    QSignalBlocker const block(m_workspaceBox);
    m_workspaceBox->clear();
    m_workspaceBox->addItems(workspaces());
    m_workspaceBox->setCurrentText(m_workspace);
}

void MainWindow::onShowWorkspaces(QMenu* menu)
{
    menu->clear();
    for (QString const& name : workspaces()) {
        QAction* const a = menu->addAction(name);
        a->setCheckable(true);
        a->setChecked(name == m_workspace);
        connect(a, &QAction::triggered, this, [this, name] { switchWorkspace(name); });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Save Workspace As...")), &QAction::triggered, this, &MainWindow::onActionSaveWorkspaceAs);
    QAction* const remove = menu->addAction(tr("Delete Workspace"));
    remove->setEnabled(!builtinWorkspaces().contains(m_workspace));
    connect(remove, &QAction::triggered, this, &MainWindow::onActionDeleteWorkspace);
}

void MainWindow::onActionSaveWorkspaceAs()
{
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Save Workspace As"), tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty() || name.contains(QLatin1Char('/')))
        return;
    // The arrangement on screen becomes the new workspace; the old one keeps what it had
    // when it was last left.
    m_workspace = name;
    settings().setValue(QStringLiteral("workspaces/") + name, saveState(kLayoutVersion));
    saveWorkspaceExtras(name);
    refreshWorkspaceBox();
}

void MainWindow::onActionDeleteWorkspace()
{
    if (builtinWorkspaces().contains(m_workspace))
        return;
    settings().remove(QStringLiteral("workspaces/") + m_workspace);
    m_workspace = QStringLiteral("Default");
    loadWorkspace(m_workspace);
    refreshWorkspaceBox();
}

void MainWindow::restoreWorkspace()
{
    QSettings s = settings();
    // The first time, the window fills the screen.
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        setWindowState(windowState() | Qt::WindowMaximized);
    m_persistWorkspaces = true;
    applyPreferences();
    QString const name = s.value(QStringLiteral("window/workspace"), QStringLiteral("Default")).toString();
    m_workspace = workspaces().contains(name) ? name : QStringLiteral("Default");
    loadWorkspace(m_workspace);
    refreshWorkspaceBox();
    holdBackFloating();
}

void MainWindow::holdBackFloating()
{
    // Before the window itself is shown (while the Welcome window asks what to open), the panels and toolbars that float
    // as windows of their own wait for it, instead of standing about the screen alone.
    if (!isVisible()) {
        for (QDockWidget* const dock : findChildren<QDockWidget*>())
            if (dock->isFloating() && dock->isVisible()) {
                dock->hide();
                m_heldBack.push_back(dock);
            }
        for (QToolBar* const bar : findChildren<QToolBar*>())
            if (bar->isFloating() && bar->isVisible()) {
                bar->hide();
                m_heldBack.push_back(bar);
            }
    }
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // The floating panels held back until now come out with it.
    QVector<QPointer<QWidget>> const held = std::exchange(m_heldBack, {});
    for (QPointer<QWidget> const& w : held)
        if (w)
            w->show();
}

void MainWindow::onActionRestoreDefaultWorkspace()
{
    // Forget what the user did to the current workspace.
    settings().remove(QStringLiteral("workspaces/") + m_workspace);
    applyBuiltinWorkspace(builtinWorkspaces().contains(m_workspace) ? m_workspace : QStringLiteral("Default"));
}
