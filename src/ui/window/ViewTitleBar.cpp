#include "ui/window/ViewTitleBar.h"

#include <algorithm>

#include <QDockWidget>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>

#include <tuple>
#include <QToolButton>

ViewTitleBar::ViewTitleBar(QMainWindow* window, QDockWidget* dock, QList<QDockWidget*> const* all)
    : QWidget(dock), m_window(window), m_dock(dock), m_all(all)
{
    build();
    refresh();
}

ViewTitleBar::ViewTitleBar(QString const& title, QWidget* parent) : QWidget(parent), m_titles({title})
{
    build();
    refresh();
}

QSize ViewTitleBar::sizeHint() const
{
    return QSize(120, 26);
}

QSize ViewTitleBar::minimumSizeHint() const
{
    return QSize(60, 26);
}

void ViewTitleBar::build()
{
    setObjectName(QStringLiteral("ViewTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto button = [this](char const* name, QString const& text, QString const& tip) {
        QToolButton* const b = new QToolButton(this);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(20, 20);
        return b;
    };
    m_menu = button("ViewMenuButton", QString(QChar(0x2261)), tr("View Menu"));
    QHBoxLayout* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 0);
    layout->setSpacing(2);
    layout->addWidget(m_menu);
    layout->addStretch(1);
    m_tabs = new QHBoxLayout;
    m_tabs->setContentsMargins(0, 0, 0, 0);
    m_tabs->setSpacing(1);
    layout->addLayout(m_tabs);
    connect(m_menu, &QToolButton::clicked, this, [this] { showViewMenu(); });
    if (m_dock) {
        m_add = button("ViewAddButton", QStringLiteral("+"), tr("Add View"));
        QToolButton* const close = button("ViewCloseButton", QString(QChar(0x00d7)), tr("Close View"));
        layout->addWidget(m_add);
        layout->addWidget(close);
        connect(m_add, &QToolButton::clicked, this, [this] { showAddMenu(); });
        connect(close, &QToolButton::clicked, m_dock, &QDockWidget::close);
    } else {
        QToolButton* const close = button("ViewCloseButton", QString(QChar(0x00d7)), tr("Close View"));
        layout->addWidget(close);
        connect(close, &QToolButton::clicked, this, &ViewTitleBar::closeRequested);
    }
}

QToolButton* ViewTitleBar::tabButton(QString const& text, bool current)
{
    QToolButton* const b = new QToolButton(this);
    b->setObjectName(QStringLiteral("ViewTab"));
    b->setText(text);
    b->setProperty("current", current);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedHeight(23);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);  // a name is never cut short
    return b;
}

void ViewTitleBar::refresh()
{
    while (QLayoutItem* const item = m_tabs->takeAt(0)) {
        // Not deleted on the spot: the tab may be the one whose press brought us here.
        if (QWidget* const tab = item->widget()) {
            tab->hide();
            tab->setObjectName(QString());
            tab->deleteLater();
        }
        delete item;
    }
    if (!m_dock) {
        for (int i = 0; i < m_titles.size(); ++i) {
            QToolButton* const b = tabButton(m_titles[i], i == m_current);
            m_tabs->addWidget(b);
            b->show();
            if (i != m_current)
                connect(b, &QToolButton::clicked, this, [this, i] { emit titleChosen(i); });
        }
        return;
    }
    // The views of this panel, in the order of the window's list.
    QList<QDockWidget*> const together = m_dock->isFloating() ? QList<QDockWidget*>() : m_window->tabifiedDockWidgets(m_dock);
    for (QDockWidget* d : *m_all) {
        if (d != m_dock && !together.contains(d))
            continue;
        QToolButton* const b = tabButton(d->windowTitle(), d == m_dock);
        m_tabs->addWidget(b);
        b->show();  // at once: a layout shows what it is given only on the next turn of the event loop
        if (d != m_dock)
            connect(b, &QToolButton::clicked, d, [d] {
                d->show();
                d->raise();
            });
    }
}

