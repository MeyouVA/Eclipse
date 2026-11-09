#pragma once

#include "board/model/BoardCamera.h"

#include <QPixmap>
#include <QWidget>

struct BoardProject;

class BoardPreview : public QWidget {
public:
    explicit BoardPreview(QWidget* parent = nullptr);
    void showFrame(BoardProject const& project, QPair<int, int> panel, int frame, QPair<int, int> previous, double mix);
    void clear();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QPixmap m_current;
    QPixmap m_previous;
    QPair<int, int> m_panel{-1, -1};
    QPair<int, int> m_before{-1, -1};
    BoardCameraPose m_pose;
    double m_mix = 1.0;
};
