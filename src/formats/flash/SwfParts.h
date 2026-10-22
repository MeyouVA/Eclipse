// Eclipse - the parts of a Flash movie (.swf) as read from its bytes: bits, shapes, morphs, fonts, pictures, sounds, the display list.
#pragma once

#include "formats/flash/SwfImport.h"
#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QTemporaryDir>
#include <QMap>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include "model/scene/Scene.h"
#include "formats/flash/FlashCut.h"
#include "formats/flash/FlashVideo.h"
#include "formats/archive/Zip.h"

namespace swf {

QString tr(char const* text);

class Bits {
public:
    Bits(QByteArray const& b, int pos) : m_b(&b), m_byte(pos) {}
    bool ok() const { return m_byte <= m_b->size(); }
    quint32 ub(int n)
    {
        quint32 v = 0;
        for (int i = 0; i < n; ++i) {
            int const bit = m_byte < m_b->size() ? (quint8((*m_b)[m_byte]) >> (7 - m_bit)) & 1 : 0;
            v = (v << 1) | quint32(bit);
            if (++m_bit == 8) {
                m_bit = 0;
                ++m_byte;
            }
        }
        return v;
    }
    qint32 sb(int n)
    {
        quint32 const v = ub(n);
        if (n > 0 && n < 32 && (v & (1u << (n - 1))))
            return qint32(v) - qint32(1u << n);
        return qint32(v);
    }
    double fb(int n) { return sb(n) / 65536.0; }
    void align()
    {
        if (m_bit) {
            m_bit = 0;
            ++m_byte;
        }
    }
    int byte() const { return m_byte; }

private:
    QByteArray const* m_b;
    int m_byte;
    int m_bit = 0;
};

quint8 u8(QByteArray const& b, int at);

quint16 u16(QByteArray const& b, int at);

quint32 u32(QByteArray const& b, int at);

// A string ended by a zero byte, from `p` on (`p` left past it).
QString text(QByteArray const& b, int& p);

// A number of one to five bytes, seven bits in each, the lowest first.
quint32 encodedU32(QByteArray const& b, int& p);

QTransform matrix(Bits& bits);

// Zlib data unpacked (empty when broken).
QByteArray unzlib(QByteArray const& data);

struct FillStyle {
    QColor colour;
    Swatch gradient;            // (Swatch::Solid: none)
    int bitmap = -1;
    QTransform matrix;
};

inline constexpr int kKeepWidthMark = -77;

struct Shape {
    struct Area {
        FillStyle style;
        QVector<QPolygonF> rings;
    };
    struct Line {
        QColor colour;
        double width = 20.0;
        bool keepWidth = false;     // a line that keeps its thickness however the shape is scaled
        int startCap = 0, endCap = 0, join = 0;    // 0: round; 1: none (flat), 2: square - and 0: round, 1: bevel, 2: mitre
        QPolygonF points;
    };
    QVector<Area> areas;
    QVector<Line> lines;
};

QColor colourAt(QByteArray const& b, int& p, bool alpha);

// A fill style read (and passed over).
FillStyle fillStyle(QByteArray const& b, int& p, int version);

struct LineStyle {
    QColor colour;
    double width = 20.0;
    bool keepWidth = false;
    int startCap = 0, endCap = 0, join = 0;
};

void styles(QByteArray const& b, int& p, int version, QVector<FillStyle>& fills, QVector<LineStyle>& lines);

struct Rec {
    bool edge = false;
    bool move = false;
    QPointF to;
    int fill0 = -1, fill1 = -1, line = -1;      // (-1: as before)
    int fillBase = -1, lineBase = -1;           // new styles: where they start among all of them (-1: no new ones)
    bool curve = false;
    QPointF control, anchor;
};

// The shape records from `p`, as they are; new styles met on the way added to `fills` and `lineStyles`.
QVector<Rec> parseRecords(QByteArray const& b, int p, int version, QVector<FillStyle>& fills, QVector<LineStyle>& lineStyles);

Shape build(QVector<Rec> const& records, QVector<FillStyle> const& fills, QVector<LineStyle> const& lineStyles);

Shape records(QByteArray const& b, int p, int version, QVector<FillStyle> fills, QVector<LineStyle> lineStyles);

// The bounds rectangle at `p`, passed over.
int skipRect(QByteArray const& b, int p);

Shape shape(QByteArray const& b, int version);

struct MorphShape {
    QVector<FillStyle> fills0, fills1;
    QVector<LineStyle> lines0, lines1;
    QVector<Rec> start, end;
};

// A morph fill style: the style at the start and at the end.
void morphFill(QByteArray const& b, int& p, FillStyle& a, FillStyle& z);

// DefineMorphShape (`version` 1) or DefineMorphShape2.
MorphShape morph(QByteArray const& b, int version);

QColor mixed(QColor a, QColor b, double t);

QTransform mixed(QTransform const& a, QTransform const& b, double t);

Shape morphAt(MorphShape const& m, double t);

// A font of the movie: the outline of each of its letters, in units of which `em` make the letter height.
struct Font {
    QVector<Shape> glyphs;
    double em = 1024.0;
};

// DefineFont (`version` 1: only the outlines) or DefineFont2 / DefineFont3 (with a name, a code table and more).
Font font(QByteArray const& b, int version);

// DefineText / DefineText2: runs of letters of a font, each where its advances put it; as a shape (in twips).
Shape textShape(QByteArray const& b, bool alpha, QHash<int, Font> const& fonts);

// A JPEG as the movie keeps it: some movies put an end and a start marker before the picture.
QImage jpeg(QByteArray data);

// DefineBitsLossless (`alpha` false) or DefineBitsLossless2: a colour table and indices, or 15- or 32-bit pixels, zlib-packed.
QImage lossless(QByteArray const& b, bool alpha);

bool soundFile(int format, QByteArray const& data, SwfImport::Sound& out);

// A thing placed on a timeline's display list.
struct Placed {
    int character = 0;
    QTransform matrix;
    // A mask: what it covers (the depths above it up to this one) shows only where it is; it is not seen itself.
    int clipDepth = 0;
    // A colour effect: each colour times so much (in 256ths), and so much more - red, green, blue, opacity.
    int mult[4] = {256, 256, 256, 256};
    int add[4] = {0, 0, 0, 0};
    int ratio = 0;          // a video: the frame of it shown; a morph shape: how far it is from its start (of 65535)
    int since = 0;          // the frame of its timeline it was put there on (a movie clip plays from there)
};

using DisplayList = QMap<int, Placed>;

// A colour effect, as a whole: each colour times so much (of 256), and so much more.
struct ColourFx {
    int mult[4] = {256, 256, 256, 256};
    int add[4] = {0, 0, 0, 0};
    bool plain() const
    {
        for (int k = 0; k < 4; ++k)
            if (mult[k] != 256 || add[k] != 0)
                return false;
        return true;
    }
    QColor apply(QColor c) const
    {
        int v[4] = {c.red(), c.green(), c.blue(), c.alpha()};
        for (int k = 0; k < 4; ++k)
            v[k] = std::clamp(v[k] * mult[k] / 256 + add[k], 0, 255);
        return QColor(v[0], v[1], v[2], v[3]);
    }
    // This after `inner`.
    ColourFx after(ColourFx const& inner) const
    {
        ColourFx f;
        for (int k = 0; k < 4; ++k) {
            f.mult[k] = inner.mult[k] * mult[k] / 256;
            f.add[k] = inner.add[k] * mult[k] / 256 + add[k];
        }
        return f;
    }
    static ColourFx of(Placed const& p)
    {
        ColourFx f;
        for (int k = 0; k < 4; ++k) {
            f.mult[k] = p.mult[k];
            f.add[k] = p.add[k];
        }
        return f;
    }
    QByteArray key() const
    {
        QByteArray k;
        for (int i = 0; i < 4; ++i)
            k += QByteArray::number(mult[i]) + ',' + QByteArray::number(add[i]) + ';';
        return k;
    }
};

// A control tag of a timeline (the main one, or a movie clip's) applied to its display list, on frame `frame` of it.
void control(int code, QByteArray const& body, DisplayList& list, int frame);

}  // namespace swf
