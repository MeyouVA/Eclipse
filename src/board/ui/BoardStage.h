#pragma once

#include "board/model/BoardCamera.h"

#include <QWidget>
#include <QPair>

class BoardDocument;
class BoardCameraCanvas;
class BoardPreview;
class QAction;
class QLabel;
class QToolBar;
struct BoardProject;
class Document;
class Scene;
class QStackedLayout;

Scene boardPanelScene(BoardProject const& project, int shot, int panel, BoardCameraPose const& camera = {});

class BoardStage : public QWidget {
    Q_OBJECT
public:
    explicit BoardStage(BoardDocument* document, QWidget* parent = nullptr);
    void showPanel(int shot, int panel, int layer, bool fit);
    void chooseLayer(int layer);
    void chooseTool(QString const& tool);
    void fit();
    void zoom(bool in);
    void setToolLabel(QString const& text);
    QWidget* createToolProperties(QWidget* parent);
    void setViewActions(QList<QAction*> const& actions);
    void showPreview(QPair<int, int> panel, int frame, QPair<int, int> previous, double mix);
    void hidePreview();
    void setCameraFrame(int frame);
    void showCameraGuide(bool show);
    bool cameraTool() const;
    bool writingArtwork() const { return m_writing; }

signals:
    void message(QString const& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void captureArtwork();
    BoardDocument* m_board;
    Document* m_scratch;
    BoardCameraCanvas* m_canvas;
    BoardPreview* m_preview;
    QStackedLayout* m_stack;
    QToolBar* m_viewBar;
    QLabel* m_zoomLabel;
    QLabel* m_layerLabel;
    QLabel* m_toolLabel;
    int m_layer = -1;
    int m_frame = 0;
    QPair<int, int> m_panel{-1, -1};
    bool m_loading = false;
    bool m_writing = false;
};
