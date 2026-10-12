// Eclipse - the strip across the top of a panel of views: a menu button at the left, and
// at the right a tab for each view in the panel, a button to add a view and one to close
// the view shown.
#pragma once

#include "EclipseExport.h"

#include <QList>
#include <QWidget>

class QDockWidget;
class QHBoxLayout;
class QMainWindow;
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

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void titleChosen(int index);

private:
    void build();
    QToolButton* tabButton(QString const& text, bool current);
    void showAddMenu();
    void showViewMenu();

    QMainWindow* m_window = nullptr;
    QDockWidget* m_dock = nullptr;
    QList<QDockWidget*> const* m_all = nullptr;
    QStringList m_titles;
    int m_current = 0;
    QHBoxLayout* m_tabs = nullptr;
    QToolButton* m_add = nullptr;
    QToolButton* m_menu = nullptr;
};
