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
    ViewTitleBar(QMainWindow* window, QDockWidget* dock, QList<QDockWidget*> const* all);
    // For the view in the middle of the window, which stays where it is.
    explicit ViewTitleBar(QString const& title, QWidget* parent = nullptr);

    void setTitles(QStringList const& titles, int current);
    int currentTitle() const { return m_current; }

    // Shows the tabs of the views that now share the panel.
    void refresh();
    // The names on the tabs, left to right.
    QStringList tabs() const;

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
