// Eclipse - the parts of a Flash document (.fla) as read from its XML: nodes, transforms, tints, colours, line styles, shapes, bitmaps.
#pragma once

#include "formats/flash/FlaImport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QMap>
#include <QPolygonF>
#include <QTransform>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <memory>
#include "model/geometry/Morph.h"
#include "model/scene/Scene.h"
#include "formats/flash/FlashCut.h"
#include "formats/flash/FlashVideo.h"
#include "formats/flash/SwfImport.h"
#include "formats/archive/Zip.h"

namespace fla {

QString tr(char const* text);

// The XML, as a tree.
struct Node {
    QString name;
    QHash<QString, QString> attributes;
    QVector<Node> children;
    QString attr(QString const& key, QString const& fallback = QString()) const { return attributes.value(key, fallback); }
    double number(QString const& key, double fallback) const
    {
        bool ok = false;
        double const v = attributes.value(key).toDouble(&ok);
        return ok ? v : fallback;
    }
    Node const* child(QString const& n) const
    {
        for (Node const& c : children)
            if (c.name == n)
                return &c;
        return nullptr;
    }
};

bool parse(QByteArray const& text, Node& root);

QTransform matrixOf(Node const& element);

// A colour effect: each part times so much, and so much more.
struct Tint {
    double mult[4] = {1, 1, 1, 1};
    double add[4] = {0, 0, 0, 0};
    QColor apply(QColor c) const
    {
        double const v[4] = {double(c.red()), double(c.green()), double(c.blue()), double(c.alpha())};
        int out[4];
        for (int k = 0; k < 4; ++k)
            out[k] = std::clamp(int(std::lround(v[k] * mult[k] + add[k])), 0, 255);
        return QColor(out[0], out[1], out[2], out[3]);
    }
    Tint then(Tint const& inner) const
    {
        // `inner` applied first, then this.
        Tint t;
        for (int k = 0; k < 4; ++k) {
            t.mult[k] = inner.mult[k] * mult[k];
            t.add[k] = inner.add[k] * mult[k] + add[k];
        }
        return t;
    }
};

Tint tintOf(Node const& element);

QColor colourOf(Node const& fill);

// A number of an edge list: a decimal, or #hex.hex (24.8 fixed point, the integer part signed).
double edgeNumber(QString const& s, int& i);

void skipSpaces(QString const& s, int& i);

struct Paint {
    QColor colour;
    int gradient = -1;
    QPointF from, to;
    // A bitmap of the library, and where it lies (from its pixels to the area's coordinates).
    QString bitmap;
    QTransform bitmapPlace;
};

struct LineStyle {
    QColor colour = Qt::black;
    double weight = 1.0;
    int scaling = 0;
    Stroke::Cap cap = Stroke::RoundCap;
    bool square = false;
    Stroke::Join join = Stroke::RoundJoin;
    QVector<QPointF> profile;
    bool ragged = false;
    int waveHeight = 0;
    int waveLength = 2;
    int pattern = 0;
    double dashOn = 0.0;
    double dashOff = 0.0;
};

LineStyle lineStyleOf(Node const& kind);

// How wide a line of that profile is (as a share of its weight) so far along it.
double widthAlong(QVector<QPointF> const& profile, double at);

// A line of so many points, drawn in a style where `place` puts it.
Stroke strokeOf(QPolygonF const& points, LineStyle const& st, QTransform const& place);

QVector<Stroke> styled(Stroke stroke, LineStyle const& st);

// A shape as read: its fills as rings, and its lines, in the shape's own pixels.
struct Shape {
    struct Area {
        Paint paint;
        QVector<QPolygonF> rings;
    };
    struct Line {
        LineStyle style;
        QPolygonF points;
    };
    QVector<Area> areas;
    QVector<Line> lines;
};

Shape readShape(Node const& shape, std::function<Paint(Node const&)> const& paintOf);

QImage bitmapOf(QByteArray const& b, QSizeF* shown);

}  // namespace fla
