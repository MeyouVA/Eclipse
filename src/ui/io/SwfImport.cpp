// Eclipse - the Flash movie reader.
#include "ui/io/SwfImport.h"

#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QMap>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"
#include "ui/io/Zip.h"

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

// A string ended by a zero byte, from `p` on (`p` left past it).
QString text(QByteArray const& b, int& p)
{
    int const start = p;
    while (p < b.size() && b[p] != 0)
        ++p;
    QString const s = QString::fromUtf8(b.mid(start, p - start));
    ++p;
    return s;
}

// A number of one to five bytes, seven bits in each, the lowest first.
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

// Zlib data unpacked (empty when broken).
QByteArray unzlib(QByteArray const& data)
{
    if (data.size() < 6)
        return QByteArray();
    bool ok = false;
    QByteArray const out = Zip::inflate(data.mid(2), &ok);
    return ok ? out : QByteArray();
}

// How a fill style paints: a colour (a solid one, a gradient's first, grey for a bitmap); a gradient (its square, -16384
// to 16384 twips each way, placed in the shape by a matrix); or a bitmap, placed by a matrix from its pixels to the shape.
struct FillStyle {
    QColor colour;
    Swatch gradient;            // (Swatch::Solid: none)
    int bitmap = -1;
    QTransform matrix;
};

// A shape as read: its fills (rings of points, in twips) and lines.
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

QColor colourAt(QByteArray const& b, int& p, bool alpha)
{
    QColor const c(u8(b, p), u8(b, p + 1), u8(b, p + 2), alpha ? u8(b, p + 3) : 255);
    p += alpha ? 4 : 3;
    return c;
}

// A fill style read (and passed over).
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

struct LineStyle {
    QColor colour;
    double width = 20.0;
    bool keepWidth = false;
    int startCap = 0, endCap = 0, join = 0;
};

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

// The shape records from `p`: edges with the fill on their left (fill 0) and right (fill 1); joined into rings, fill by fill.
Shape records(QByteArray const& b, int p, int version, QVector<FillStyle> fills, QVector<LineStyle> lineStyles)
{
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
    QVector<FillStyle> fills;
    QVector<LineStyle> lineStyles;
    styles(b, p, version, fills, lineStyles);
    return records(b, p, version, fills, lineStyles);
}

// A font of the movie: the outline of each of its letters, in units of which `em` make the letter height.
struct Font {
    QVector<Shape> glyphs;
    double em = 1024.0;
};

// DefineFont (`version` 1: only the outlines) or DefineFont2 / DefineFont3 (with a name, a code table and more).
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

// DefineText / DefineText2: runs of letters of a font, each where its advances put it; as a shape (in twips).
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

// A JPEG as the movie keeps it: some movies put an end and a start marker before the picture.
QImage jpeg(QByteArray data)
{
    if (data.startsWith("\xff\xd9\xff\xd8"))
        data = data.mid(4);
    return QImage::fromData(data);
}

// DefineBitsLossless (`alpha` false) or DefineBitsLossless2: a colour table and indices, or 15- or 32-bit pixels, zlib-packed.
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

// A sound of the movie in the form of a file: an MP3 as it is, uncompressed samples as a WAV; or nothing when it is in a
// form Eclipse does not read.
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

}  // namespace

