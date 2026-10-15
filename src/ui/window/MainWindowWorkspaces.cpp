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
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

QStringList MainWindow::builtinWorkspaces()
{
    return {QStringLiteral("Default"), QStringLiteral("Hand-Drawn"), QStringLiteral("Animating"), QStringLiteral("Compositing")};
}

QStringList MainWindow::workspaces() const
{
    QStringList names = builtinWorkspaces();
    QSettings s = settings();
    s.beginGroup(QStringLiteral("workspaces"));
    for (QString const& name : s.childKeys())
        if (!names.contains(name))
            names << name;
    s.endGroup();
    // In the order the Workspace Manager put them in (those it has not seen yet after them).
    QStringList const order = s.value(QStringLiteral("workspaceOrder")).toStringList();
    QStringList sorted;
    for (QString const& name : order)
        if (names.contains(name) && !sorted.contains(name))
            sorted << name;
    for (QString const& name : std::as_const(names))
        if (!sorted.contains(name))
            sorted << name;
    return sorted;
}

bool MainWindow::workspaceInToolbar(QString const& name) const
{
    return !settings().value(QStringLiteral("workspaceHidden")).toStringList().contains(name);
}

void MainWindow::setWorkspaceInToolbar(QString const& name, bool shown)
{
    QStringList hidden = settings().value(QStringLiteral("workspaceHidden")).toStringList();
    hidden.removeAll(name);
    if (!shown)
        hidden << name;
    settings().setValue(QStringLiteral("workspaceHidden"), hidden);
    refreshWorkspaceBox();
}

bool MainWindow::renameWorkspace(QString const& name, QString const& to)
{
    QString const target = to.trimmed();
    if (builtinWorkspaces().contains(name) || !workspaces().contains(name) || target.isEmpty() || target.contains(QLatin1Char('/'))
        || workspaces().contains(target))
        return false;
    QSettings s = settings();
    s.setValue(QStringLiteral("workspaces/") + target, s.value(QStringLiteral("workspaces/") + name));
    s.remove(QStringLiteral("workspaces/") + name);
    // What goes with it: where the floating views were, what it was made from, its place in the list.
    for (QString const& key : s.allKeys())
        if (key.startsWith(QStringLiteral("workspaceExtras/") + name + QLatin1Char('/'))) {
            s.setValue(QStringLiteral("workspaceExtras/") + target + key.mid(QStringLiteral("workspaceExtras/").size() + name.size()), s.value(key));
            s.remove(key);
        }
    if (s.contains(QStringLiteral("workspaceBase/") + name)) {
        s.setValue(QStringLiteral("workspaceBase/") + target, s.value(QStringLiteral("workspaceBase/") + name));
        s.remove(QStringLiteral("workspaceBase/") + name);
    }
    QStringList order = s.value(QStringLiteral("workspaceOrder")).toStringList();
    order.replaceInStrings(QRegularExpression(QStringLiteral("^") + QRegularExpression::escape(name) + QStringLiteral("$")), target);
    s.setValue(QStringLiteral("workspaceOrder"), order);
    QStringList hidden = s.value(QStringLiteral("workspaceHidden")).toStringList();
    if (hidden.removeAll(name) > 0)
        hidden << target;
    s.setValue(QStringLiteral("workspaceHidden"), hidden);
    if (m_workspace == name)
        m_workspace = target;
    refreshWorkspaceBox();
    return true;
}

QString MainWindow::duplicateWorkspace(QString const& name)
{
    if (!workspaces().contains(name))
        return QString();
    QString copy = tr("%1 Copy").arg(name);
    for (int k = 2; workspaces().contains(copy); ++k)
        copy = tr("%1 Copy %2").arg(name).arg(k);
    QSettings s = settings();
    if (name == m_workspace) {
        // The one in use: as it is now.
        s.setValue(QStringLiteral("workspaces/") + copy, saveState(kLayoutVersion));
        saveWorkspaceExtras(copy);
    } else if (s.contains(QStringLiteral("workspaces/") + name)) {
        s.setValue(QStringLiteral("workspaces/") + copy, s.value(QStringLiteral("workspaces/") + name));
        for (QString const& key : s.allKeys())
            if (key.startsWith(QStringLiteral("workspaceExtras/") + name + QLatin1Char('/')))
                s.setValue(QStringLiteral("workspaceExtras/") + copy + key.mid(QStringLiteral("workspaceExtras/").size() + name.size()), s.value(key));
    } else {
        // A built-in one never changed: made as that one is, when it is first opened.
        s.setValue(QStringLiteral("workspaces/") + copy, QByteArray());
        s.setValue(QStringLiteral("workspaceBase/") + copy, s.value(QStringLiteral("workspaceBase/") + name, name));
    }
    QStringList order = workspaces();
    order.removeAll(copy);
    order.insert(order.indexOf(name) + 1, copy);
    s.setValue(QStringLiteral("workspaceOrder"), order);
    refreshWorkspaceBox();
    return copy;
}

