#pragma once

#include <QWidget>
#include <functional>

class QDockWidget;
class QLabel;
class QMenu;
class QToolButton;

// A view's title bar: its name as a tab on the right, a menu of the view's own commands, and close.
class BoardDockTitle : public QWidget {
    Q_OBJECT
public:
    explicit BoardDockTitle(QDockWidget* dock, std::function<void(QMenu*)> fillMenu = {});
    QToolButton* menuButton() const { return m_menuButton; }
    QMenu* menu() const { return m_menu; }

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void rebuildMenu();
    QDockWidget* m_dock;
    std::function<void(QMenu*)> m_fill;
    QLabel* m_label;
    QToolButton* m_menuButton;
    QToolButton* m_close;
    QMenu* m_menu;
};
