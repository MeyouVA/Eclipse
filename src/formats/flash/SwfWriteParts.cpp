// Eclipse - the Flash movie writer's parts.
#include "formats/flash/SwfWriteParts.h"

#include <QPolygonF>

#include <algorithm>
#include <climits>
#include <cmath>

namespace swfout {

void Bits::put(quint32 value, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        m_byte = quint8((m_byte << 1) | ((value >> i) & 1u));
        if (++m_used == 8)
            flush();
    }
}

QByteArray Bits::bytes()
{
    if (m_used > 0) {
        m_byte = quint8(m_byte << (8 - m_used));
        flush();
    }
    return m_out;
}

void Bits::align()
{
    if (m_used > 0) {
        m_byte = quint8(m_byte << (8 - m_used));
        flush();
    }
}

void Bits::raw(QByteArray const& bytes)
{
    align();
    m_out += bytes;
}

void Bits::flush()
{
    m_out.append(char(m_byte));
    m_byte = 0;
    m_used = 0;
}

int signedBits(qint32 v)
{
    int n = 1;
    while (n < 32 && (v < -(qint32(1) << (n - 1)) || v > (qint32(1) << (n - 1)) - 1))
        ++n;
    return n;
}

void u8(QByteArray& out, int v)
{
    out.append(char(v & 0xff));
}

void u16(QByteArray& out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char(v >> 8));
}

void u32(QByteArray& out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xff));
}

QByteArray rect(qint32 xmin, qint32 xmax, qint32 ymin, qint32 ymax)
{
    int const n = std::max({signedBits(xmin), signedBits(xmax), signedBits(ymin), signedBits(ymax)});
    Bits bits;
    bits.put(quint32(n), 5);
    for (qint32 v : {xmin, xmax, ymin, ymax})
        bits.putSigned(v, n);
    return bits.bytes();
}

QByteArray tag(int code, QByteArray const& body)
{
    QByteArray out;
    // (Shapes and pictures always with the long length, as Flash itself writes them.)
    bool const definition = code == 2 || code == 22 || code == 32 || code == 83 || code == 20 || code == 36 || code == 21 || code == 35;
    if (body.size() < 63 && !definition) {
        u16(out, quint16((code << 6) | int(body.size())));
    } else {
        u16(out, quint16((code << 6) | 0x3f));
        u32(out, quint32(body.size()));
    }
    return out + body;
}

QByteArray matrix(QTransform const& m)
{
    Bits bits;
    auto fixed = [](double v) { return qint32(std::clamp(std::lround(v * 65536.0), long(INT_MIN / 2), long(INT_MAX / 2))); };
    bool const scales = m.m11() != 1.0 || m.m22() != 1.0;
    bits.put(scales, 1);
    if (scales) {
        qint32 const sx = fixed(m.m11()), sy = fixed(m.m22());
        int const n = std::max(signedBits(sx), signedBits(sy));
        bits.put(quint32(n), 5);
        bits.putSigned(sx, n);
        bits.putSigned(sy, n);
    }
    bool const turns = m.m12() != 0.0 || m.m21() != 0.0;
    bits.put(turns, 1);
    if (turns) {
        qint32 const r0 = fixed(m.m12()), r1 = fixed(m.m21());
        int const n = std::max(signedBits(r0), signedBits(r1));
        bits.put(quint32(n), 5);
        bits.putSigned(r0, n);
        bits.putSigned(r1, n);
    }
    qint32 const tx = qint32(std::lround(m.dx())), ty = qint32(std::lround(m.dy()));
    int const n = tx == 0 && ty == 0 ? 0 : std::max(signedBits(tx), signedBits(ty));
    bits.put(quint32(n), 5);
    if (n > 0) {
        bits.putSigned(tx, n);
        bits.putSigned(ty, n);
    }
    return bits.bytes();
}

bool ColourEffect::plain() const
{
    for (int k = 0; k < 4; ++k)
        if (mult[k] != 256 || add[k] != 0)
            return false;
    return true;
}

bool ColourEffect::operator==(ColourEffect const& o) const
{
    for (int k = 0; k < 4; ++k)
        if (mult[k] != o.mult[k] || add[k] != o.add[k])
            return false;
    return true;
}

