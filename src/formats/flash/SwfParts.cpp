// Eclipse - reading a Flash movie (.swf): its numbers, text, matrices, colours, styles and shapes.
#include "formats/flash/SwfParts.h"

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

QString tr(char const* text)
{
    return QCoreApplication::translate("SwfImport", text);
}

quint8 u8(QByteArray const& b, int at)
{
    return at >= 0 && at < b.size() ? quint8(b[at]) : 0;
}

quint16 u16(QByteArray const& b, int at)
{
    return at + 1 < b.size() ? quint16(quint8(b[at]) | (quint8(b[at + 1]) << 8)) : 0;
}

quint32 u32(QByteArray const& b, int at)
{
    return at + 3 < b.size() ? quint32(quint8(b[at])) | (quint32(quint8(b[at + 1])) << 8) | (quint32(quint8(b[at + 2])) << 16) | (quint32(quint8(b[at + 3])) << 24) : 0;
}

QString text(QByteArray const& b, int& p)
{
    int const start = p;
    while (p < b.size() && b[p] != 0)
        ++p;
    QString const s = QString::fromUtf8(b.mid(start, p - start));
    ++p;
    return s;
}

quint32 encodedU32(QByteArray const& b, int& p)
{
    quint32 v = 0;
    for (int i = 0; i < 5 && p < b.size(); ++i) {
        quint8 const c = quint8(b[p++]);
        v |= quint32(c & 0x7f) << (7 * i);
        if (!(c & 0x80))
            break;
    }
    return v;
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

QByteArray unzlib(QByteArray const& data)
{
    if (data.size() < 6)
        return QByteArray();
    bool ok = false;
    QByteArray const out = Zip::inflate(data.mid(2), &ok);
    return ok ? out : QByteArray();
}

QColor colourAt(QByteArray const& b, int& p, bool alpha)
{
    QColor const c(u8(b, p), u8(b, p + 1), u8(b, p + 2), alpha ? u8(b, p + 3) : 255);
    p += alpha ? 4 : 3;
    return c;
}

FillStyle fillStyle(QByteArray const& b, int& p, int version)
{
    bool const alpha = version >= 3;
    int const type = u8(b, p++);
    FillStyle style;
    if (type == 0x00) {
        style.colour = colourAt(b, p, alpha);
        return style;
    }
    if (type == 0x10 || type == 0x12 || type == 0x13) {
        Bits bits(b, p);
        style.matrix = matrix(bits);
        p = bits.byte();
        int const count = u8(b, p++) & 0x0f;
        style.gradient.kind = type == 0x10 ? Swatch::Linear : Swatch::Radial;
        for (int i = 0; i < count; ++i) {
            double const at = u8(b, p++) / 255.0;
            QColor const c = colourAt(b, p, alpha);
            if (i == 0)
                style.colour = c;
            style.gradient.stops.push_back({at, c});
        }
        style.gradient.color = style.colour;
        style.gradient.name = tr("Flash gradient");
        if (style.gradient.stops.size() < 2)
            style.gradient.kind = Swatch::Solid;
        if (type == 0x13)
            p += 2;     // focal point
        if (!style.colour.isValid())
            style.colour = QColor(Qt::gray);
        return style;
    }
    // A bitmap fill (repeated or clipped, smoothed or not).
    int const id = u16(b, p);
    p += 2;
    Bits bits(b, p);
    style.matrix = matrix(bits);
    p = bits.byte();
    style.colour = QColor(128, 128, 128);
    style.bitmap = id == 0xffff ? -1 : id;
    return style;
}

void styles(QByteArray const& b, int& p, int version, QVector<FillStyle>& fills, QVector<LineStyle>& lines)
{
    int count = u8(b, p++);
    if (count == 0xff && version >= 2) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i)
        fills.push_back(fillStyle(b, p, version));
    count = u8(b, p++);
    if (count == 0xff) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i) {
        LineStyle line;
        line.width = u16(b, p);
        p += 2;
        if (version >= 4) {
            // LINESTYLE2: its flags (caps, join, a fill, how it scales), a mitre limit, then a colour or a fill.
            quint16 const flags = u16(b, p);
            p += 2;
            line.startCap = (flags >> 6) & 3;
            line.join = (flags >> 4) & 3;
            line.endCap = (flags >> 8) & 3;
            if (line.join == 2)
                p += 2;
            // (Scaled neither across nor down: as thick on every scale.)
            line.keepWidth = (flags & 0x0004) && (flags & 0x0002);
            line.colour = (flags & 0x0008) ? fillStyle(b, p, version).colour : colourAt(b, p, true);
        } else {
            line.colour = colourAt(b, p, version >= 3);
        }
        lines.push_back(line);
    }
}

