// Eclipse - the Flash movie reader.
#include "ui/io/SwfImport.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"

namespace {

QString tr(char const* text)
{
    return QCoreApplication::translate("SwfImport", text);
}

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

quint16 u16(QByteArray const& b, int at)
{
    return at + 1 < b.size() ? quint16(quint8(b[at]) | (quint8(b[at + 1]) << 8)) : 0;
}

quint32 u32(QByteArray const& b, int at)
{
    return at + 3 < b.size() ? quint32(quint8(b[at])) | (quint32(quint8(b[at + 1])) << 8) | (quint32(quint8(b[at + 2])) << 16) | (quint32(quint8(b[at + 3])) << 24) : 0;
}

QTransform matrix(Bits& bits)
{
    double sx = 1.0, sy = 1.0, r0 = 0.0, r1 = 0.0;
    if (bits.ub(1)) {
        int const n = int(bits.ub(5));
        sx = bits.fb(n);
        sy = bits.fb(n);
    }
    if (bits.ub(1)) {
        int const n = int(bits.ub(5));
        r0 = bits.fb(n);
        r1 = bits.fb(n);
    }
    int const n = int(bits.ub(5));
    double const tx = bits.sb(n), ty = bits.sb(n);
    bits.align();
    return QTransform(sx, r0, r1, sy, tx, ty);
}

// A shape as read: its fills (rings of points, in twips) and lines.
struct Shape {
    struct Area {
        QColor colour;
        QVector<QPolygonF> rings;
    };
    struct Line {
        QColor colour;
        double width = 20.0;
        bool keepWidth = false;     // a line that keeps its thickness however the shape is scaled
        QPolygonF points;
    };
    QVector<Area> areas;
    QVector<Line> lines;
};

QColor colourAt(QByteArray const& b, int& p, bool alpha)
{
    QColor const c(quint8(b[p]), quint8(b[p + 1]), quint8(b[p + 2]), alpha ? quint8(b[p + 3]) : 255);
    p += alpha ? 4 : 3;
    return c;
}

// A fill style read (and passed over): its colour - a solid one, a gradient's first, grey for a bitmap.
QColor fillStyle(QByteArray const& b, int& p, int version)
{
    bool const alpha = version >= 3;
    int const type = quint8(b[p++]);
    if (type == 0x00)
        return colourAt(b, p, alpha);
    if (type == 0x10 || type == 0x12 || type == 0x13) {
        Bits bits(b, p);
        matrix(bits);
        p = bits.byte();
        int const count = quint8(b[p++]) & 0x0f;
        QColor first;
        for (int i = 0; i < count; ++i) {
            ++p;    // ratio
            QColor const c = colourAt(b, p, alpha);
            if (i == 0)
                first = c;
        }
        if (type == 0x13)
            p += 2;     // focal point
        return first.isValid() ? first : QColor(Qt::gray);
    }
    // A bitmap fill.
    p += 2;
    Bits bits(b, p);
    matrix(bits);
    p = bits.byte();
    return QColor(128, 128, 128);
}

struct LineStyle {
    QColor colour;
    double width = 20.0;
    bool keepWidth = false;
};

void styles(QByteArray const& b, int& p, int version, QVector<QColor>& fills, QVector<LineStyle>& lines)
{
    int count = quint8(b[p++]);
    if (count == 0xff && version >= 2) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i)
        fills.push_back(fillStyle(b, p, version));
    count = quint8(b[p++]);
    if (count == 0xff) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i) {
        double const width = u16(b, p);
        p += 2;
        if (version >= 4) {
            // LINESTYLE2: its flags, a mitre limit, then a colour or a fill.
            quint16 const flags = u16(b, p);
            p += 2;
            if (((flags >> 4) & 3) == 2)
                p += 2;
            // (Scaled neither across nor down: as thick on every scale.)
            bool const keep = (flags & 0x0004) && (flags & 0x0002);
            if (flags & 0x0008)
                lines.push_back({fillStyle(b, p, version), width, keep});
            else
                lines.push_back({colourAt(b, p, true), width, keep});
        } else {
            lines.push_back({colourAt(b, p, version >= 3), width, false});
        }
    }
}