ColourEffect ColourEffect::after(ColourEffect const& inner) const
{
    ColourEffect c;
    for (int k = 0; k < 4; ++k) {
        c.mult[k] = inner.mult[k] * mult[k] / 256;
        c.add[k] = inner.add[k] * mult[k] / 256 + add[k];
    }
    return c;
}

QByteArray colourEffect(ColourEffect const& c)
{
    bool adds = false, mults = false;
    int n = 1;
    for (int k = 0; k < 4; ++k) {
        adds = adds || c.add[k] != 0;
        mults = mults || c.mult[k] != 256;
    }
    for (int k = 0; k < 4; ++k) {
        if (mults)
            n = std::max(n, signedBits(c.mult[k]));
        if (adds)
            n = std::max(n, signedBits(c.add[k]));
    }
    n = std::min(n, 15);
    Bits bits;
    bits.put(adds, 1);
    bits.put(mults, 1);
    bits.put(quint32(n), 4);
    if (mults)
        for (int k = 0; k < 4; ++k)
            bits.putSigned(std::clamp(c.mult[k], -(1 << (n - 1)), (1 << (n - 1)) - 1), n);
    if (adds)
        for (int k = 0; k < 4; ++k)
            bits.putSigned(std::clamp(c.add[k], -(1 << (n - 1)), (1 << (n - 1)) - 1), n);
    return bits.bytes();
}

QByteArray filterList(QVector<Flash::Filter> const& filters)
{
    QByteArray out;
    auto fixed = [&](double v) { u32(out, quint32(qint32(std::lround(v * 65536.0)))); };
    auto rgba = [&](QColor const& c) {
        u8(out, c.red());
        u8(out, c.green());
        u8(out, c.blue());
        u8(out, c.alpha());
    };
    QVector<Flash::Filter> known;
    for (Flash::Filter const& f : filters)
        if (f.kind == QLatin1String("BlurFilter") || f.kind == QLatin1String("GlowFilter") || f.kind == QLatin1String("DropShadowFilter"))
            known.push_back(f);
    u8(out, int(std::min<qsizetype>(known.size(), 255)));
    for (Flash::Filter const& f : std::as_const(known)) {
        if (f.kind == QLatin1String("BlurFilter")) {
            u8(out, 1);
            fixed(f.blurX);
            fixed(f.blurY);
            u8(out, 1 << 3);    // one pass
        } else {
            bool const shadow = f.kind == QLatin1String("DropShadowFilter");
            u8(out, shadow ? 0 : 2);
            rgba(f.colour);
            fixed(f.blurX);
            fixed(f.blurY);
            if (shadow) {
                fixed(f.angle * 3.14159265358979323846 / 180.0);
                fixed(f.distance);
            }
            u16(out, quint16(qint16(std::lround(f.strength * 256.0))));
            u8(out, (f.inner ? 0x80 : 0) | 0x20 | 1);     // drawn with the picture, one pass
        }
    }
    return out;
}

