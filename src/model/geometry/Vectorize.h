// Eclipse - turns a bitmap picture into painted shapes: everything darker than a threshold
// (and not transparent) becomes vector artwork that can be selected, repainted and scaled
// like anything drawn in the program.
#pragma once

#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QVector>

namespace Vectorize {

QVector<QPolygonF> trace(QImage const& image, int threshold = 128, double minimumArea = 4.0);

struct ColourShape {
    QColor colour;
    QVector<QPolygonF> outlines;
};
QVector<ColourShape> traceColours(QImage const& image, int maxColours = 8, QColor const& paper = QColor(255, 255, 255), double minimumArea = 4.0);

struct CentreLine {
    QPolygonF points;
    bool closed = false;
    double width = 1.0;
};
QVector<CentreLine> centreLines(QImage const& image, int threshold = 128, int minimumLength = 4);

}