// The shape records: edges with the fill on their left (fill 0) and right (fill 1); joined into rings, fill by fill.
Shape shape(QByteArray const& b, int version)
{
    int p = 2;
    Bits head(b, p);
    int const nb = int(head.ub(5));
    for (int i = 0; i < 4; ++i)
        head.sb(nb);
    head.align();
    p = head.byte();
    if (version == 4) {
        Bits edge(b, p);
        int const n = int(edge.ub(5));
        for (int i = 0; i < 4; ++i)
            edge.sb(n);
        edge.align();
        p = edge.byte() + 1;    // and its flags
    }
    QVector<QColor> fills;
    QVector<LineStyle> lineStyles;
    styles(b, p, version, fills, lineStyles);
    Bits bits(b, p);
    int fillBits = int(bits.ub(4)), lineBits = int(bits.ub(4));
    struct Edge {
        QPolygonF points;
        int fill0, fill1;
    };
    QVector<Edge> edges;
    Shape out;
    int fill0 = 0, fill1 = 0, line = 0, base = 0, lineBase = 0;
    QPointF at;
    int currentLine = -1;
    for (int guard = 0; guard < 2000000 && bits.ok(); ++guard) {
        if (bits.ub(1) == 0) {
            int const flags = int(bits.ub(5));
            if (flags == 0)
                break;
            if (flags & 1) {
                int const n = int(bits.ub(5));
                double const x = bits.sb(n);
                at = QPointF(x, bits.sb(n));
            }
            if (flags & 2)
                fill0 = int(bits.ub(fillBits));
            if (flags & 4)
                fill1 = int(bits.ub(fillBits));
            if (flags & 8)
                line = int(bits.ub(lineBits));
            currentLine = -1;
            if (flags & 16) {
                // New styles: those that follow count from here.
                bits.align();
                int q = bits.byte();
                base = int(fills.size());
                lineBase = int(lineStyles.size());
                styles(b, q, version, fills, lineStyles);
                bits = Bits(b, q);
                fillBits = int(bits.ub(4));
                lineBits = int(bits.ub(4));
            }
            continue;
        }
        QPolygonF piece{at};
        if (bits.ub(1)) {
            int const n = int(bits.ub(4)) + 2;
            double dx = 0.0, dy = 0.0;
            if (bits.ub(1)) {
                dx = bits.sb(n);
                dy = bits.sb(n);
            } else if (bits.ub(1)) {
                dy = bits.sb(n);
            } else {
                dx = bits.sb(n);
            }
            at += QPointF(dx, dy);
            piece << at;
        } else {
            int const n = int(bits.ub(4)) + 2;
            double const cx = bits.sb(n), cy = bits.sb(n), ax = bits.sb(n), ay = bits.sb(n);
            QPointF const control = at + QPointF(cx, cy), end = control + QPointF(ax, ay), from = at;
            for (int s = 1; s <= 8; ++s) {
                double const t = s / 8.0;
                piece << from * ((1 - t) * (1 - t)) + control * (2 * t * (1 - t)) + end * (t * t);
            }
            at = end;
        }
        auto styleOf = [&](int index) { return index == 0 ? 0 : index + base; };
        if (fill0 != 0 || fill1 != 0)
            edges.push_back({piece, styleOf(fill0), styleOf(fill1)});
        if (line != 0 && line + lineBase - 1 < lineStyles.size()) {
            if (currentLine < 0) {
                Shape::Line l;
                l.colour = lineStyles[line + lineBase - 1].colour;
                l.width = lineStyles[line + lineBase - 1].width;
                l.keepWidth = lineStyles[line + lineBase - 1].keepWidth;
                l.points = piece;
                out.lines.push_back(l);
                currentLine = int(out.lines.size()) - 1;
            } else {
                out.lines[currentLine].points += piece.mid(1);
            }
        }
    }
    // Each fill: its edges, turned so that it lies on their right, joined end to start into rings.
    for (int s = 1; s <= fills.size(); ++s) {
        QVector<QPolygonF> pieces;
        for (Edge const& e : std::as_const(edges)) {
            if (e.fill1 == s)
                pieces.push_back(e.points);
            if (e.fill0 == s) {
                QPolygonF back = e.points;
                std::reverse(back.begin(), back.end());
                pieces.push_back(back);
            }
        }
        if (pieces.isEmpty())
            continue;
        auto key = [](QPointF p) { return qint64(std::llround(p.x())) * 4000003 + qint64(std::llround(p.y())); };
        QMultiHash<qint64, int> starting;
        for (int i = 0; i < pieces.size(); ++i)
            starting.insert(key(pieces[i].first()), i);
        QVector<bool> used(pieces.size(), false);
        Shape::Area area;
        area.colour = fills[s - 1];
        for (int i = 0; i < pieces.size(); ++i) {
            if (used[i])
                continue;
            QPolygonF ring = pieces[i];
            used[i] = true;
            for (int guard = 0; guard < pieces.size() && key(ring.last()) != key(ring.first()); ++guard) {
                int next = -1;
                for (auto it = starting.constFind(key(ring.last())); it != starting.cend() && it.key() == key(ring.last()); ++it)
                    if (!used[*it]) {
                        next = *it;
                        break;
                    }
                if (next < 0)
                    break;
                used[next] = true;
                ring += pieces[next].mid(1);
            }
            if (ring.size() > 1 && key(ring.first()) == key(ring.last()))
                ring.removeLast();
            if (ring.size() >= 3)
                area.rings.push_back(ring);
        }
        if (!area.rings.isEmpty())
            out.areas.push_back(area);
    }
    return out;
}

}  // namespace

