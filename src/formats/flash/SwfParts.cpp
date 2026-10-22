// Eclipse - reading the parts of a Flash movie (.swf) from its bytes.
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
    for (Rec const& r : records) {
        if (!r.edge) {
            if (r.move)
                at = r.to;
            if (r.fillBase >= 0) {
                base = r.fillBase;
                lineBase = r.lineBase;
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

void morphFill(QByteArray const& b, int& p, FillStyle& a, FillStyle& z)
{
    int const type = u8(b, p++);
    if (type == 0x00) {
        a.colour = colourAt(b, p, true);
        z.colour = colourAt(b, p, true);
        return;
    }
    if (type == 0x10 || type == 0x12 || type == 0x13) {
        Bits m0(b, p);
        a.matrix = matrix(m0);
        Bits m1(b, m0.byte());
        z.matrix = matrix(m1);
        p = m1.byte();
        int const count = u8(b, p++) & 0x0f;
        a.gradient.kind = z.gradient.kind = type == 0x10 ? Swatch::Linear : Swatch::Radial;
        for (int i = 0; i < count; ++i) {
            double const at0 = u8(b, p++) / 255.0;
            QColor const c0 = colourAt(b, p, true);
            double const at1 = u8(b, p++) / 255.0;
            QColor const c1 = colourAt(b, p, true);
            a.gradient.stops.push_back({at0, c0});
            z.gradient.stops.push_back({at1, c1});
        }
        if (type == 0x13)
            p += 4;     // focal points
        for (FillStyle* s : {&a, &z}) {
            s->colour = s->gradient.stops.isEmpty() ? QColor(Qt::gray) : s->gradient.stops.first().color;
            s->gradient.color = s->colour;
            s->gradient.name = tr("Flash gradient");
            if (s->gradient.stops.size() < 2)
                s->gradient.kind = Swatch::Solid;
        }
        return;
    }
    int const id = u16(b, p);
    p += 2;
    Bits m0(b, p);
    a.matrix = matrix(m0);
    Bits m1(b, m0.byte());
    z.matrix = matrix(m1);
    p = m1.byte();
    a.colour = z.colour = QColor(128, 128, 128);
    a.bitmap = z.bitmap = id == 0xffff ? -1 : id;
}

MorphShape morph(QByteArray const& b, int version)
{
    MorphShape out;
    int p = skipRect(b, 2);
    p = skipRect(b, p);
    if (version == 2) {
        p = skipRect(b, p);
        p = skipRect(b, p) + 1;
    }
    int const endAt = p + 4 + int(u32(b, p));
    p += 4;
    int count = u8(b, p++);
    if (count == 0xff) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i) {
        FillStyle a, z;
        morphFill(b, p, a, z);
        out.fills0.push_back(a);
        out.fills1.push_back(z);
    }
    count = u8(b, p++);
    if (count == 0xff) {
        count = u16(b, p);
        p += 2;
    }
    for (int i = 0; i < count && p < b.size(); ++i) {
        LineStyle a, z;
        a.width = u16(b, p);
        z.width = u16(b, p + 2);
        p += 4;
        if (version == 2) {
            quint16 const flags = u16(b, p);
            p += 2;
            a.startCap = z.startCap = (flags >> 6) & 3;
            a.join = z.join = (flags >> 4) & 3;
            a.endCap = z.endCap = (flags >> 8) & 3;
            if (a.join == 2)
                p += 2;
            a.keepWidth = z.keepWidth = (flags & 0x0004) && (flags & 0x0002);
            if (flags & 0x0008) {
                FillStyle fa, fz;
                morphFill(b, p, fa, fz);
                a.colour = fa.colour;
                z.colour = fz.colour;
            } else {
                a.colour = colourAt(b, p, true);
                z.colour = colourAt(b, p, true);
            }
        } else {
            a.colour = colourAt(b, p, true);
            z.colour = colourAt(b, p, true);
        }
        out.lines0.push_back(a);
        out.lines1.push_back(z);
    }
    QVector<FillStyle> fills = out.fills0;
    QVector<LineStyle> lines = out.lines0;
    out.start = parseRecords(b, p, 3, fills, lines);
    QVector<FillStyle> none;
    QVector<LineStyle> noLines;
    out.end = parseRecords(b, endAt, 3, none, noLines);
    return out;
}

QColor mixed(QColor a, QColor b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t), float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QTransform mixed(QTransform const& a, QTransform const& b, double t)
{
    auto at = [&](double x, double y) { return x + (y - x) * t; };
    return QTransform(at(a.m11(), b.m11()), at(a.m12(), b.m12()), at(a.m21(), b.m21()), at(a.m22(), b.m22()), at(a.dx(), b.dx()), at(a.dy(), b.dy()));
}

Shape morphAt(MorphShape const& m, double t)
{
    QVector<FillStyle> fills;
    for (int i = 0; i < m.fills0.size(); ++i) {
        FillStyle f = m.fills0[i];
        FillStyle const& z = m.fills1.value(i, f);
        f.colour = mixed(f.colour, z.colour, t);
        f.matrix = mixed(f.matrix, z.matrix, t);
        for (int k = 0; k < f.gradient.stops.size() && k < z.gradient.stops.size(); ++k) {
            f.gradient.stops[k].at = f.gradient.stops[k].at + (z.gradient.stops[k].at - f.gradient.stops[k].at) * t;
            f.gradient.stops[k].color = mixed(f.gradient.stops[k].color, z.gradient.stops[k].color, t);
        }
        if (!f.gradient.stops.isEmpty())
            f.gradient.color = f.gradient.stops.first().color;
        fills.push_back(f);
    }
    QVector<LineStyle> lines;
    for (int i = 0; i < m.lines0.size(); ++i) {
        LineStyle l = m.lines0[i];
        LineStyle const& z = m.lines1.value(i, l);
        l.width = l.width + (z.width - l.width) * t;
        l.colour = mixed(l.colour, z.colour, t);
        lines.push_back(l);
    }
    QVector<Rec> recs;
    int j = 0;
    auto asCurve = [](Rec r) {
        if (!r.curve) {
            r.curve = true;
            r.control = r.anchor / 2.0;
            r.anchor = r.anchor / 2.0;
        }
        return r;
    };
    for (Rec r : m.start) {
        if (!r.edge) {
            // A move: to where the end has it, if the end moves here too.
            if (j < m.end.size() && !m.end[j].edge) {
                if (r.move && m.end[j].move)
                    r.to = r.to + (m.end[j].to - r.to) * t;
                ++j;
            }
            recs.push_back(r);
            continue;
        }
        while (j < m.end.size() && !m.end[j].edge)
            ++j;
        if (j >= m.end.size()) {
            recs.push_back(r);
            continue;
        }
        Rec e = m.end[j++];
        if (r.curve != e.curve) {
            r = asCurve(r);
            e = asCurve(e);
        }
        r.control = r.control + (e.control - r.control) * t;
        r.anchor = r.anchor + (e.anchor - r.anchor) * t;
        recs.push_back(r);
    }
    return build(recs, fills, lines);
}

Font font(QByteArray const& b, int version)
{
    Font out;
    FillStyle ink;
    ink.colour = Qt::black;
    int table = 2, count = 0;
    bool wide = false;
    if (version == 1) {
        count = u16(b, table) / 2;
    } else {
        int p = 2;
        int const flags = u8(b, p++);
        ++p;    // language
        p += 1 + u8(b, p);    // name
        count = u16(b, p);
        p += 2;
        table = p;
        wide = flags & 0x08;
        out.em = version == 3 ? 20480.0 : 1024.0;
    }
    for (int i = 0; i < count; ++i) {
        int const offset = wide ? int(u32(b, table + 4 * i)) : u16(b, table + 2 * i);
        if (table + offset >= b.size())
            break;
        out.glyphs.push_back(records(b, table + offset, 1, {ink}, {}));
    }
    return out;
}

Shape textShape(QByteArray const& b, bool alpha, QHash<int, Font> const& fonts)
{
    Shape out;
    int p = 2;
    Bits bounds(b, p);
    int const nb = int(bounds.ub(5));
    for (int i = 0; i < 4; ++i)
        bounds.sb(nb);
    bounds.align();
    Bits m(b, bounds.byte());
    QTransform const place = matrix(m);
    p = m.byte();
    int const glyphBits = u8(b, p++), advanceBits = u8(b, p++);
    int fontId = -1;
    double height = 240.0, x = 0.0, y = 0.0;
    QColor colour = Qt::black;
    while (p < b.size()) {
        int const flags = u8(b, p++);
        if (flags == 0)
            break;
        if (flags & 0x08) {
            fontId = u16(b, p);
            p += 2;
        }
        if (flags & 0x04)
            colour = colourAt(b, p, alpha);
        if (flags & 0x01) {
            x = qint16(u16(b, p));
            p += 2;
        }
        if (flags & 0x02) {
            y = qint16(u16(b, p));
            p += 2;
        }
        if (flags & 0x08) {
            height = u16(b, p);
            p += 2;
        }
        int const count = u8(b, p++);
        Bits glyphs(b, p);
        Shape::Area run;
        run.style.colour = colour;
        auto const found = fonts.constFind(fontId);
        Font const* const f = found == fonts.cend() ? nullptr : &*found;
        for (int i = 0; i < count; ++i) {
            int const index = int(glyphs.ub(glyphBits));
            int const advance = glyphs.sb(advanceBits);
            if (f && index < f->glyphs.size()) {
                QTransform const letter = QTransform::fromScale(height / f->em, height / f->em) * QTransform::fromTranslate(x, y) * place;
                for (Shape::Area const& a : f->glyphs[index].areas)
                    for (QPolygonF const& ring : a.rings)
                        run.rings.push_back(letter.map(ring));
            }
            x += advance;
        }
        glyphs.align();
        p = glyphs.byte();
        if (!run.rings.isEmpty())
            out.areas.push_back(run);
    }
    return out;
}

QImage jpeg(QByteArray data)
{
    if (data.startsWith("\xff\xd9\xff\xd8"))
        data = data.mid(4);
    return QImage::fromData(data);
}

QImage lossless(QByteArray const& b, bool alpha)
{
    int const format = u8(b, 2), width = u16(b, 3), height = u16(b, 5);
    if (width <= 0 || height <= 0)
        return QImage();
    int const tableSize = format == 3 ? u8(b, 7) + 1 : 0;
    QByteArray const pixels = unzlib(b.mid(format == 3 ? 8 : 7));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    auto premultiplied = [](int r, int g, int bl, int a) { return qRgba(std::min(r, a), std::min(g, a), std::min(bl, a), a); };
    if (format == 3) {
        int const entry = alpha ? 4 : 3, stride = (width + 3) & ~3;
        if (pixels.size() < tableSize * entry + stride * height)
            return QImage();
        QVector<QRgb> table(tableSize);
        for (int i = 0; i < tableSize; ++i) {
            int const at = i * entry;
            table[i] = premultiplied(u8(pixels, at), u8(pixels, at + 1), u8(pixels, at + 2), alpha ? u8(pixels, at + 3) : 255);
        }
        for (int y = 0; y < height; ++y) {
            QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                int const index = u8(pixels, tableSize * entry + y * stride + x);
                out[x] = index < tableSize ? table[index] : 0;
            }
        }
    } else if (format == 4) {
        int const stride = (width * 2 + 3) & ~3;
        if (pixels.size() < stride * height)
            return QImage();
        for (int y = 0; y < height; ++y) {
            QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                int const v = (u8(pixels, y * stride + 2 * x) << 8) | u8(pixels, y * stride + 2 * x + 1);
                out[x] = qRgb(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31);
            }
        }
    } else if (format == 5) {
        if (pixels.size() < width * height * 4)
            return QImage();
        for (int y = 0; y < height; ++y) {
            QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                int const at = 4 * (y * width + x);
                out[x] = premultiplied(u8(pixels, at + 1), u8(pixels, at + 2), u8(pixels, at + 3), alpha ? u8(pixels, at) : 255);
            }
        }
    } else {
        return QImage();
    }
    return image;
}

