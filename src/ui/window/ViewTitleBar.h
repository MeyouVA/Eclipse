// Eclipse - the strip across the top of a panel of views: a menu button at the left, and
// at the right a tab for each view in the panel, a button to add a view and one to close
// the view shown.
#pragma once

#include "EclipseExport.h"

#include <functional>

#include <QList>
#include <QWidget>

class QDockWidget;
class QHBoxLayout;
class QMainWindow;
class QMenu;
class QToolButton;

class ECLIPSE_UI_API ViewTitleBar : public QWidget {
    Q_OBJECT

public:
    // For a view that can be moved about.  `all` is every such view of the window, in the
    // order their tabs are to be shown.
    ViewTitleBar(QMainWindow* window, QDockWidget* dock, QList<QDockWidget*> const* all);
    // For the view in the middle of the window, which stays where it is.
    explicit ViewTitleBar(QString const& title, QWidget* parent = nullptr);

    // For the view in the middle: the kinds of view it can be (Camera, Drawing), as tabs,
    // and which of them it is now.  A tab pressed says so with titleChosen.
    void setTitles(QStringList const& titles, int current);
    int currentTitle() const { return m_current; }

    // Shows the tabs of the views that now share the panel.
    void refresh();
    // The names on the tabs, left to right.
    QStringList tabs() const;

    // The menu of the view itself (the Timeline's, the Xsheet's ...): what the menu button at the
    // left shows, before Float and Close.  `make` builds it afresh each time; the bar owns what it gets.
    void setViewMenu(std::function<QMenu*()> make) { m_makeMenu = std::move(make); }
    // What the menu button offers, top to bottom (separators left out).
    QStringList viewMenuEntries();
    // For the view in the middle: whether it is out in a window of its own just now (the tick beside Float).
    void setFloating(bool floating) { m_floating = floating; }
    // ... or docked among the panels (on which side; Qt::NoDockWidgetArea: not).
    void setDocked(int area) { m_docked = area; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void titleChosen(int index);
    // The view in the middle is asked out into a window of its own (or back), or to be closed.
    void floatRequested(bool floating);
    void closeRequested();
    // The view in the middle asked to a side of the window among the panels (Qt::DockWidgetArea; with `tabbed`, as a tab of
    // the panels there), or (Qt::NoDockWidgetArea) back into the middle.
    void dockRequested(int area, bool tabbed);

public:
    // The view's menu: its own commands, then floating, docking, renaming and closing.
    void fillViewMenu(QMenu& menu);

private:
    void build();
    QToolButton* tabButton(QString const& text, bool current);
    void showAddMenu();
    void showViewMenu();
    std::function<QMenu*()> m_makeMenu;

    QMainWindow* m_window = nullptr;
    QDockWidget* m_dock = nullptr;
    QList<QDockWidget*> const* m_all = nullptr;
    QStringList m_titles;
    int m_current = 0;
    bool m_floating = false;
    int m_docked = 0;
    QHBoxLayout* m_tabs = nullptr;
    QToolButton* m_add = nullptr;
    QToolButton* m_menu = nullptr;
};
