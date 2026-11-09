#pragma once

#include <QListView>

class BoardDocument;
class BoardThumbnailModel;

class BoardThumbnails : public QListView {
    Q_OBJECT
public:
    explicit BoardThumbnails(BoardDocument* document, QWidget* parent = nullptr);
    void rebuild();
    void setVertical(bool vertical);
    void setOverview(bool overview);
    void selectPanel(int shot, int panel);
    void refreshPanel(int shot, int panel);

signals:
    void panelSelected(int shot, int panel);
    void panelDropped(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after, bool wholeScene);

protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool acceptsDrag(QDropEvent const* event) const;
    BoardThumbnailModel* m_panels;
    bool m_overview = false;
};