namespace {

void fillStyle(QByteArray& out, Style const& s)
{
    if (s.kind == Style::Solid) {
        u8(out, 0x00);
        u8(out, s.colour.red());
        u8(out, s.colour.green());
        u8(out, s.colour.blue());
        u8(out, s.colour.alpha());
    } else if (s.kind == Style::Bitmap) {
        u8(out, s.repeat ? 0x40 : 0x41);
        u16(out, quint16(s.bitmap));
        out += matrix(s.matrix);
    } else {
        u8(out, s.kind == Style::Linear ? 0x10 : 0x12);
        out += matrix(s.matrix);
        // Flash takes fifteen colours at most: more are read off the gradient at fifteen places.
        QVector<QPair<double, QColor>> stops = s.stops;
        if (stops.size() > 15) {
            QVector<QPair<double, QColor>> fewer;
            for (int i = 0; i < 15; ++i)
                fewer.push_back(stops[int(qint64(i) * (stops.size() - 1) / 14)]);
            stops = fewer;
        }
        if (stops.isEmpty())
            stops = {{0.0, s.colour}, {1.0, s.colour}};
        u8(out, int(stops.size()));     // spread: pad; colours mixed as they are
        for (auto const& stop : std::as_const(stops)) {
            u8(out, int(std::lround(std::clamp(stop.first, 0.0, 1.0) * 255.0)));
            u8(out, stop.second.red());
            u8(out, stop.second.green());
            u8(out, stop.second.blue());
            u8(out, stop.second.alpha());
        }
    }
}

void lineStyle(QByteArray& out, Paint const& p)
{
    u16(out, quint16(std::clamp(p.width, 1, 65535)));
    bool const filled = p.style.kind != Style::Solid;
    u8(out, (p.startCap << 6) | (p.join << 4) | (filled ? 0x08 : 0) | (p.noScale ? 0x06 : 0));
    u8(out, p.endCap);
    if (p.join == 2)
        u16(out, quint16(std::clamp(int(std::lround(p.mitre * 256.0)), 256, 65535)));
    if (filled) {
        fillStyle(out, p.style);
    } else {
        u8(out, p.style.colour.red());
        u8(out, p.style.colour.green());
        u8(out, p.style.colour.blue());
        u8(out, p.style.colour.alpha());
    }
}

// A move to a place, setting the styles: the fill on the right (style 1) or the line.
void moveTo(Bits& bits, QPoint at, bool line)
{
    bits.put(0, 1);     // a style change
    bits.put(0, 1);     // no new styles
    bits.put(1, 1);     // the line style
    bits.put(1, 1);     // fill style 1
    bits.put(1, 1);     // fill style 0
    bits.put(1, 1);     // a move
    int const n = std::max(signedBits(at.x()), signedBits(at.y()));
    bits.put(quint32(n), 5);
    bits.putSigned(at.x(), n);
    bits.putSigned(at.y(), n);
    bits.put(0, 1);             // fill style 0: none
    bits.put(line ? 0 : 1, 1);  // fill style 1
    bits.put(line ? 1 : 0, 1);  // line style
}

void straight(Bits& bits, QPoint& at, QPoint to)
{
    // (An edge no longer than a straight edge can say: long ones in pieces.)
    int const steps = std::max(1, int(std::ceil(std::max(std::abs(to.x() - at.x()), std::abs(to.y() - at.y())) / 60000.0)));
    QPoint const from = at;
    for (int s = 1; s <= steps; ++s) {
        QPoint const next = from + (to - from) * s / steps;
        int const dx = next.x() - at.x(), dy = next.y() - at.y();
        if (dx == 0 && dy == 0)
            continue;
        int const n = std::max({signedBits(dx), signedBits(dy), 2});
        bits.put(1, 1);     // an edge
        bits.put(1, 1);     // straight
        bits.put(quint32(n - 2), 4);
        bits.put(1, 1);     // across and down both
        bits.putSigned(dx, n);
        bits.putSigned(dy, n);
        at = next;
    }
}

void curved(Bits& bits, QPoint& at, QPoint control, QPoint to)
{
    int const cx = control.x() - at.x(), cy = control.y() - at.y(), ax = to.x() - control.x(), ay = to.y() - control.y();
    if (std::max({std::abs(cx), std::abs(cy), std::abs(ax), std::abs(ay)}) > 60000) {
        straight(bits, at, to);
        return;
    }
    int const n = std::max({signedBits(cx), signedBits(cy), signedBits(ax), signedBits(ay), 2});
    bits.put(1, 1);     // an edge
    bits.put(0, 1);     // curved
    bits.put(quint32(n - 2), 4);
    bits.putSigned(cx, n);
    bits.putSigned(cy, n);
    bits.putSigned(ax, n);
    bits.putSigned(ay, n);
    at = to;
}

QPoint rounded(QPointF p)
{
    return QPoint(int(std::lround(p.x())), int(std::lround(p.y())));
}

// A cubic curve as quadratic ones, near enough (in twips).
void cubic(Bits& bits, QPoint& at, QPointF p0, QPointF c1, QPointF c2, QPointF p3, int depth = 0)
{
    QPointF const d = p3 - 3.0 * c2 + 3.0 * c1 - p0;
    double const error = std::hypot(d.x(), d.y()) * 0.048;
    if (error > 2.0 && depth < 6) {
        QPointF const a = (p0 + c1) / 2.0, b = (c1 + c2) / 2.0, c = (c2 + p3) / 2.0;
        QPointF const ab = (a + b) / 2.0, bc = (b + c) / 2.0, m = (ab + bc) / 2.0;
        cubic(bits, at, p0, a, ab, m, depth + 1);
        cubic(bits, at, m, bc, c, p3, depth + 1);
        return;
    }
    curved(bits, at, rounded((3.0 * (c1 + c2) - p0 - p3) / 4.0), rounded(p3));
}

}  // namespace