bool MainWindow::deleteWorkspace(QString const& name)
{
    if (builtinWorkspaces().contains(name) || !workspaces().contains(name))
        return false;
    QSettings s = settings();
    s.remove(QStringLiteral("workspaces/") + name);
    s.remove(QStringLiteral("workspaceExtras/") + name);
    s.remove(QStringLiteral("workspaceBase/") + name);
    if (m_workspace == name) {
        m_workspace = QStringLiteral("Default");
        loadWorkspace(m_workspace);
    }
    refreshWorkspaceBox();
    return true;
}

bool MainWindow::moveWorkspace(QString const& name, int by)
{
    QStringList order = workspaces();
    int const at = int(order.indexOf(name)), to = at + by;
    if (at < 0 || to < 0 || to >= order.size())
        return false;
    order.move(at, to);
    settings().setValue(QStringLiteral("workspaceOrder"), order);
    refreshWorkspaceBox();
    return true;
}

bool MainWindow::workspaceChanged() const
{
    return !m_cleanWorkspace.isEmpty() && const_cast<MainWindow*>(this)->saveState(kLayoutVersion) != m_cleanWorkspace;
}

void MainWindow::markWorkspaceClean()
{
    m_cleanWorkspace = saveState(kLayoutVersion);
}

void MainWindow::onActionSaveWorkspace()
{
    saveCurrentWorkspace(true);
    statusBar()->showMessage(tr("Workspace %1 saved.").arg(m_workspace), 4000);
}

