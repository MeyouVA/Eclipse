// Eclipse - the main window: collapse arrows, which view is in use, toolbars, the Windows menu.
#include "ui/window/MainWindow.h"

#include <QMouseEvent>
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
        CollapseHandle* const handle = new CollapseHandle(this);
        handle->setObjectName(QLatin1String(kNames[side]));
        handle->setStacked(side == TopSide || side == BottomSide);
        connect(handle, &CollapseHandle::pressed, this, [this, side](bool second) { pressSide(side, second); });
        handle->hide();
        m_collapse[side] = handle;
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
        QRect panels;
        for (QDockWidget* const d : std::as_const(m_docks)) {
            if (dockWidgetArea(d) == areaOfSide(side) && !d->isHidden() && !d->isFloating()) {
                open = true;
                if (!d->visibleRegion().isEmpty())
                    panels |= d->geometry();        // (one behind another on its tabs is not where it says it is)
            }
        }
        // Panels brought back some other way: there is nothing left for the arrows to bring back.
        if (open) {
            m_collapsed[side].clear();
            m_collapsedFront[side].clear();
        }
        CollapseHandle* const handle = m_collapse[side];
        bool const folded = !m_collapsed[side].isEmpty();
        if (!open && !folded) {
            handle->hide();
            continue;
        }
        // Each arrow pushes the line between the panels and the middle view its own way.
        bool const sideIsSecond = side == RightSide || side == BottomSide;       // the panels lie right of, or below, that line
        bool const towardSide = m_middleCollapsed || open, towardMiddle = folded || !m_middleCollapsed;
        handle->setArrowsEnabled(sideIsSecond ? towardMiddle : towardSide, sideIsSecond ? towardSide : towardMiddle);
        QString const sideTip = m_middleCollapsed ? tr("Show the view in the middle again") : tr("Hide the panels on this side"),
                      middleTip = folded ? tr("Show the panels on this side again") : tr("Hide the view in the middle");
        handle->first()->setToolTip(sideIsSecond ? middleTip : sideTip);
        handle->second()->setToolTip(sideIsSecond ? sideTip : middleTip);
        // Just inside the edge of the middle view, half way along it; with the middle view folded away, at the edge of the panels.
        QSize const size = handle->handleSize();
        int const thick = std::min(size.width(), size.height());
        QRect at(QPoint(0, 0), size);
        if (!m_middleCollapsed) {
            if (side == LeftSide)
                at.moveTo(c.left(), c.center().y() - size.height() / 2);
            else if (side == RightSide)
                at.moveTo(c.right() - thick + 1, c.center().y() - size.height() / 2);
            else if (side == TopSide)
                at.moveTo(c.center().x() - size.width() / 2, c.top());
            else
                at.moveTo(c.center().x() - size.width() / 2, c.bottom() - 3);       // over the gap below, clear of the messages along the bottom of the view
        } else if (open && !panels.isEmpty()) {
            if (side == LeftSide)
                at.moveTo(panels.right() - thick + 1, panels.center().y() - size.height() / 2);
            else if (side == RightSide)
                at.moveTo(panels.left(), panels.center().y() - size.height() / 2);
            else if (side == TopSide)
                at.moveTo(panels.center().x() - size.width() / 2, panels.bottom() - thick + 1);
            else
                at.moveTo(panels.center().x() - size.width() / 2, panels.top());
        } else {
            // Neither is there: at the edge of the window.
            if (side == LeftSide)
                at.moveTo(0, height() / 2 - size.height() / 2);
            else if (side == RightSide)
                at.moveTo(width() - thick, height() / 2 - size.height() / 2);
            else if (side == TopSide)
                at.moveTo(width() / 2 - size.width() / 2, c.top());
            else
                at.moveTo(width() / 2 - size.width() / 2, height() - thick);
        }
        // Never off the window: with nothing beyond that edge it stands just inside it.
        if (at.bottom() > height() - 1)
            at.moveBottom(height() - 1);
        if (at.right() > width() - 1)
            at.moveRight(width() - 1);
        handle->setGeometry(at);
        handle->show();
        handle->raise();
    }
    placeBetweenArrows();
}