void ViewTitleBar::setTitles(QStringList const& titles, int current)
{
    m_titles = titles;
    m_current = std::clamp(current, 0, std::max(0, int(titles.size()) - 1));
    refresh();
}

QStringList ViewTitleBar::tabs() const
{
    QStringList names;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (QToolButton* const b = qobject_cast<QToolButton*>(m_tabs->itemAt(i)->widget()))
            names << b->text();
    return names;
}

void ViewTitleBar::showAddMenu()
{
    // Every view that is not in this panel already; choosing one brings it here.
    QMenu menu(this);
    QList<QDockWidget*> const together = m_window->tabifiedDockWidgets(m_dock);
    for (QDockWidget* d : *m_all) {
        if (d == m_dock || (together.contains(d) && !d->isHidden()))
            continue;
        connect(menu.addAction(d->windowTitle()), &QAction::triggered, this, [this, d] {
            d->setFloating(false);
            m_window->tabifyDockWidget(m_dock, d);
            d->show();
            d->raise();
        });
    }
    if (menu.isEmpty())
        menu.addAction(tr("Every view is here"))->setEnabled(false);
    menu.exec(m_add->mapToGlobal(QPoint(0, m_add->height())));
}

QStringList ViewTitleBar::viewMenuEntries()
{
    QMenu menu(this);
    fillViewMenu(menu);
    QStringList entries;
    for (QAction* const action : menu.actions())
        if (!action->isSeparator())
            entries << action->text().remove(QLatin1Char('&'));
    return entries;
}

void ViewTitleBar::showViewMenu()
{
    QMenu menu(this);
    fillViewMenu(menu);
    menu.exec(m_menu->mapToGlobal(QPoint(0, m_menu->height())));
}

void ViewTitleBar::fillViewMenu(QMenu& menu)
{
    // The view's own commands first.
    if (QMenu* const own = m_makeMenu ? m_makeMenu() : nullptr) {
        own->setParent(&menu);      // it goes when this menu goes; its commands stay alive until then
        menu.addActions(own->actions());
        menu.addSeparator();
    }
    if (m_dock) {
        QAction* const floating = menu.addAction(tr("Float"));
        floating->setCheckable(true);
        floating->setChecked(m_dock->isFloating());
        connect(floating, &QAction::triggered, m_dock, &QDockWidget::setFloating);
        connect(menu.addAction(tr("Close View")), &QAction::triggered, m_dock, &QDockWidget::close);
    } else {
        QAction* const floating = menu.addAction(tr("Float"));
        floating->setCheckable(true);
        floating->setChecked(m_floating);
        connect(floating, &QAction::triggered, this, &ViewTitleBar::floatRequested);
        // Among the panels: at a side of the window, or as a tab of the panels on the right; and back.
        QMenu* const dock = menu.addMenu(tr("Dock"));
        dock->setObjectName(QStringLiteral("ViewDockMenu"));
        for (auto const& [label, area, tabbed] : {std::tuple<char const*, int, bool>{QT_TR_NOOP("Dock Left"), int(Qt::LeftDockWidgetArea), false},
                                                  {QT_TR_NOOP("Dock Right"), int(Qt::RightDockWidgetArea), false},
                                                  {QT_TR_NOOP("Dock at Bottom"), int(Qt::BottomDockWidgetArea), false},
                                                  {QT_TR_NOOP("Tab with the Panels on the Right"), int(Qt::RightDockWidgetArea), true}}) {
            QAction* const a = dock->addAction(tr(label));
            a->setCheckable(true);
            a->setChecked(!tabbed && m_docked == area);
            connect(a, &QAction::triggered, this, [this, area, tabbed] { emit dockRequested(area, tabbed); });
        }
        dock->addSeparator();
        QAction* const back = dock->addAction(tr("Back to the Middle"));
        back->setEnabled(m_docked != 0 || m_floating);
        connect(back, &QAction::triggered, this, [this] {
            if (m_floating)
                emit floatRequested(false);
            else
                emit dockRequested(0, false);
        });
        connect(menu.addAction(tr("Close View")), &QAction::triggered, this, &ViewTitleBar::closeRequested);
    }
}
