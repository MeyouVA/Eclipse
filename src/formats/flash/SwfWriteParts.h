// Eclipse - the Flash movie writer's parts: bits, tags, matrices, colour effects, filters, and shapes of many styles.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QPainterPath>
#include <QPair>
#include <QPoint>
#include <QTransform>
#include <QVector>

#include "formats/flash/FlashFilter.h"

namespace swfout {

// Bits written one after another, most significant first, as the file's records want them.
class Bits {
public:
    void put(quint32 value, int count);
    void putSigned(qint32 value, int count) { put(quint32(value) & ((count >= 32) ? 0xffffffffu : ((1u << count) - 1u)), count); }
    // The rest of the last byte filled with noughts.
    QByteArray bytes();
    // To the next byte; then whole bytes.
    void align();
    void raw(QByteArray const& bytes);

private:
    void flush();
    QByteArray m_out;
    quint8 m_byte = 0;
    int m_used = 0;
};

// How many bits a signed number takes (with its sign).
int signedBits(qint32 v);
void u8(QByteArray& out, int v);
void u16(QByteArray& out, quint16 v);
void u32(QByteArray& out, quint32 v);
QByteArray rect(qint32 xmin, qint32 xmax, qint32 ymin, qint32 ymax);
QByteArray tag(int code, QByteArray const& body);
// A MATRIX record: a placement whose moves are in twips.
QByteArray matrix(QTransform const& m);

// A colour effect: each of red, green, blue, opacity times so much (of 256), and so much more.
struct ColourEffect {
    int mult[4] = {256, 256, 256, 256};
    int add[4] = {0, 0, 0, 0};
    bool plain() const;
    bool operator==(ColourEffect const& o) const;
    // This after `inner`.
    ColourEffect after(ColourEffect const& inner) const;
};
// A CXFORMWITHALPHA record.
QByteArray colourEffect(ColourEffect const& c);
// A FILTERLIST: the blurs, glows and drop shadows among them.
QByteArray filterList(QVector<Flash::Filter> const& filters);

// The style an area or a line is painted in.
struct Style {
    enum Kind { Solid, Linear, Radial, Bitmap };
    Kind kind = Solid;
    QColor colour = Qt::black;
    QVector<QPair<double, QColor>> stops;    // a gradient's colours, and where they stand (0..1)
    QTransform matrix;                      // the gradient's square, or the bitmap's pixels, into the shape's twips
    int bitmap = 0;
    bool repeat = false;
};

// One thing painted: an area (its outlines, filled odd-even) or a line (where it runs); in twips.
struct Paint {
    bool line = false;
    Style style;
    QVector<QVector<QPoint>> rings;
    QPainterPath path;
    int width = 20;
    int startCap = 0, endCap = 0;   // Flash's: 0 round, 1 none, 2 square
    int join = 0;                   // Flash's: 0 round, 1 bevel, 2 mitre
    double mitre = 8.0;
    bool noScale = false;           // its thickness the same however the movie is scaled
};

// The body of a DefineShape4 (its id first): the paints, each over the ones before it.
QByteArray shape(int id, QVector<Paint> const& paints);

}  // namespace swfout