QByteArray shape(int id, QVector<Paint> const& paints)
{
    QByteArray body;
    u16(body, quint16(id));
    // How far it reaches: its edges alone, and with half the thickness of its lines.
    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    auto take = [&](QPoint q) {
        left = std::min(left, q.x());
        right = std::max(right, q.x());
        top = std::min(top, q.y());
        bottom = std::max(bottom, q.y());
    };
    int reach = 0;
    bool scaling = false, fixedLines = false;
    for (Paint const& p : paints) {
        if (p.line) {
            QRect const box = p.path.boundingRect().toAlignedRect();
            take(box.topLeft());
            take(box.bottomRight());
            reach = std::max(reach, (p.width + 1) / 2);
            (p.noScale ? fixedLines : scaling) = true;
        } else {
            for (auto const& ring : p.rings)
                for (QPoint const& q : ring)
                    take(q);
        }
    }
    if (left > right) {
        left = right = top = bottom = 0;
    }
    QRect const e(QPoint(left, top), QPoint(right, bottom));
    QRect const s = e.adjusted(-reach, -reach, reach, reach);
    body += rect(s.left(), s.right(), s.top(), s.bottom());
    body += rect(e.left(), e.right(), e.top(), e.bottom());
    u8(body, (fixedLines ? 0x02 : 0) | (scaling ? 0x01 : 0));
    // No styles to begin with: each paint brings its own, so that each lies over the ones before.
    u8(body, 0);
    u8(body, 0);
    Bits bits;
    bits.put(0, 4);     // no fill or line style bits yet
    bits.put(0, 4);
    for (Paint const& p : paints) {
        bits.put(0, 1);
        bits.put(1, 1);     // new styles
        bits.put(0, 4);
        QByteArray styles;
        if (p.line) {
            u8(styles, 0);
            u8(styles, 1);
            lineStyle(styles, p);
        } else {
            u8(styles, 1);
            fillStyle(styles, p.style);
            u8(styles, 0);
        }
        u8(styles, 0x11);   // a bit each for the fill and the line style
        bits.raw(styles);
        if (p.line) {
            QPoint at;
            for (int i = 0; i < p.path.elementCount(); ++i) {
                QPainterPath::Element const el = p.path.elementAt(i);
                if (el.isMoveTo()) {
                    at = rounded(QPointF(el.x, el.y));
                    moveTo(bits, at, true);
                } else if (el.isLineTo()) {
                    straight(bits, at, rounded(QPointF(el.x, el.y)));
                } else if (el.isCurveTo() && i + 2 < p.path.elementCount()) {
                    QPainterPath::Element const c2 = p.path.elementAt(i + 1), end = p.path.elementAt(i + 2);
                    cubic(bits, at, QPointF(at), QPointF(el.x, el.y), QPointF(c2.x, c2.y), QPointF(end.x, end.y));
                    i += 2;
                }
            }
        } else {
            // Which rings are holes: those inside an odd number of the others.
            QVector<QPolygonF> polygons;
            for (auto const& ring : p.rings) {
                QPolygonF poly;
                for (QPoint const& q : ring)
                    poly << QPointF(q);
                polygons.push_back(poly);
            }
            for (int r = 0; r < p.rings.size(); ++r) {
                QVector<QPoint> ring = p.rings[r];
                if (ring.size() < 3)
                    continue;
                int inside = 0;
                for (int o = 0; o < polygons.size(); ++o)
                    if (o != r && polygons[o].containsPoint(polygons[r].first(), Qt::OddEvenFill))
                        ++inside;
                // The fill on the right of each edge (y down): an outer ring goes clockwise on screen, a hole the other way.
                double twice = 0.0;
                for (int i = 0; i < ring.size(); ++i)
                    twice += double(ring[i].x()) * ring[(i + 1) % ring.size()].y() - double(ring[(i + 1) % ring.size()].x()) * ring[i].y();
                if ((twice > 0.0) != (inside % 2 == 0))
                    std::reverse(ring.begin(), ring.end());
                QPoint at = ring.first();
                moveTo(bits, at, false);
                for (int i = 1; i <= ring.size(); ++i)
                    straight(bits, at, ring[i % ring.size()]);
            }
        }
    }
    bits.put(0, 6);     // the end
    return body + bits.bytes();
}

}  // namespace swfout
