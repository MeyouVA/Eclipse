// Eclipse - the Timeline: what its painting is done with - its size, how far it is scrolled, the frames in view, its colours.
#pragma once

#include <QColor>

class Scene;

struct TimelinePaintSpace {
    Scene const& scene;
    int w, h;           // the viewport
    int sx, sy;         // scrolled so far across and down
    int cw;             // a cell's width
    int frames;         // the frames of the scene (as many as the end being dragged makes)
    int sceneEndX;      // where the scene ends, across
    int firstFrame, lastFrame;      // the frames in view
    QColor bg, rowA, rowB, line, text, dim, exposed, exposedCurrent, currentRow, playhead, headerBg;
};
