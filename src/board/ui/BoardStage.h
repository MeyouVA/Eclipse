#pragma once

#include <QWidget>
#include <QPair>

class BoardDocument;
struct BoardProject;
class Canvas;
class Document;
class QComboBox;
class Scene;

Scene boardPanelScene(BoardProject const& project, int shot, int panel);

class BoardStage : public QWidget {
    Q_OBJECT
public:
    explicit BoardStage(BoardDocument* document, QWidget* parent = nullptr);
    void showPanel(int shot, int panel, bool fit);
    void chooseTool(QString const& tool);
    void fit();
    bool writingArtwork() const { return m_writing; }

signals:
    void message(QString const& text);

private:
    void captureArtwork();
    void chooseLayer(int layer);
    BoardDocument* m_board;
    Document* m_scratch;
    Canvas* m_canvas;
    QComboBox* m_layers;
    QPair<int, int> m_panel{-1, -1};
    bool m_loading = false;
    bool m_writing = false;
};
