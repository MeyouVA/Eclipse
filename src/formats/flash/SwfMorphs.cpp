// Eclipse - reading a Flash movie (.swf): morph shapes, fonts and text, pictures, sounds, the display list.
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

QVector<Flash::Filter> filters(QByteArray const& b, int& q)
{
    QVector<Flash::Filter> out;
    auto fixed = [&]() {
        double const v = qint32(u32(b, q)) / 65536.0;
        q += 4;
        return v;
    };
    auto colour = [&]() {
        QColor const c(u8(b, q), u8(b, q + 1), u8(b, q + 2), u8(b, q + 3));
        q += 4;
        return c;
    };
    int const count = u8(b, q++);
    for (int i = 0; i < count && q < b.size(); ++i) {
        int const id = u8(b, q++);
        Flash::Filter f;
        if (id == 0 || id == 2) {
            // A drop shadow or a glow: its colour, how soft it is, (a shadow) where it falls, how strong.
            f.kind = id == 0 ? QStringLiteral("DropShadowFilter") : QStringLiteral("GlowFilter");
            f.colour = colour();
            f.blurX = fixed();
            f.blurY = fixed();
            if (id == 0) {
                f.angle = fixed() * 180.0 / 3.14159265358979323846;
                f.distance = fixed();
            }
            f.strength = qint16(u16(b, q)) / 256.0;
            q += 2;
            f.inner = u8(b, q++) & 0x80;
            out.push_back(f);
        } else if (id == 1) {
            f.kind = QStringLiteral("BlurFilter");
            f.blurX = fixed();
            f.blurY = fixed();
            ++q;
            out.push_back(f);
        } else if (id == 3) {
            q += 27;    // a bevel (not one of Eclipse's)
        } else if (id == 4 || id == 7) {
            q += 1 + 5 * u8(b, q) + 19;     // a gradient glow or bevel
        } else if (id == 5) {
            q += 2 + 8 + 4 * u8(b, q) * u8(b, q + 1) + 5;     // a convolution
        } else if (id == 6) {
            q += 80;    // a colour matrix
        } else {
            q = int(b.size());  // not known: what follows cannot be found
        }
    }
    return out;
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
            at.name = text(body, q);
        if (flags & 0x40) {
            at.clipDepth = u16(body, q);
            q += 2;
        }
        if (flags2 & 0x01)
            at.filters = filters(body, q);
        if (flags2 & 0x02)
            at.blend = u8(body, q++);
        if (flags2 & 0x04)
            ++q;        // kept as a bitmap or not
        if (flags2 & 0x20)
            at.hidden = u8(body, q++) == 0;
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