namespace SwfImport {

QByteArray wav(QByteArray const& samples, int rate, int bits, int channels)
{
    QByteArray out;
    auto put16 = [&](int v) {
        out.append(char(v & 0xff));
        out.append(char((v >> 8) & 0xff));
    };
    auto put32 = [&](quint32 v) {
        put16(int(v & 0xffff));
        put16(int(v >> 16));
    };
    int const block = channels * bits / 8;
    out += "RIFF";
    put32(quint32(36 + samples.size()));
    out += "WAVEfmt ";
    put32(16);
    put16(1);
    put16(channels);
    put32(quint32(rate));
    put32(quint32(rate * block));
    put16(block);
    put16(bits);
    out += "data";
    put32(quint32(samples.size()));
    out += samples;
    if (samples.size() & 1)
        out.append('\0');
    return out;
}

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
    out = Movie{out.size, out.fps, {}, {}, {}, {}, 0, 0};
    QHash<int, Shape> shapes;
    QHash<int, Font> fonts;
    QHash<QByteArray, int> gradientKeys;    // a gradient of `out.gradients`, by what it is
    auto gradientOf = [&](Swatch const& s) {
        QByteArray key;
        key += char(s.kind);
        for (GradientStop const& stop : s.stops)
            key += QByteArray::number(stop.at, 'f', 4) + ':' + QByteArray::number(stop.color.rgba()) + ';';
        auto found = gradientKeys.constFind(key);
        if (found != gradientKeys.cend())
            return *found;
        out.gradients.push_back(s);
        gradientKeys.insert(key, int(out.gradients.size()) - 1);
        return int(out.gradients.size()) - 1;
    };
    QHash<int, QImage> bitmaps;
    QByteArray jpegTables;
    QHash<int, Sound> sounds;           // event sounds, by their ids
    QHash<QString, int> classes;        // ids by the names ActionScript knows them by
    int streamFormat = -1;              // the sound streamed along with the frames: its form, and what has come of it
    Sound stream;
    stream.frame = -1;
    QByteArray streamed;
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
    auto label = [&](int frame, QString const& name) {
        for (Label const& l : std::as_const(out.labels))
            if (l.frame == frame && l.name == name)
                return;
        out.labels.push_back({frame, name});
    };
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
        } else if (code == 10 || code == 48 || code == 75) {
            fonts.insert(u16(body, 0), font(body, code == 10 ? 1 : code == 48 ? 2 : 3));
        } else if (code == 11 || code == 33) {
            shapes.insert(u16(body, 0), textShape(body, code == 33, fonts));
        } else if (code == 8) {
            jpegTables = body;
        } else if (code == 6) {
            // A JPEG whose tables are kept apart (JPEGTables): the two put together, less the end of the one and the start of
            // the other.
            QByteArray picture = body.mid(2);
            if (picture.startsWith("\xff\xd9\xff\xd8"))
                picture = picture.mid(4);
            QByteArray tables = jpegTables;
            if (tables.startsWith("\xff\xd9\xff\xd8"))
                tables = tables.mid(4);
            QImage image = tables.size() > 4 ? QImage::fromData(tables.left(tables.size() - 2) + picture.mid(2)) : QImage();
            if (image.isNull())
                image = jpeg(picture);
            bitmaps.insert(u16(body, 0), image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        } else if (code == 21) {
            bitmaps.insert(u16(body, 0), jpeg(body.mid(2)).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        } else if (code == 35 || code == 90) {
            // A JPEG (or PNG, GIF) and, for a JPEG, its opacity apart: a byte a pixel, zlib-packed.
            int const length = int(u32(body, 2));
            int const start = code == 90 ? 8 : 6;
            QByteArray const picture = body.mid(start, length);
            QImage image = jpeg(picture).convertToFormat(QImage::Format_ARGB32);
            QByteArray const alpha = unzlib(body.mid(start + length));
            if (!image.isNull() && picture.startsWith("\xff\xd8") && alpha.size() >= qsizetype(image.width()) * image.height())
                for (int y = 0; y < image.height(); ++y) {
                    QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
                    for (int x = 0; x < image.width(); ++x)
                        line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), quint8(alpha[y * image.width() + x]));
                }
            bitmaps.insert(u16(body, 0), image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        } else if (code == 20 || code == 36) {
            bitmaps.insert(u16(body, 0), lossless(body, code == 36));
        } else if (code == 43) {
            int q = 0;
            label(int(out.frames.size()), text(body, q));
        } else if (code == 86) {
            // Scenes (passed over), then the labels of frames.
            int q = 0;
            quint32 const scenes = encodedU32(body, q);
            for (quint32 i = 0; i < scenes && q < body.size(); ++i) {
                encodedU32(body, q);
                text(body, q);
            }
            quint32 const labels = encodedU32(body, q);
            for (quint32 i = 0; i < labels && q < body.size(); ++i) {
                int const frame = int(encodedU32(body, q));
                label(frame, text(body, q));
            }
        } else if (code == 76) {
            int const count = u16(body, 0);
            int q = 2;
            for (int i = 0; i < count && q < body.size(); ++i) {
                int const id = u16(body, q);
                q += 2;
                classes.insert(text(body, q), id);
            }
        } else if (code == 14) {
            // An event sound: its form, how many samples, then them (an MP3 after a count of samples to skip).
            int const format = u8(body, 2);
            Sound sound;
            if (soundFile(format, body.mid((format >> 4) == 2 ? 9 : 7), sound))
                sounds.insert(u16(body, 0), sound);
            else
                ++out.skippedSounds;
        } else if (code == 15 || code == 89) {
            int q = 0;
            int id = -1;
            if (code == 15) {
                id = u16(body, 0);
                q = 2;
            } else {
                id = classes.value(text(body, q), -1);
            }
            bool const stop = u8(body, q) & 0x20;
            if (!stop && sounds.contains(id)) {
                Sound sound = sounds.value(id);
                sound.frame = int(out.frames.size());
                out.sounds.push_back(sound);
            }
        } else if (code == 18 || code == 45) {
            streamFormat = u8(body, 1);
        } else if (code == 19) {
            if (streamFormat >= 0) {
                if (stream.frame < 0)
                    stream.frame = int(out.frames.size());
                // (Each block of an MP3 stream starts with a count of samples and a count to skip.)
                streamed += (streamFormat >> 4) == 2 ? body.mid(4) : body;
            }
        } else if (code == 60) {
            ++out.skippedVideo;
        } else if (code == 26 || code == 70) {
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
                bool const tinted = it->mult[0] != 256 || it->mult[1] != 256 || it->mult[2] != 256 || it->mult[3] != 256 || it->add[0] != 0 || it->add[1] != 0
                                    || it->add[2] != 0 || it->add[3] != 0;
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
                    if (area.style.bitmap >= 0) {
                        // Filled with a bitmap: the picture, where the fill's matrix puts it (under the paint of the drawing).
                        QImage const image = bitmaps.value(area.style.bitmap);
                        if (!image.isNull()) {
                            Picture picture;
                            picture.image = image;
                            if (tinted) {
                                picture.image = image.convertToFormat(QImage::Format_ARGB32);
                                for (int y = 0; y < picture.image.height(); ++y) {
                                    QRgb* const line = reinterpret_cast<QRgb*>(picture.image.scanLine(y));
                                    for (int x = 0; x < picture.image.width(); ++x) {
                                        QColor const c = colour(QColor::fromRgba(line[x]));
                                        line[x] = c.rgba();
                                    }
                                }
                                picture.image = picture.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                            }
                            picture.size = image.size();
                            picture.placement = area.style.matrix * place;
                            drawing.pictures.push_back(picture);
                            continue;
                        }
                    }
                    Fill fill;
                    fill.color = colour(area.style.colour);
                    fill.order = order;
                    for (QPolygonF const& ring : area.rings)
                        fill.outlines.push_back(place.map(ring));
                    if (area.style.gradient.isGradient()) {
                        // A gradient: as the colour effect leaves it, from one side of its square to the other (a round one
                        // from its middle to its rim).
                        Swatch s = area.style.gradient;
                        for (GradientStop& stop : s.stops)
                            stop.color = colour(stop.color);
                        s.color = s.stops.first().color;
                        QTransform const at = area.style.matrix * place;
                        fill.swatch = gradientOf(s);
                        fill.gradPlaced = true;
                        fill.gradFrom = at.map(s.kind == Swatch::Linear ? QPointF(-16384.0, 0.0) : QPointF(0.0, 0.0));
                        fill.gradTo = at.map(QPointF(16384.0, 0.0));
                    }
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
                    stroke.capStart = line.startCap == 0 ? Stroke::RoundCap : Stroke::FlatCap;
                    stroke.capEnd = line.endCap == 0 ? Stroke::RoundCap : Stroke::FlatCap;
                    stroke.join = line.join == 1 ? Stroke::BevelJoin : line.join == 2 ? Stroke::MitreJoin : Stroke::RoundJoin;
                    QPolygonF points = place.map(line.points);
                    // A square cap: flat, half the thickness beyond the end.
                    auto reach = [&](int end, int towards) {
                        QPointF const d = points[end] - points[towards];
                        double const length = std::hypot(d.x(), d.y());
                        if (length > 1e-9)
                            points[end] += d * (stroke.width / 2.0 / length);
                    };
                    if (points.size() >= 2 && points.first() != points.last()) {
                        if (line.startCap == 2)
                            reach(0, 1);
                        if (line.endCap == 2)
                            reach(int(points.size()) - 1, int(points.size()) - 2);
                    }
                    for (QPointF const& point : std::as_const(points))
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
    if (!streamed.isEmpty()) {
        if (soundFile(streamFormat, streamed, stream))
            out.sounds.push_back(stream);
        else
            ++out.skippedSounds;
    }
    bool any = false;
    for (Drawing const& d : out.frames)
        any = any || !d.fills.isEmpty() || !d.strokes.isEmpty() || !d.pictures.isEmpty();
    if (!any)
        return fail(tr("The movie has nothing on its main timeline that Eclipse can read (only code, movie clips or video)."));
    return true;
}

}
