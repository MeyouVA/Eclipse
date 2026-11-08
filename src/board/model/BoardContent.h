#pragma once

#include "model/scene/Drawing.h"
#include "model/scene/Storyboard.h"

struct BoardLayer {
    enum class Kind { Vector, Bitmap };

    QString name;
    Kind kind = Kind::Vector;
    bool visible = true;
    Drawing drawing;
};

struct BoardPanel : Storyboard::Panel {
    QVector<BoardLayer> layers;
};

struct BoardShot {
    QString name;
    QString sequence;
    QString act;
    QVector<BoardPanel> panels;
};
