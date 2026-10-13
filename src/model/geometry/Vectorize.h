// Eclipse - turns a bitmap picture into painted shapes: everything darker than a threshold
// (and not transparent) becomes vector artwork that can be selected, repainted and scaled
// like anything drawn in the program.
#pragma once

#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QVector>

namespace Vectorize {

// The outlines (odd-even: an outline inside another is a hole) of the dark parts of
// `image`, in the coordinates of its pixels.  A pixel is dark when its lightness
// (0..255) is under `threshold` and it is at least half opaque.  Specks of fewer than
// `minimumArea` pixels are left out.
QVector<QPolygonF> trace(QImage const& image, int threshold = 128, double minimumArea = 4.0);

// A picture in colour, as flat shapes: its colours are brought down to `maxColours` at
// most, and the area of each becomes a shape of that colour.  Pixels of the `paper`
// colour (or near it) and transparent ones are left out.  The shapes come largest first,
// each reaching a pixel under its neighbours, so that painted in order they leave no gaps.
struct ColourShape {
    QColor colour;
    QVector<QPolygonF> outlines;
};
QVector<ColourShape> traceColours(QImage const& image, int maxColours = 8, QColor const& paper = QColor(255, 255, 255), double minimumArea = 4.0);

// The dark parts of a picture as lines: each stroke of the picture is thinned to the line that runs down its
// middle, with how thick the stroke was there (the average along it), in the coordinates of its pixels.  Where
// strokes meet or cross, the lines end at the meeting.  Loose ends shorter than `minimumLength` pixels are
// left out.
struct CentreLine {
    QPolygonF points;
    bool closed = false;
    double width = 1.0;
};
QVector<CentreLine> centreLines(QImage const& image, int threshold = 128, int minimumLength = 4);

}
