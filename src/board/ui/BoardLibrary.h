#pragma once

#include "board/model/BoardContent.h"

#include <QWidget>

class QAction;
class BoardDocument;
class QLabel;
class QListWidget;

class BoardLibrary : public QWidget {
    Q_OBJECT
public:
    explicit BoardLibrary(BoardDocument* document, QWidget* parent = nullptr);
    void setPanel(int shot, int panel, int layer);
    void selectLastItem();

signals:
    void importRequested();

private:
    void refreshItems();
    void refreshSelection();
    void updateActions();
    void captureLayer();
    void placeItem();
    void renameItem();
    void removeItem();

    BoardDocument* m_document;
    QListWidget* m_list;
    QLabel* m_preview;
    QAction* m_import;
    QAction* m_capture;
    QAction* m_place;
    QAction* m_rename;
    QAction* m_remove;
    QVector<BoardLayer> m_items;
    QSize m_projectSize;
    int m_shot = -1;
    int m_panel = -1;
    int m_layer = -1;
};
