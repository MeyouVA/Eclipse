#pragma once

#include <QWidget>

class QAction;
class BoardDocument;
class QMenu;
class QLabel;
class QTreeWidget;
class QToolButton;

class BoardLayers : public QWidget {
    Q_OBJECT
public:
    explicit BoardLayers(BoardDocument* document, QWidget* parent = nullptr);
    int showPanel(int shot, int panel);
    void refreshSelectedPreview();
    void populateMenu(QMenu* menu);

signals:
    void layerSelected(int row);

private:
    int row() const;
    void editLayer(QString const& operation);
    void updateActions();
    void updatePreview(int row);
    BoardDocument* m_document;
    QTreeWidget* m_list;
    QToolButton* m_add;
    QLabel* m_layerType;
    QAction* m_rename;
    QAction* m_duplicate;
    QAction* m_remove;
    QAction* m_up;
    QAction* m_down;
    int m_shot = -1;
    int m_panel = -1;
    bool m_writingFlags = false;
};