void MainWindow::placeBetweenArrows()
{
    // A fold whose panels came back some other way, or whose neighbour is gone, is a fold no more.
    for (int i = int(m_folds.size()) - 1; i >= 0; --i) {
        Fold const& fold = m_folds[i];
        bool gone = !m_docks.contains(fold.anchor) || fold.anchor->isFloating();
        for (QDockWidget* const d : fold.docks)
            gone = gone || !m_docks.contains(d) || !d->isHidden();
        if (!gone && fold.anchor->isHidden()) {
            // (The neighbour hidden with its whole side comes back with it: the fold waits.)
            bool waits = false;
            for (int side = 0; side < SideCount; ++side)
                waits = waits || m_collapsed[side].contains(fold.anchor);
            gone = !waits;
        }
        if (gone)
            m_folds.removeAt(i);
    }
    struct Placed {
        BetweenSpot spot;
        QPoint centre;
        bool stacked;
    };
    QList<Placed> placed;
    // The panels in front, and every two of them that lie against each other on the same side.
    QList<QDockWidget*> fronts;
    for (QDockWidget* const d : std::as_const(m_docks))
        if (!d->isHidden() && !d->isFloating() && dockWidgetArea(d) != Qt::NoDockWidgetArea && !d->visibleRegion().isEmpty())
            fronts << d;
    auto foldAt = [this](QDockWidget* a, QDockWidget* b, bool stacked) {
        for (Fold const& fold : std::as_const(m_folds))
            if (fold.stacked == stacked && ((fold.anchor == b && fold.before) || (fold.anchor == a && !fold.before)))
                return true;
        return false;
    };
    int const reach = 40, least = 60;
    for (QDockWidget* const a : std::as_const(fronts)) {
        for (QDockWidget* const b : std::as_const(fronts)) {
            if (a == b || dockWidgetArea(a) != dockWidgetArea(b))
                continue;
            QRect const ra = a->geometry(), rb = b->geometry();
            int const gapDown = rb.top() - ra.bottom(), gapAcross = rb.left() - ra.right();
            int const from = std::max(ra.left(), rb.left()), to = std::min(ra.right(), rb.right());
            int const top = std::max(ra.top(), rb.top()), bottom = std::min(ra.bottom(), rb.bottom());
            if (gapDown >= -2 && gapDown <= reach && to - from > least) {
                if (!foldAt(a, b, true))
                    placed.push_back({{a, b, -1}, QPoint((from + to) / 2, ra.bottom() + gapDown / 2), true});
            } else if (gapAcross >= -2 && gapAcross <= reach && bottom - top > least) {
                if (!foldAt(a, b, false))
                    placed.push_back({{a, b, -1}, QPoint(ra.right() + gapAcross / 2, (top + bottom) / 2), false});
            }
        }
    }
    // The folds: by the edge of the neighbour the folded panel lay against.
    for (int i = 0; i < m_folds.size(); ++i) {
        Fold const& fold = m_folds[i];
        if (fold.anchor->isHidden())
            continue;
        QRect const r = fold.anchor->geometry();
        QPoint const centre = fold.stacked ? QPoint(r.center().x(), fold.before ? r.top() + 4 : r.bottom() - 4)
                                           : QPoint(fold.before ? r.left() + 4 : r.right() - 4, r.center().y());
        placed.push_back({{fold.anchor, nullptr, i}, centre, fold.stacked});
    }
    while (m_betweenHandles.size() < placed.size()) {
        CollapseHandle* const handle = new CollapseHandle(this);
        handle->setObjectName(QStringLiteral("CollapseBetween"));
        int const index = int(m_betweenHandles.size());
        connect(handle, &CollapseHandle::pressed, this, [this, index](bool second) { pressBetween(index, second); });
        m_betweenHandles.push_back(handle);
    }
    m_betweenSpots.clear();
    for (int i = 0; i < m_betweenHandles.size(); ++i) {
        CollapseHandle* const handle = m_betweenHandles[i];
        if (i >= placed.size()) {
            handle->hide();
            continue;
        }
        Placed const& one = placed[i];
        m_betweenSpots.push_back(one.spot);
        handle->setStacked(one.stacked);
        if (one.spot.fold >= 0) {
            // Only the arrow that points away from the folded panel has something to do: it brings it back.
            bool const before = m_folds[one.spot.fold].before;
            handle->setArrowsEnabled(!before, before);
            (before ? handle->second() : handle->first())->setToolTip(tr("Show the hidden panel again"));
            (before ? handle->first() : handle->second())->setToolTip(QString());
        } else {
            handle->setArrowsEnabled(true, true);
            handle->first()->setToolTip(one.stacked ? tr("Hide the panel above") : tr("Hide the panel on the left"));
            handle->second()->setToolTip(one.stacked ? tr("Hide the panel below") : tr("Hide the panel on the right"));
        }
        QSize const size = handle->handleSize();
        QRect at(one.centre - QPoint(size.width() / 2, size.height() / 2), size);
        at.moveLeft(std::clamp(at.left(), 0, std::max(0, width() - size.width())));
        at.moveTop(std::clamp(at.top(), 0, std::max(0, height() - size.height())));
        handle->setGeometry(at);
        handle->show();
        handle->raise();
    }
}

void MainWindow::pressSide(int side, bool second)
{
    if (side < 0 || side >= SideCount)
        return;
    bool open = false;
    for (QDockWidget* const d : std::as_const(m_docks))
        open = open || (dockWidgetArea(d) == areaOfSide(side) && !d->isHidden() && !d->isFloating());
    bool const sideIsSecond = side == RightSide || side == BottomSide;
    if (second == sideIsSecond) {
        // Towards the panels: the middle view comes back, or the panels go.
        if (m_middleCollapsed)
            setMiddleCollapsed(false);
        else if (open)
            toggleSide(side);
    } else {
        // Towards the middle view: the panels come back, or the middle view goes.
        if (!open && !m_collapsed[side].isEmpty())
            toggleSide(side);
        else if (!m_middleCollapsed && open)
            setMiddleCollapsed(true);
    }
}