void MainWindow::onActionWorkspaceManager()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("WorkspaceManager"));
    dialog.setWindowTitle(tr("Workspace Manager"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("WorkspaceList"));
    auto fill = [&, this](QString const& pick) {
        QSignalBlocker const block(list);
        list->clear();
        for (QString const& name : workspaces()) {
            QListWidgetItem* const item = new QListWidgetItem(builtinWorkspaces().contains(name) ? tr("%1  (built in)").arg(name) : name, list);
            item->setData(Qt::UserRole, name);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(workspaceInToolbar(name) ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(tr("Ticked: in the workspace list of the toolbar."));
            if (name == pick)
                list->setCurrentItem(item);
        }
    };
    auto current = [list] { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString(); };
    fill(m_workspace);
    connect(list, &QListWidget::itemChanged, &dialog, [this](QListWidgetItem* item) {
        setWorkspaceInToolbar(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
    });
    QPushButton* const rename = new QPushButton(tr("Rename..."), &dialog);
    rename->setObjectName(QStringLiteral("WorkspaceRename"));
    rename->setToolTip(tr("Rename the selected workspace"));
    QPushButton* const duplicate = new QPushButton(tr("Duplicate"), &dialog);
    duplicate->setObjectName(QStringLiteral("WorkspaceDuplicate"));
    duplicate->setToolTip(tr("Duplicate the selected workspace"));
    QPushButton* const remove = new QPushButton(tr("Delete"), &dialog);
    remove->setObjectName(QStringLiteral("WorkspaceDelete"));
    remove->setToolTip(tr("Delete the selected workspace"));
    QPushButton* const up = new QPushButton(tr("Move Up"), &dialog);
    up->setObjectName(QStringLiteral("WorkspaceUp"));
    up->setToolTip(tr("Move the workspace up"));
    QPushButton* const down = new QPushButton(tr("Move Down"), &dialog);
    down->setObjectName(QStringLiteral("WorkspaceDown"));
    down->setToolTip(tr("Move the workspace down"));
    QPushButton* const use = new QPushButton(tr("Use"), &dialog);
    use->setObjectName(QStringLiteral("WorkspaceUse"));
    use->setToolTip(tr("Switch to the selected workspace"));
    auto enable = [&, this] {
        QString const name = current();
        bool const builtin = builtinWorkspaces().contains(name);
        rename->setEnabled(!name.isEmpty() && !builtin);
        remove->setEnabled(!name.isEmpty() && !builtin);
        duplicate->setEnabled(!name.isEmpty());
        up->setEnabled(list->currentRow() > 0);
        down->setEnabled(list->currentRow() >= 0 && list->currentRow() + 1 < list->count());
        use->setEnabled(!name.isEmpty() && name != m_workspace);
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, enable);
    connect(rename, &QPushButton::clicked, &dialog, [&, this] {
        QString const name = current();
        bool ok = false;
        QString const to = QInputDialog::getText(&dialog, tr("Rename Workspace"), tr("Enter the new workspace name:"), QLineEdit::Normal, name, &ok).trimmed();
        if (!ok || to == name)
            return;
        if (!renameWorkspace(name, to)) {
            QMessageBox::information(&dialog, tr("Rename Workspace"),
                                     workspaces().contains(to) ? tr("Workspace already exists.") : tr("That name will not do."));
            return;
        }
        fill(to);
        enable();
    });
    connect(duplicate, &QPushButton::clicked, &dialog, [&, this] {
        fill(duplicateWorkspace(current()));
        enable();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&, this] {
        QString const name = current();
        if (QMessageBox::question(&dialog, tr("Delete Workspace"), tr("Delete the workspace %1?").arg(name)) != QMessageBox::Yes)
            return;
        deleteWorkspace(name);
        fill(m_workspace);
        enable();
    });
    connect(up, &QPushButton::clicked, &dialog, [&, this] {
        QString const name = current();
        moveWorkspace(name, -1);
        fill(name);
        enable();
    });
    connect(down, &QPushButton::clicked, &dialog, [&, this] {
        QString const name = current();
        moveWorkspace(name, 1);
        fill(name);
        enable();
    });
    connect(use, &QPushButton::clicked, &dialog, [&, this] {
        switchWorkspace(current());
        enable();
    });
    connect(list, &QListWidget::itemDoubleClicked, use, &QPushButton::click);
    QVBoxLayout* const buttons = new QVBoxLayout;
    for (QPushButton* b : {use, rename, duplicate, remove, up, down})
        buttons->addWidget(b);
    buttons->addStretch(1);
    QHBoxLayout* const middle = new QHBoxLayout;
    middle->addWidget(list, 1);
    middle->addLayout(buttons);
    QDialogButtonBox* const close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Your Workspaces (ticked: in the toolbar's list):"), &dialog));
    layout->addLayout(middle, 1);
    layout->addWidget(close);
    enable();
    dialog.resize(420, 320);
    dialog.exec();
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
    } else if (name == QLatin1String("Compositing")) {
        // Putting the picture together: the node graph large down the right, the node library and the layer's properties
        // (its effects) beside it, the timeline low along the bottom.
        QDockWidget* const nodeView = dockOf("NodeViewDock");
        QDockWidget* const nodeLibrary = dockOf("NodeLibraryDock");
        place(nodeView, Qt::RightDockWidgetArea);
        place(layerProps, Qt::RightDockWidgetArea);
        place(timeline, Qt::BottomDockWidgetArea);
        tab(layerProps, nodeLibrary);
        tab(layerProps, tool);
        tab(timeline, xsheet);
        nodeView->raise();
        layerProps->raise();
        timeline->raise();
        QSize const size = isVisible() && !isMinimized() ? this->size() : screen()->availableGeometry().size();
        resizeDocks({nodeView}, {std::max(420, size.width() * 620 / 1920)}, Qt::Horizontal);
        resizeDocks({nodeView, layerProps}, {460, 300}, Qt::Vertical);
        resizeDocks({timeline}, {std::max(170, size.height() * 200 / 1080)}, Qt::Vertical);
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
    // And the graph of the scene behind the colours (where it is not out on its own).
    if (name != QLatin1String("Compositing")) {
        QDockWidget* const nodes = dockOf("NodeViewDock");
        tabifyDockWidget(colour, nodes);
        nodes->show();
        colour->raise();
    } else {
        place(colour, Qt::LeftDockWidgetArea);
        colour->hide();
    }
}

void MainWindow::loadWorkspace(QString const& name)
{
    QByteArray const saved = m_persistWorkspaces ? settings().value(QStringLiteral("workspaces/") + name).toByteArray() : QByteArray();
    if (saved.isEmpty() || !restoreState(saved, kLayoutVersion)) {
        // A copy of a built-in one that has not been changed yet is made as that one is.
        QString const base = settings().value(QStringLiteral("workspaceBase/") + name, name).toString();
        applyBuiltinWorkspace(builtinWorkspaces().contains(base) ? base : QStringLiteral("Default"));
    }
    if (m_persistWorkspaces)
        loadWorkspaceExtras(name);
    // (What it is now is what it was opened as: changes are told from that, once the window has settled.)
    if (isVisible())
        QTimer::singleShot(0, this, [this] { markWorkspaceClean(); });
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

void MainWindow::saveCurrentWorkspace(bool force)
{
    // A workspace is kept as the user left it - or, with that preference off, only when saved.
    // (Panels hidden by a collapse arrow are brought back first: the arrow would not be there to do it next time.)
    if (!m_persistWorkspaces || (!force && !m_autoSaveWorkspace))
        return;
    expandAllSides();
    settings().setValue(QStringLiteral("workspaces/") + m_workspace, saveState(kLayoutVersion));
    saveWorkspaceExtras(m_workspace);
    markWorkspaceClean();
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
    // Those the Workspace Manager leaves in the toolbar's list (and the one in use, always).
    for (QString const& name : workspaces())
        if (workspaceInToolbar(name) || name == m_workspace)
            m_workspaceBox->addItem(name);
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
    QAction* const save = menu->addAction(tr("Save Workspace"));
    save->setObjectName(QStringLiteral("SaveWorkspace"));
    connect(save, &QAction::triggered, this, &MainWindow::onActionSaveWorkspace);
    connect(menu->addAction(tr("Save Workspace As...")), &QAction::triggered, this, &MainWindow::onActionSaveWorkspaceAs);
    connect(menu->addAction(tr("Workspace Manager...")), &QAction::triggered, this, &MainWindow::onActionWorkspaceManager);
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
    // The arrangement as it is first seen: changes to the workspace are told from it.
    if (m_cleanWorkspace.isEmpty())
        QTimer::singleShot(0, this, [this] { markWorkspaceClean(); });
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