bool soundFile(int format, QByteArray const& data, SwfImport::Sound& out)
{
    int const compression = format >> 4;
    if (compression == 2) {
        out.bytes = data;
        out.suffix = QStringLiteral("mp3");
        return true;
    }
    if (compression == 0 || compression == 3) {
        static int const rates[4] = {5512, 11025, 22050, 44100};
        out.bytes = SwfImport::wav(data, rates[(format >> 2) & 3], (format & 2) ? 16 : 8, (format & 1) ? 2 : 1);
        out.suffix = QStringLiteral("wav");
        return true;
    }
    return false;
}

void control(int code, QByteArray const& body, DisplayList& list, int frame)
{
    if (code == 26 || code == 70) {
        int q = 0;
        int const flags = u8(body, q++);
        int flags2 = 0;
        if (code == 70)
            flags2 = u8(body, q++);
        int const depth = u16(body, q);
        q += 2;
        if (code == 70 && ((flags2 & 0x08) || ((flags2 & 0x10) && (flags & 0x02))))
            while (q < body.size() && body[q++] != 0) {}       // a class name
        Placed& at = list[depth];
        if (flags & 0x02) {
            int const character = u16(body, q);
            q += 2;
            if (!(flags & 0x01)) {
                at = Placed();
                at.since = frame;
            } else if (character != at.character) {
                at.since = frame;
            }
            at.character = character;
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
        if (flags & 0x10) {
            at.ratio = u16(body, q);
            q += 2;
        }
        if (flags & 0x20)
            while (q < body.size() && body[q++] != 0) {}       // a name
        if (flags & 0x40)
            at.clipDepth = u16(body, q);
    } else if (code == 4) {
        Placed placed;
        placed.character = u16(body, 0);
        Bits bits(body, 4);
        placed.matrix = matrix(bits);
        placed.since = frame;
        list[u16(body, 2)] = placed;
    } else if (code == 28) {
        list.remove(u16(body, 0));
    } else if (code == 5) {
        list.remove(u16(body, 2));
    }
}

}  // namespace swf