void MainWindow::setMiddleCollapsed(bool collapsed)
{
    QWidget* const middle = centralWidget();
    if (!middle || collapsed == m_middleCollapsed || (!collapsed && (m_cameraWindow || m_cameraDock)))
        return;     // (nothing to show in the middle while its view is out in a window, or among the panels)
    m_middleCollapsed = collapsed;
    m_collapsing = true;
    if (collapsed) {
        m_sizesBeforeMiddle.clear();
        for (QDockWidget* const d : std::as_const(m_docks))
            if (!d->isHidden() && !d->isFloating() && !d->visibleRegion().isEmpty())
                m_sizesBeforeMiddle.insert(d, d->size());
    }
    middle->setVisible(!collapsed);
    if (!collapsed) {
        // The panels go back to the size they had: across for those beside the middle view, up and down for those above and below it.
        QList<QDockWidget*> beside, above;
        QList<int> widths, heights;
        for (auto it = m_sizesBeforeMiddle.constBegin(); it != m_sizesBeforeMiddle.constEnd(); ++it) {
            QDockWidget* const d = it.key();
            if (!m_docks.contains(d) || d->isHidden() || d->isFloating())
                continue;
            Qt::DockWidgetArea const area = dockWidgetArea(d);
            if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
                beside << d;
                widths << it.value().width();
            } else if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea) {
                above << d;
                heights << it.value().height();
            }
        }
        if (!beside.isEmpty())
            resizeDocks(beside, widths, Qt::Horizontal);
        if (!above.isEmpty())
            resizeDocks(above, heights, Qt::Vertical);
        m_sizesBeforeMiddle.clear();
    }
    m_collapsing = false;
    placeCollapseArrows();
    // (The panels take their new places a moment later.)
    QTimer::singleShot(0, this, [this] { placeCollapseArrows(); });
}

void MainWindow::pressBetween(int spot, bool second)
{
    if (spot < 0 || spot >= m_betweenSpots.size())
        return;
    BetweenSpot const what = m_betweenSpots[spot];
    m_collapsing = true;
    if (what.fold >= 0 && what.fold < m_folds.size()) {
        Fold const fold = m_folds[what.fold];
        if (second == fold.before) {
            m_folds.removeAt(what.fold);
            for (QDockWidget* const d : fold.docks)
                if (m_docks.contains(d) && !d->isFloating())
                    d->show();
            for (QDockWidget* const d : fold.front)
                if (m_docks.contains(d) && !d->isFloating())
                    d->raise();
            // The two get the room they had.
            if (!fold.front.isEmpty() && fold.size > 0 && fold.anchorSize > 0 && !fold.anchor->isHidden())
                resizeDocks({fold.front.first(), fold.anchor}, {fold.size, fold.anchorSize}, fold.stacked ? Qt::Vertical : Qt::Horizontal);
        }
    } else if (what.a && what.b) {
        // The panel the arrow points to goes, with those on its tabs; the arrows stay by the other.
        QDockWidget* const goes = second ? what.b : what.a;
        Fold fold;
        fold.anchor = second ? what.a : what.b;
        fold.before = !second;
        fold.stacked = m_betweenHandles[spot]->stacked();
        fold.docks << goes;
        fold.front << goes;
        fold.size = fold.stacked ? goes->height() : goes->width();
        fold.anchorSize = fold.stacked ? fold.anchor->height() : fold.anchor->width();
        for (QDockWidget* const d : tabifiedDockWidgets(goes))
            if (m_docks.contains(d) && !d->isHidden() && !d->isFloating())
                fold.docks << d;
        for (QDockWidget* const d : std::as_const(fold.docks))
            d->hide();
        m_folds.push_back(fold);
    }
    m_collapsing = false;
    placeCollapseArrows();
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
    setCameraFloating(false);
    setCameraDocked(Qt::NoDockWidgetArea);
    setMiddleCollapsed(false);
    for (int side = 0; side < SideCount; ++side)
        if (!m_collapsed[side].isEmpty())
            toggleSide(side);
    while (!m_folds.isEmpty()) {
        Fold const fold = m_folds.takeLast();
        for (QDockWidget* const d : fold.docks)
            if (m_docks.contains(d) && !d->isFloating())
                d->show();
        for (QDockWidget* const d : fold.front)
            if (m_docks.contains(d) && !d->isFloating())
                d->raise();
    }
    placeCollapseArrows();
}

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
            if (w->objectName() == QLatin1String("ViewFrame") && (w->window() == this || w->window() == m_cameraWindow)) {
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
        layout->addWidget(view, 1);
        window->resize(700, 520);
        window->move(mapToGlobal(QPoint(width() - 760, 80)));
    }
    window->show();
    window->raise();
}
