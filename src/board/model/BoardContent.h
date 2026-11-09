#pragma once

#include "board/model/BoardCamera.h"
#include "model/scene/Drawing.h"
#include "model/scene/Storyboard.h"

struct BoardLayer {
    enum class Kind { Vector, Bitmap };

    QString name;
    Kind kind = Kind::Vector;
    bool visible = true;
    Drawing drawing;
    bool locked = false;
};

struct BoardPanel : Storyboard::Panel {
    QVector<BoardLayer> layers;
    QVector<BoardCameraKey> camera;
};

struct BoardShot {
    QString name;
    QString sequence;
    QString act;
    QVector<BoardPanel> panels;
    int dissolve = 0;
};

struct BoardSound {
    QString asset;
    QString name;
    int start = 0;
    int length = 0;
    bool muted = false;
    bool operator==(BoardSound const&) const = default;
};

struct BoardSpan {
    int shot;
    int panel;
    int start;
    int length;
};
