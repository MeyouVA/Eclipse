#pragma once

#include "board/model/BoardProject.h"

#include <QColor>
#include <QImage>

class BoardRender {
public:
    explicit BoardRender(BoardProject const& project);
    int length() const;
    QImage frame(int frame, QSize size, QColor background, bool deep, bool dissolve = true);

private:
    struct Cached {
        QImage picture;
        int row = -1;
        BoardCameraPose camera;
        QColor background;
        bool deep = false;
    };
    QImage panel(int row, int local, QSize size, QColor background, bool deep, Cached& cache);
    BoardProject const& m_project;
    QVector<BoardSpan> m_spans;
    Cached m_current, m_previous;
};