namespace SwfImport {

bool read(QByteArray const& bytes, Movie& out, QString* error)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    if (bytes.size() < 8)
        return fail(tr("This is not a Flash movie."));
    QByteArray data;
    if (bytes.startsWith("FWS")) {
        data = bytes;
    } else if (bytes.startsWith("CWS")) {
        // The rest is zlib: unpacked to the length the header gives.
        quint32 const length = u32(bytes, 4);
        QByteArray packed;
        packed.append(char((length - 8) >> 24));
        packed.append(char(((length - 8) >> 16) & 0xff));
        packed.append(char(((length - 8) >> 8) & 0xff));
        packed.append(char((length - 8) & 0xff));
        packed += bytes.mid(8);
        data = bytes.left(8) + qUncompress(packed);
        if (data.size() <= 8)
            return fail(tr("The compressed movie could not be unpacked."));
    } else if (bytes.startsWith("ZWS")) {
        return fail(tr("This movie is compressed with LZMA, which Eclipse cannot read. Save it from Flash or Animate without that compression."));
    } else {
        return fail(tr("This is not a Flash movie."));
    }
    Bits head(data, 8);
    int const n = int(head.ub(5));
    double const xmin = head.sb(n), xmax = head.sb(n), ymin = head.sb(n), ymax = head.sb(n);
    head.align();
    int p = head.byte();
    out.size = QSize(std::max(1, int(std::lround((xmax - xmin) / 20.0))), std::max(1, int(std::lround((ymax - ymin) / 20.0))));
    out.fps = std::max(1.0, u16(data, p) / 256.0);
    p += 4;
    out.frames.clear();
    QHash<int, Shape> shapes;
    struct Placed {
        int character = 0;
        QTransform matrix;
        // A mask: what it covers (the depths above it up to this one) shows only where it is; it is not seen itself.
        int clipDepth = 0;
        // A colour effect: each colour times so much (in 256ths), and so much more - red, green, blue, opacity.
        int mult[4] = {256, 256, 256, 256};
        int add[4] = {0, 0, 0, 0};
    };
    QMap<int, Placed> list;     // the display list, by depth
    QTransform const toPixels = QTransform::fromTranslate(-xmin, -ymin) * QTransform::fromScale(1.0 / 20.0, 1.0 / 20.0);
    while (p + 2 <= data.size()) {
        quint16 const tagHead = u16(data, p);
        p += 2;
        int const code = tagHead >> 6;
        int size = tagHead & 0x3f;
        if (size == 0x3f) {
            size = int(u32(data, p));
            p += 4;
        }
        if (size < 0 || p + size > data.size())
            break;
        QByteArray const body = data.mid(p, size);
        p += size;
        if (code == 0)
            break;
        if (code == 2 || code == 22 || code == 32 || code == 83) {
            int const version = code == 2 ? 1 : code == 22 ? 2 : code == 32 ? 3 : 4;
            shapes.insert(u16(body, 0), shape(body, version));
        } else if (code == 26 || code == 70) {
            int q = 0;
            int const flags = quint8(body[q++]);
            int flags2 = 0;
            if (code == 70)
                flags2 = quint8(body[q++]);
            int const depth = u16(body, q);
            q += 2;
            if (code == 70 && ((flags2 & 0x08) || ((flags2 & 0x10) && (flags & 0x02))))
                while (q < body.size() && body[q++] != 0) {}       // a class name
            Placed& at = list[depth];
            if (flags & 0x02) {
                at.character = u16(body, q);
                q += 2;
                if (!(flags & 0x01))
                    at.matrix = QTransform();
            }
            if (flags & 0x04) {
                Bits bits(body, q);
                at.matrix = matrix(bits);
                q = bits.byte();
            }
            if (flags & 0x08) {
                // A colour effect (with opacity).
                Bits bits(body, q);
                bool const adds = bits.ub(1), mults = bits.ub(1);
                int const termBits = int(bits.ub(4));
                for (int c = 0; c < 4; ++c) {
                    at.mult[c] = 256;
                    at.add[c] = 0;
                }
                if (mults)
                    for (int c = 0; c < 4; ++c)
                        at.mult[c] = bits.sb(termBits);
                if (adds)
                    for (int c = 0; c < 4; ++c)
                        at.add[c] = bits.sb(termBits);
                bits.align();
                q = bits.byte();
            }
            if (flags & 0x10)
                q += 2;     // a ratio
            if (flags & 0x20)
                while (q < body.size() && body[q++] != 0) {}       // a name
            if (flags & 0x40)
                at.clipDepth = u16(body, q);
        } else if (code == 4) {
            Placed placed;
            placed.character = u16(body, 0);
            Bits bits(body, 4);
            placed.matrix = matrix(bits);
            list[u16(body, 2)] = placed;
        } else if (code == 28) {
            list.remove(u16(body, 0));
        } else if (code == 5) {
            list.remove(u16(body, 2));
        } else if (code == 1) {
            // The frame as the display list has it: from the deepest up, each on top of the one before - what lies under a
            // mask cut to it, the mask itself not seen.
            Drawing drawing;
            int order = 0;
            struct Mask {
                int upTo;
                QVector<QPolygonF> area;
            };
            QVector<Mask> masks;
            for (auto it = list.cbegin(); it != list.cend(); ++it) {
                masks.removeIf([&](Mask const& m) { return it.key() > m.upTo; });
                auto const found = shapes.constFind(it->character);
                if (found == shapes.cend())
                    continue;
                QTransform const place = it->matrix * toPixels;
                if (it->clipDepth > 0) {
                    Mask mask;
                    mask.upTo = it->clipDepth;
                    for (Shape::Area const& area : found->areas)
                        for (QPolygonF const& ring : area.rings)
                            mask.area.push_back(place.map(ring));
                    masks.push_back(mask);
                    continue;
                }
                ++order;
                auto colour = [&](QColor c) {
                    int v[4] = {c.red(), c.green(), c.blue(), c.alpha()};
                    for (int k = 0; k < 4; ++k)
                        v[k] = std::clamp(v[k] * it->mult[k] / 256 + it->add[k], 0, 255);
                    return QColor(v[0], v[1], v[2], v[3]);
                };
                // Kept, or cut to each mask it lies under.
                auto masked = [&](Fill const& fill) {
                    QVector<Fill> pieces{fill};
                    for (Mask const& m : std::as_const(masks)) {
                        QVector<Fill> cut;
                        for (Fill const& piece : std::as_const(pieces))
                            cut += piece.within(m.area, 0.25);
                        pieces = cut;
                    }
                    for (Fill& piece : pieces) {
                        piece.color = fill.color;
                        piece.order = order;
                        drawing.fills.push_back(piece);
                    }
                };
                for (Shape::Area const& area : found->areas) {
                    Fill fill;
                    fill.color = colour(area.colour);
                    fill.order = order;
                    for (QPolygonF const& ring : area.rings)
                        fill.outlines.push_back(place.map(ring));
                    if (masks.isEmpty())
                        drawing.fills.push_back(fill);
                    else
                        masked(fill);
                }
                for (Shape::Line const& line : found->lines) {
                    Stroke stroke;
                    stroke.color = colour(line.colour);
                    stroke.order = order;
                    stroke.width = float(std::max(0.5, line.width / 20.0 * (line.keepWidth ? 1.0 : std::sqrt(std::abs(it->matrix.determinant())))));
                    for (QPointF const& point : place.map(line.points))
                        stroke.points.push_back({point, 1.0f});
                    if (stroke.points.size() < 2)
                        continue;
                    if (masks.isEmpty()) {
                        drawing.strokes.push_back(stroke);
                    } else {
                        // (A line under a mask is cut as the area it covers.)
                        Fill asArea = Fill::fromStroke(stroke, 0.25);
                        asArea.color = stroke.color;
                        masked(asArea);
                    }
                }
            }
            out.frames.push_back(drawing);
        }
    }
    bool any = false;
    for (Drawing const& d : out.frames)
        any = any || !d.fills.isEmpty() || !d.strokes.isEmpty();
    if (!any)
        return fail(tr("The movie has no shapes on its main timeline that Eclipse can read (only code, movie clips, text or bitmaps)."));
    return true;
}

}