QVector<Rec> parseRecords(QByteArray const& b, int p, int version, QVector<FillStyle>& fills, QVector<LineStyle>& lineStyles)
{
    QVector<Rec> out;
    Bits bits(b, p);
    int fillBits = int(bits.ub(4)), lineBits = int(bits.ub(4));
    for (int guard = 0; guard < 2000000 && bits.ok(); ++guard) {
        Rec r;
        if (bits.ub(1) == 0) {
            int const flags = int(bits.ub(5));
            if (flags == 0)
                break;
            if (flags & 1) {
                int const n = int(bits.ub(5));
                double const x = bits.sb(n);
                r.move = true;
                r.to = QPointF(x, bits.sb(n));
            }
            if (flags & 2)
                r.fill0 = int(bits.ub(fillBits));
            if (flags & 4)
                r.fill1 = int(bits.ub(fillBits));
            if (flags & 8)
                r.line = int(bits.ub(lineBits));
            if (flags & 16) {
                // New styles: those that follow count from here.
                bits.align();
                int q = bits.byte();
                r.fillBase = int(fills.size());
                r.lineBase = int(lineStyles.size());
                styles(b, q, version, fills, lineStyles);
                bits = Bits(b, q);
                fillBits = int(bits.ub(4));
                lineBits = int(bits.ub(4));
            }
            out.push_back(r);
            continue;
        }
        r.edge = true;
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
            r.anchor = QPointF(dx, dy);
        } else {
            int const n = int(bits.ub(4)) + 2;
            double const cx = bits.sb(n), cy = bits.sb(n), ax = bits.sb(n), ay = bits.sb(n);
            r.curve = true;
            r.control = QPointF(cx, cy);
            r.anchor = QPointF(ax, ay);
        }
        out.push_back(r);
    }
    return out;
}

Shape build(QVector<Rec> const& records, QVector<FillStyle> const& fills, QVector<LineStyle> const& lineStyles)
{
    struct Edge {
        QPolygonF points;
        int fill0, fill1;
    };
    QVector<Edge> edges;
    Shape out;
    int fill0 = 0, fill1 = 0, line = 0, base = 0, lineBase = 0;
    QPointF at;
    int currentLine = -1;
    QVector<int> groupStarts{0};    // where the fill styles of each set of styles start
    for (Rec const& r : records) {
        if (!r.edge) {
            if (r.move)
                at = r.to;
            if (r.fillBase >= 0) {
                base = r.fillBase;
                lineBase = r.lineBase;
                groupStarts.push_back(r.fillBase);
            }
            if (r.fill0 >= 0)
                fill0 = r.fill0;
            if (r.fill1 >= 0)
                fill1 = r.fill1;
            if (r.line >= 0)
                line = r.line;
            currentLine = -1;
            continue;
        }
        QPolygonF piece{at};
        if (!r.curve) {
            at += r.anchor;
            piece << at;
        } else {
            QPointF const control = at + r.control, end = control + r.anchor, from = at;
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
                LineStyle const& style = lineStyles[line + lineBase - 1];
                Shape::Line l;
                l.colour = style.colour;
                l.width = style.width;
                l.keepWidth = style.keepWidth;
                l.startCap = style.startCap;
                l.endCap = style.endCap;
                l.join = style.join;
                l.points = piece;
                l.group = int(groupStarts.size()) - 1;
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
        area.style = fills[s - 1];
        for (int g = int(groupStarts.size()) - 1; g >= 0; --g)
            if (groupStarts[g] <= s - 1) {
                area.group = g;
                break;
            }
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

Shape records(QByteArray const& b, int p, int version, QVector<FillStyle> fills, QVector<LineStyle> lineStyles)
{
    QVector<Rec> const recs = parseRecords(b, p, version, fills, lineStyles);
    return build(recs, fills, lineStyles);
}

int skipRect(QByteArray const& b, int p)
{
    Bits r(b, p);
    int const n = int(r.ub(5));
    for (int i = 0; i < 4; ++i)
        r.sb(n);
    r.align();
    return r.byte();
}

Shape shape(QByteArray const& b, int version)
{
    int p = skipRect(b, 2);
    if (version == 4)
        p = skipRect(b, p) + 1;     // the edge bounds, and its flags
    QVector<FillStyle> fills;
    QVector<LineStyle> lineStyles;
    styles(b, p, version, fills, lineStyles);
    return records(b, p, version, fills, lineStyles);
}

}  // namespace swf
