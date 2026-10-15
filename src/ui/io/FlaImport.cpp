// Eclipse - the Flash document reader.
#include "ui/io/FlaImport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QPolygonF>
#include <QTransform>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
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
#include "ui/io/FlashVideo.h"
#include "ui/io/SwfImport.h"
#include "ui/io/Zip.h"

namespace {

QString tr(char const* text)
{
    return QCoreApplication::translate("FlaImport", text);
}

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

bool parse(QByteArray const& text, Node& root)
{
    QXmlStreamReader xml(text);
    QVector<Node*> stack;
    bool started = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            Node n;
            n.name = xml.name().toString();
            for (QXmlStreamAttribute const& a : xml.attributes())
                n.attributes.insert(a.name().toString(), a.value().toString());
            if (!started) {
                root = n;
                stack.push_back(&root);
                started = true;
            } else {
                stack.last()->children.push_back(n);
                stack.push_back(&stack.last()->children.last());
            }
        } else if (xml.isEndElement() && !stack.isEmpty()) {
            stack.pop_back();
        } else if (xml.isCharacters() && !stack.isEmpty() && !xml.isWhitespace()) {
            // The words inside an element (as a text run's characters).
            stack.last()->attributes[QStringLiteral("#text")] += xml.text().toString();
        }
    }
    return started && !xml.hasError();
}

QTransform matrixOf(Node const& element)
{
    Node const* m = element.child(QStringLiteral("matrix"));
    Node const* inner = m ? m->child(QStringLiteral("Matrix")) : nullptr;
    if (!inner)
        return QTransform();
    return QTransform(inner->number(QStringLiteral("a"), 1.0), inner->number(QStringLiteral("b"), 0.0), inner->number(QStringLiteral("c"), 0.0),
                      inner->number(QStringLiteral("d"), 1.0), inner->number(QStringLiteral("tx"), 0.0), inner->number(QStringLiteral("ty"), 0.0));
}

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

Tint tintOf(Node const& element)
{
    Tint t;
    Node const* c = element.child(QStringLiteral("color"));
    Node const* inner = c ? c->child(QStringLiteral("Color")) : nullptr;
    if (!inner)
        return t;
    t.mult[0] = inner->number(QStringLiteral("redMultiplier"), 1.0);
    t.mult[1] = inner->number(QStringLiteral("greenMultiplier"), 1.0);
    t.mult[2] = inner->number(QStringLiteral("blueMultiplier"), 1.0);
    t.mult[3] = inner->number(QStringLiteral("alphaMultiplier"), 1.0);
    t.add[0] = inner->number(QStringLiteral("redOffset"), 0.0);
    t.add[1] = inner->number(QStringLiteral("greenOffset"), 0.0);
    t.add[2] = inner->number(QStringLiteral("blueOffset"), 0.0);
    t.add[3] = inner->number(QStringLiteral("alphaOffset"), 0.0);
    // Tint: towards a colour, so much of the way.
    if (inner->attributes.contains(QStringLiteral("tintColor"))) {
        double const amount = inner->number(QStringLiteral("tintMultiplier"), 0.0);
        QColor const to(inner->attr(QStringLiteral("tintColor")));
        for (int k = 0; k < 3; ++k)
            t.mult[k] = 1.0 - amount;
        t.add[0] = to.red() * amount;
        t.add[1] = to.green() * amount;
        t.add[2] = to.blue() * amount;
    }
    // Brightness: towards white (or black), so much of the way.
    if (inner->attributes.contains(QStringLiteral("brightness"))) {
        double const b = inner->number(QStringLiteral("brightness"), 0.0);
        for (int k = 0; k < 3; ++k) {
            t.mult[k] = 1.0 - std::abs(b);
            t.add[k] = b > 0 ? 255.0 * b : 0.0;
        }
    }
    return t;
}

QColor colourOf(Node const& fill)
{
    // A solid colour; a gradient's first; grey for a bitmap.
    if (Node const* solid = fill.child(QStringLiteral("SolidColor"))) {
        QColor c(solid->attr(QStringLiteral("color"), QStringLiteral("#000000")));
        c.setAlphaF(float(solid->number(QStringLiteral("alpha"), 1.0)));
        return c;
    }
    for (Node const& g : fill.children)
        if (g.name.endsWith(QLatin1String("Gradient")))
            for (Node const& entry : g.children)
                if (entry.name == QLatin1String("GradientEntry")) {
                    QColor c(entry.attr(QStringLiteral("color"), QStringLiteral("#000000")));
                    c.setAlphaF(float(entry.number(QStringLiteral("alpha"), 1.0)));
                    return c;
                }
    return QColor(128, 128, 128);
}

// A number of an edge list: a decimal, or #hex.hex (24.8 fixed point, the integer part signed).
double edgeNumber(QString const& s, int& i)
{
    if (i < s.size() && s[i] == QLatin1Char('#')) {
        ++i;
        qint64 whole = 0;
        int digits = 0;
        while (i < s.size() && std::isxdigit(s[i].unicode()) && s[i] != QLatin1Char('.')) {
            whole = whole * 16 + QString(s[i]).toInt(nullptr, 16);
            ++i;
            ++digits;
        }
        double fraction = 0.0;
        if (i < s.size() && s[i] == QLatin1Char('.')) {
            ++i;
            int f = 0, n = 0;
            while (i < s.size() && std::isxdigit(s[i].unicode())) {
                f = f * 16 + QString(s[i]).toInt(nullptr, 16);
                ++i;
                ++n;
            }
            fraction = n > 0 ? f / std::pow(16.0, n) : 0.0;
        }
        if (digits > 0 && whole >= 0x800000)
            whole -= 0x1000000;
        return double(whole) + fraction;
    }
    int const start = i;
    while (i < s.size() && (s[i].isDigit() || s[i] == QLatin1Char('-') || s[i] == QLatin1Char('.') || s[i] == QLatin1Char('e') || s[i] == QLatin1Char('E')
                            || (s[i] == QLatin1Char('+') && i > start)))
        ++i;
    return s.mid(start, i - start).toDouble();
}

void skipSpaces(QString const& s, int& i)
{
    while (i < s.size() && s[i].isSpace())
        ++i;
}

// How an area is painted: a colour; or one of the document's gradients (`gradient`, its colour then the first), lying
// from `from` to `to` (for a round one, from its middle to its rim) in the area's own coordinates.
struct Paint {
    QColor colour;
    int gradient = -1;
    QPointF from, to;
    // A bitmap of the library, and where it lies (from its pixels to the area's coordinates).
    QString bitmap;
    QTransform bitmapPlace;
};

// The part of a picture that shows (its clip, in its own pixels; empty: all of it), cut to an area of the drawing.
QVector<QPolygonF> clipTo(Picture const& picture, QVector<QPolygonF> const& area)
{
    QTransform const back = picture.placement.inverted();
    QPainterPath cut;
    cut.setFillRule(Qt::WindingFill);
    for (QPolygonF const& ring : area)
        cut.addPolygon(back.map(ring));
    cut = cut.simplified();
    if (!picture.clip.isEmpty()) {
        QPainterPath own;
        for (QPolygonF const& ring : picture.clip)
            own.addPolygon(ring);
        cut = own.intersected(cut);
    }
    QVector<QPolygonF> out;
    for (QPolygonF const& ring : cut.toSubpathPolygons())
        if (ring.size() >= 3)
            out.push_back(ring);
    return out;
}

// How a line is drawn: its colour and thickness, how its thickness goes with scaling (0: with it, 1: not at all, 2: only
// with the scaling across, 3: only with that down), its ends (round, flat, or square - flat, half the thickness beyond the
// end) and its corners.
struct LineStyle {
    QColor colour = Qt::black;
    double weight = 1.0;
    int scaling = 0;
    Stroke::Cap cap = Stroke::RoundCap;
    bool square = false;
    Stroke::Join join = Stroke::RoundJoin;
};

LineStyle lineStyleOf(Node const& kind)
{
    LineStyle st;
    st.weight = kind.number(QStringLiteral("weight"), 1.0);
    QString const scale = kind.attr(QStringLiteral("scaleMode"));
    st.scaling = scale == QLatin1String("none") ? 1 : scale == QLatin1String("horizontal") ? 2 : scale == QLatin1String("vertical") ? 3 : 0;
    QString const caps = kind.attr(QStringLiteral("caps"));
    st.cap = caps == QLatin1String("none") || caps == QLatin1String("square") ? Stroke::FlatCap : Stroke::RoundCap;
    st.square = caps == QLatin1String("square");
    QString const joints = kind.attr(QStringLiteral("joints"));
    st.join = joints == QLatin1String("bevel") ? Stroke::BevelJoin : joints == QLatin1String("miter") ? Stroke::MitreJoin : Stroke::RoundJoin;
    st.colour = kind.child(QStringLiteral("fill")) ? colourOf(*kind.child(QStringLiteral("fill"))) : QColor(Qt::black);
    return st;
}

// A line of so many points, drawn in a style where `place` puts it.
Stroke strokeOf(QPolygonF const& points, LineStyle const& st, QTransform const& place)
{
    Stroke stroke;
    double const grow = st.scaling == 1 ? 1.0
                        : st.scaling == 2 ? std::hypot(place.m11(), place.m12())
                        : st.scaling == 3 ? std::hypot(place.m21(), place.m22())
                                          : std::sqrt(std::abs(place.determinant()));
    stroke.width = float(std::max(0.25, st.weight * grow));
    stroke.capStart = stroke.capEnd = st.cap;
    stroke.join = st.join;
    QPolygonF at = place.map(points);
    if (st.square && at.size() >= 2 && at.first() != at.last()) {
        // (A square end: half the thickness beyond the point.)
        auto reach = [&](int end, int towards) {
            QPointF const d = at[end] - at[towards];
            double const length = std::hypot(d.x(), d.y());
            if (length > 1e-9)
                at[end] += d * (stroke.width / 2.0 / length);
        };
        reach(0, 1);
        reach(int(at.size()) - 1, int(at.size()) - 2);
    }
    for (QPointF const& p : std::as_const(at))
        stroke.points.push_back({p, 1.0f});
    return stroke;
}

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

Shape readShape(Node const& shape, std::function<Paint(Node const&)> const& paintOf)
{
    Shape out;
    QHash<int, Paint> fills;
    QHash<int, LineStyle> strokes;
    if (Node const* f = shape.child(QStringLiteral("fills")))
        for (Node const& style : f->children)
            fills.insert(int(style.number(QStringLiteral("index"), 0)), paintOf(style));
    if (Node const* s = shape.child(QStringLiteral("strokes")))
        for (Node const& style : s->children) {
            if (style.children.isEmpty())
                continue;
            strokes.insert(int(style.number(QStringLiteral("index"), 0)), lineStyleOf(style.children.first()));
        }
    struct Piece {
        QPolygonF points;
        int fill0, fill1, stroke;
    };
    QVector<Piece> pieces;
    if (Node const* edges = shape.child(QStringLiteral("edges"))) {
        for (Node const& edge : edges->children) {
            QString const s = edge.attr(QStringLiteral("edges"));
            if (s.isEmpty())
                continue;
            int const f0 = int(edge.number(QStringLiteral("fillStyle0"), 0)), f1 = int(edge.number(QStringLiteral("fillStyle1"), 0));
            int const st = int(edge.number(QStringLiteral("strokeStyle"), 0));
            QPointF at;
            Piece run{QPolygonF(), f0, f1, st};
            auto flush = [&] {
                if (run.points.size() >= 2)
                    pieces.push_back(run);
                run.points.clear();
            };
            for (int i = 0; i < s.size();) {
                QChar const c = s[i];
                if (c == QLatin1Char('!')) {
                    ++i;
                    skipSpaces(s, i);
                    double const x = edgeNumber(s, i);
                    skipSpaces(s, i);
                    double const y = edgeNumber(s, i);
                    QPointF const to(x / 20.0, y / 20.0);
                    // A move to where the line already is goes on with it.
                    if (run.points.isEmpty() || QLineF(run.points.last(), to).length() > 1e-6) {
                        flush();
                        run.points << to;
                    }
                    at = to;
                } else if (c == QLatin1Char('|') || c == QLatin1Char('/')) {
                    ++i;
                    skipSpaces(s, i);
                    double const x = edgeNumber(s, i);
                    skipSpaces(s, i);
                    double const y = edgeNumber(s, i);
                    at = QPointF(x / 20.0, y / 20.0);
                    run.points << at;
                } else if (c == QLatin1Char('[') || c == QLatin1Char(']')) {
                    ++i;
                    skipSpaces(s, i);
                    double const cx = edgeNumber(s, i);
                    skipSpaces(s, i);
                    double const cy = edgeNumber(s, i);
                    skipSpaces(s, i);
                    double const x = edgeNumber(s, i);
                    skipSpaces(s, i);
                    double const y = edgeNumber(s, i);
                    QPointF const control(cx / 20.0, cy / 20.0), end(x / 20.0, y / 20.0), from = at;
                    for (int k = 1; k <= 8; ++k) {
                        double const t = k / 8.0;
                        run.points << from * ((1 - t) * (1 - t)) + control * (2 * t * (1 - t)) + end * (t * t);
                    }
                    at = end;
                } else if (c == QLatin1Char('S')) {
                    ++i;
                    while (i < s.size() && s[i].isDigit())
                        ++i;
                } else if (c == QLatin1Char('(')) {
                    // (Cubic hints: what follows says the same in quadratics.)
                    while (i < s.size() && s[i] != QLatin1Char(')'))
                        ++i;
                    ++i;
                } else {
                    ++i;
                }
            }
            flush();
        }
    }
    // Fills: their edges turned to one side and joined end to start into rings.
    auto key = [](QPointF p) { return qint64(std::llround(p.x() * 20.0)) * 4000037 + qint64(std::llround(p.y() * 20.0)); };
    for (auto it = fills.cbegin(); it != fills.cend(); ++it) {
        int const s = it.key();
        QVector<QPolygonF> parts;
        for (Piece const& p : std::as_const(pieces)) {
            if (p.fill1 == s)
                parts.push_back(p.points);
            if (p.fill0 == s) {
                QPolygonF back = p.points;
                std::reverse(back.begin(), back.end());
                parts.push_back(back);
            }
        }
        if (parts.isEmpty())
            continue;
        QMultiHash<qint64, int> starting;
        for (int i = 0; i < parts.size(); ++i)
            starting.insert(key(parts[i].first()), i);
        QVector<bool> used(parts.size(), false);
        Shape::Area area;
        area.paint = it.value();
        for (int i = 0; i < parts.size(); ++i) {
            if (used[i])
                continue;
            QPolygonF ring = parts[i];
            used[i] = true;
            for (int guard = 0; guard < parts.size() && key(ring.last()) != key(ring.first()); ++guard) {
                int next = -1;
                for (auto at = starting.constFind(key(ring.last())); at != starting.cend() && at.key() == key(ring.last()); ++at)
                    if (!used[*at]) {
                        next = *at;
                        break;
                    }
                if (next < 0)
                    break;
                used[next] = true;
                ring += parts[next].mid(1);
            }
            if (ring.size() > 1 && key(ring.first()) == key(ring.last()))
                ring.removeLast();
            if (ring.size() >= 3)
                area.rings.push_back(ring);
        }
        if (!area.rings.isEmpty())
            out.areas.push_back(area);
    }
    // Lines: one run of edges of one stroke style, joined where they meet.
    for (Piece const& p : std::as_const(pieces)) {
        if (p.stroke == 0 || !strokes.contains(p.stroke))
            continue;
        LineStyle const& st = strokes[p.stroke];
        if (!out.lines.isEmpty() && out.lines.last().style.colour == st.colour && out.lines.last().style.weight == st.weight
            && key(out.lines.last().points.last()) == key(p.points.first())) {
            out.lines.last().points += p.points.mid(1);
            continue;
        }
        out.lines.push_back({st, p.points});
    }
    return out;
}

// A bitmap of the document, from its file in the .fla: a JPEG as it is, or Flash's own (03 05: so many rows of A, R, G, B
// bytes, zlib-packed in pieces); and the size it is shown at (its frame, in pixels).
QImage bitmapOf(QByteArray const& b, QSizeF* shown)
{
    if (b.startsWith("\xff\xd8")) {
        QImage const image = QImage::fromData(b, "JPG");
        if (shown)
            *shown = QSizeF(image.size());
        return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (b.size() < 26 || quint8(b[0]) != 0x03 || quint8(b[1]) != 0x05)
        return QImage();
    auto u16 = [&](int at) { return int(quint8(b[at]) | (quint8(b[at + 1]) << 8)); };
    auto u32 = [&](int at) { return quint32(u16(at)) | (quint32(u16(at + 2)) << 16); };
    int const stride = u16(2), width = u16(4), height = u16(6);
    double const frameWidth = (double(u32(12)) - double(u32(8))) / 20.0, frameHeight = (double(u32(20)) - double(u32(16))) / 20.0;
    bool const alpha = b[24] != 0;
    if (width <= 0 || height <= 0 || stride < width * 4)
        return QImage();
    QByteArray packed;
    int p = 26;
    while (p + 2 <= b.size()) {
        int const length = u16(p);
        p += 2;
        if (length == 0 || p + length > b.size())
            break;
        packed += b.mid(p, length);
        p += length;
    }
    // (qUncompress wants the length first, big-endian, then the zlib stream as it is.)
    quint32 const expected = quint32(stride) * quint32(height);
    QByteArray prefixed;
    prefixed.append(char(expected >> 24));
    prefixed.append(char((expected >> 16) & 0xff));
    prefixed.append(char((expected >> 8) & 0xff));
    prefixed.append(char(expected & 0xff));
    QByteArray const pixels = qUncompress(prefixed + packed);
    if (pixels.size() < qsizetype(expected))
        return QImage();
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        uchar const* in = reinterpret_cast<uchar const*>(pixels.constData()) + qsizetype(y) * stride;
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int const a = alpha ? in[4 * x] : 255;
            out[x] = qRgba(std::min<int>(in[4 * x + 1], a), std::min<int>(in[4 * x + 2], a), std::min<int>(in[4 * x + 3], a), a);
        }
    }
    if (shown)
        *shown = frameWidth > 0.0 && frameHeight > 0.0 ? QSizeF(frameWidth, frameHeight) : QSizeF(width, height);
    return image;
}

class Reader {
public:
    QHash<QString, QByteArray> const* files = nullptr;
    QHash<QString, QString> bitmapFiles;    // a bitmap of the library, by its name: its file in bin/
    struct Bitmap {
        QImage image;
        QSizeF shown;
    };
    QHash<QString, Bitmap> bitmaps;
    QHash<QString, Node> symbols;           // library symbols, by their names: their timelines
    QHash<Node const*, std::shared_ptr<Shape>> shapes;
    QHash<Node const*, int> lengths;
    QHash<QByteArray, int> gradientKeys;    // a gradient of `out->gradients`, by what it is
    FlaImport::Document* out = nullptr;

    // The gradient, among the document's (the one already there that is the same, or added).
    int gradient(Swatch const& s)
    {
        QByteArray key;
        key += char(s.kind);
        for (GradientStop const& stop : s.stops)
            key += QByteArray::number(stop.at, 'f', 4) + ':' + QByteArray::number(stop.color.rgba()) + ';';
        auto found = gradientKeys.constFind(key);
        if (found != gradientKeys.cend())
            return *found;
        out->gradients.push_back(s);
        gradientKeys.insert(key, int(out->gradients.size()) - 1);
        return int(out->gradients.size()) - 1;
    }

    // How a fill style paints: a solid colour; a gradient (its box, -819.2 to 819.2 each way, placed by its matrix);
    // grey for a bitmap.
    Paint paintOf(Node const& fill)
    {
        Paint paint;
        paint.colour = colourOf(fill);
        if (Node const* b = fill.child(QStringLiteral("BitmapFill"))) {
            // (Its matrix, as a movie's: twentieths of the bitmap's pixels.)
            QTransform const m = matrixOf(*b);
            paint.bitmap = b->attr(QStringLiteral("bitmapPath"));
            paint.bitmapPlace = QTransform(m.m11() / 20.0, m.m12() / 20.0, m.m21() / 20.0, m.m22() / 20.0, m.dx(), m.dy());
            return paint;
        }
        for (Node const& g : fill.children) {
            bool const linear = g.name == QLatin1String("LinearGradient"), radial = g.name == QLatin1String("RadialGradient");
            if (!linear && !radial)
                continue;
            Swatch s;
            s.name = tr("Flash gradient");
            s.kind = linear ? Swatch::Linear : Swatch::Radial;
            for (Node const& entry : g.children) {
                if (entry.name != QLatin1String("GradientEntry"))
                    continue;
                QColor c(entry.attr(QStringLiteral("color"), QStringLiteral("#000000")));
                c.setAlphaF(float(entry.number(QStringLiteral("alpha"), 1.0)));
                s.stops.push_back({std::clamp(entry.number(QStringLiteral("ratio"), 0.0), 0.0, 1.0), c});
            }
            if (s.stops.size() < 2)
                break;
            s.color = s.stops.first().color;
            QTransform const m = matrixOf(g);
            paint.from = linear ? m.map(QPointF(-819.2, 0.0)) : m.map(QPointF(0.0, 0.0));
            paint.to = m.map(QPointF(819.2, 0.0));
            paint.gradient = gradient(s);
            break;
        }
        return paint;
    }

    // A fill painted as `paint` says, coloured by `tint`, placed by `place`.
    void paintFill(Fill& fill, Paint const& paint, Tint const& tint, QTransform const& place)
    {
        fill.color = tint.apply(paint.colour);
        if (paint.gradient < 0 || paint.gradient >= out->gradients.size())
            return;
        Swatch s = out->gradients[paint.gradient];
        bool plain = true;
        for (int k = 0; k < 4; ++k)
            plain = plain && tint.mult[k] == 1.0 && tint.add[k] == 0.0;
        if (!plain) {
            for (GradientStop& stop : s.stops)
                stop.color = tint.apply(stop.color);
            s.color = s.stops.first().color;
        }
        fill.swatch = plain ? paint.gradient : gradient(s);
        fill.gradPlaced = true;
        fill.gradFrom = place.map(paint.from);
        fill.gradTo = place.map(paint.to);
    }

    // How many frames a timeline runs.
    int length(Node const& timeline)
    {
        if (auto found = lengths.constFind(&timeline); found != lengths.cend())
            return *found;
        int most = 1;
        if (Node const* layers = timeline.child(QStringLiteral("layers")))
            for (Node const& layer : layers->children)
                if (Node const* frames = layer.child(QStringLiteral("frames")))
                    for (Node const& frame : frames->children)
                        most = std::max(most, int(frame.number(QStringLiteral("index"), 0) + frame.number(QStringLiteral("duration"), 1)));
        lengths.insert(&timeline, most);
        return most;
    }

    Node const* timelineOf(QString const& symbol)
    {
        auto found = symbols.constFind(symbol);
        if (found == symbols.cend())
            return nullptr;
        Node const* t = found->child(QStringLiteral("timeline"));
        return t ? t->child(QStringLiteral("DOMTimeline")) : nullptr;
    }

    // What a layer shows on a frame, drawn into `drawing` placed by `place`.
    void layer(Node const& layerNode, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
    {
        Node const* frames = layerNode.child(QStringLiteral("frames"));
        if (!frames)
            return;
        for (int k = 0; k < frames->children.size(); ++k) {
            Node const& f = frames->children[k];
            int const index = int(f.number(QStringLiteral("index"), 0)), duration = int(f.number(QStringLiteral("duration"), 1));
            if (frame < index || frame >= index + duration)
                continue;
            Node const* elements = f.child(QStringLiteral("elements"));
            if (!elements)
                return;
            // A classic motion tween: each placed thing on its way to where it is on the next keyframe.  A shape tween: the
            // drawing itself on its way to the next keyframe's.
            Node const* next = k + 1 < frames->children.size() ? &frames->children[k + 1] : nullptr;
            Node const* nextElements = next ? next->child(QStringLiteral("elements")) : nullptr;
            bool const tween = f.attr(QStringLiteral("tweenType")) == QLatin1String("motion") && nextElements && duration > 0;
            bool const morph = (f.child(QStringLiteral("MorphShape")) || f.attr(QStringLiteral("tweenType")) == QLatin1String("shape")) && nextElements
                               && !nextElements->children.isEmpty() && duration > 0;
            double const t = tween || morph ? eased(f, double(frame - index) / double(duration)) : 0.0;
            if (morph && t > 0.0) {
                Drawing from, to;
                int fromOrder = 0, toOrder = 0;
                for (Node const& element : elements->children)
                    draw(element, frame - index, matrixOf(element) * place, tint, from, fromOrder, depth);
                for (Node const& element : nextElements->children)
                    draw(element, 0, matrixOf(element) * place, tint, to, toOrder, depth);
                Drawing const mid = Morphing::between(from, to, t);
                int const base = order;
                for (Fill fill : mid.fills) {
                    fill.order += base;
                    order = std::max(order, fill.order);
                    drawing.fills.push_back(fill);
                }
                for (Stroke stroke : mid.strokes) {
                    stroke.order += base;
                    order = std::max(order, stroke.order);
                    drawing.strokes.push_back(stroke);
                }
                drawing.pictures += from.pictures;
                return;
            }
            for (int e = 0; e < elements->children.size(); ++e) {
                Node const& element = elements->children[e];
                QTransform own = matrixOf(element);
                if (tween && e < nextElements->children.size()) {
                    Node const& to = nextElements->children[e];
                    if (to.name == element.name && to.attr(QStringLiteral("libraryItemName")) == element.attr(QStringLiteral("libraryItemName")))
                        own = between(own, matrixOf(to), t, pivotOf(element));
                }
                draw(element, frame - index, own * place, tint, drawing, order, depth);
            }
            return;
        }
    }

    // Where a placed thing turns and scales about, in its own coordinates.
    static QPointF pivotOf(Node const& element)
    {
        Node const* holder = element.child(QStringLiteral("transformationPoint"));
        Node const* point = holder ? holder->child(QStringLiteral("Point")) : nullptr;
        return point ? QPointF(point->number(QStringLiteral("x"), 0.0), point->number(QStringLiteral("y"), 0.0)) : QPointF();
    }

    // How far along a tween is when `t` (0..1) of its time has passed: by its custom ease (a curve of cubic pieces through
    // points, from (0, 0) to (1, 1)), or its ease out (above 0) or in (below), -100 to 100.
    static double eased(Node const& frame, double t)
    {
        double ease = frame.number(QStringLiteral("acceleration"), 0.0) / 100.0;
        if (Node const* tweens = frame.child(QStringLiteral("tweens"))) {
            for (Node const& e : tweens->children) {
                QString const target = e.attr(QStringLiteral("target"), QStringLiteral("all"));
                if (target != QLatin1String("all") && target != QLatin1String("position"))
                    continue;
                if (e.name == QLatin1String("CustomEase") && frame.attr(QStringLiteral("hasCustomEase")) == QLatin1String("true")) {
                    QVector<QPointF> points;
                    for (Node const& p : e.children)
                        if (p.name == QLatin1String("Point"))
                            points.push_back(QPointF(p.number(QStringLiteral("x"), 0.0), p.number(QStringLiteral("y"), 0.0)));
                    if (points.size() < 4 || (points.size() - 1) % 3 != 0)
                        continue;
                    int s = 0;
                    while (s + 6 < points.size() && t > points[s + 3].x())
                        s += 3;
                    auto at = [&](double u) {
                        double const v = 1.0 - u;
                        return points[s] * (v * v * v) + points[s + 1] * (3 * v * v * u) + points[s + 2] * (3 * v * u * u) + points[s + 3] * (u * u * u);
                    };
                    double lo = 0.0, hi = 1.0;
                    for (int i = 0; i < 40; ++i) {
                        double const mid = (lo + hi) / 2.0;
                        (at(mid).x() < t ? lo : hi) = mid;
                    }
                    return at((lo + hi) / 2.0).y();
                }
                if (e.name == QLatin1String("Ease"))
                    ease = e.number(QStringLiteral("intensity"), 0.0) / 100.0;
            }
        }
        return std::clamp(t + ease * t * (1.0 - t), 0.0, 1.0);
    }

    // `t` of the way from one placing to another: turned, scaled and leaned part by part about `pivot` (a straight blend
    // of the numbers would shrink what turns), the pivot itself going straight from where it was to where it goes.
    static QTransform between(QTransform const& a, QTransform const& b, double t, QPointF pivot = QPointF())
    {
        // Turned, scaled and moved part by part (a straight blend of the numbers would shrink what turns).
        auto parts = [](QTransform const& m, double* v) {
            v[0] = std::atan2(m.m12(), m.m11());
            v[1] = std::atan2(-m.m21(), m.m22());
            v[2] = std::hypot(m.m11(), m.m12());
            v[3] = std::hypot(m.m21(), m.m22());
            v[4] = m.dx();
            v[5] = m.dy();
        };
        double pa[6], pb[6], v[6];
        parts(a, pa);
        parts(b, pb);
        // (The second angle as a lean from the first: so that half a turn, whose two angles may come out as a half turn
        // one way and the other, blends as a turn and not as a lean.)
        double const turn = 2.0 * 3.14159265358979323846;
        pa[1] = std::remainder(pa[1] - pa[0], turn);
        pb[1] = std::remainder(pb[1] - pb[0], turn);
        for (int k = 0; k < 6; ++k) {
            double d = pb[k] - pa[k];
            if (k == 0)
                d = std::remainder(d, turn);
            v[k] = pa[k] + d * t;
        }
        v[1] += v[0];
        QTransform const linear(v[2] * std::cos(v[0]), v[2] * std::sin(v[0]), -v[3] * std::sin(v[1]), v[3] * std::cos(v[1]), 0.0, 0.0);
        QPointF const from = a.map(pivot), to = b.map(pivot);
        QPointF const centre = from + (to - from) * t - linear.map(pivot);
        return QTransform(linear.m11(), linear.m12(), linear.m21(), linear.m22(), centre.x(), centre.y());
    }

    void timeline(Node const& t, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
    {
        Node const* layers = t.child(QStringLiteral("layers"));
        if (!layers)
            return;
        // From the bottom layer up (the list goes from the top); guides, folders and masks not drawn - what a mask covers
        // cut to it.
        for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
            Node const& layerNode = layers->children[l];
            QString const type = layerNode.attr(QStringLiteral("layerType"));
            if (type == QLatin1String("guide") || type == QLatin1String("folder") || type == QLatin1String("mask"))
                continue;
            if (layerNode.attr(QStringLiteral("visible")) == QLatin1String("false"))
                continue;
            int const parent = int(layerNode.number(QStringLiteral("parentLayerIndex"), -1));
            Node const* mask = parent >= 0 && parent < layers->children.size()
                                       && layers->children[parent].attr(QStringLiteral("layerType")) == QLatin1String("mask")
                                   ? &layers->children[parent]
                                   : nullptr;
            if (!mask) {
                layer(layerNode, frame, place, tint, drawing, order, depth);
                continue;
            }
            Drawing masked, shape;
            int maskOrder = 0;
            layer(layerNode, frame, place, tint, masked, order, depth);
            layer(*mask, frame, place, Tint(), shape, maskOrder, depth);
            QVector<QPolygonF> area;
            for (Fill const& f : std::as_const(shape.fills))
                area += f.outlines;
            for (Stroke const& s : std::as_const(shape.strokes))
                area += Fill::fromStroke(s, 0.25).outlines;
            for (Picture const& p : std::as_const(shape.pictures))
                area.push_back(p.placement.map(QPolygonF(QRectF(QPointF(0, 0), QSizeF(p.size)))));
            if (area.isEmpty())
                continue;   // (an empty mask lets nothing through)
            QRectF reach;
            for (QPolygonF const& ring : std::as_const(area))
                reach = reach.united(ring.boundingRect());
            auto cut = [&](Fill const& whole) {
                if (!whole.bounds().intersects(reach))
                    return;
                for (Fill piece : whole.within(area, 0.25)) {
                    piece.color = whole.color;
                    piece.order = whole.order;
                    piece.swatch = whole.swatch;
                    piece.gradPlaced = whole.gradPlaced;
                    piece.gradFrom = whole.gradFrom;
                    piece.gradTo = whole.gradTo;
                    drawing.fills.push_back(piece);
                }
            };
            for (Fill const& f : std::as_const(masked.fills))
                cut(f);
            for (Stroke const& s : std::as_const(masked.strokes)) {
                Fill asArea = Fill::fromStroke(s, 0.25);
                asArea.color = s.color;
                asArea.order = s.order;
                cut(asArea);
            }
            for (Picture p : std::as_const(masked.pictures)) {
                if (!p.bounds().intersects(reach))
                    continue;
                p.clip = clipTo(p, area);
                if (!p.clip.isEmpty())
                    drawing.pictures.push_back(p);
            }
        }
    }

    void shapeInto(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
    {
        std::shared_ptr<Shape>& cached = shapes[&element];
        if (!cached)
            cached = std::make_shared<Shape>(readShape(element, [this](Node const& fill) { return paintOf(fill); }));
        ++order;
        for (Shape::Area const& area : cached->areas) {
            if (!area.paint.bitmap.isEmpty()) {
                // Filled with a bitmap: the bitmap where the fill puts it, only where the area is.
                Bitmap const& found = bitmap(area.paint.bitmap);
                if (!found.image.isNull()) {
                    Picture picture;
                    picture.image = found.image;
                    picture.size = found.image.size();
                    picture.order = order;
                    // (The fill's matrix starts from the bitmap's own pixels, whatever size the bitmap is shown at elsewhere.)
                    QTransform const own = area.paint.bitmapPlace;
                    picture.placement = own * place;
                    QTransform const back = own.inverted();
                    for (QPolygonF const& ring : area.rings)
                        picture.clip.push_back(back.map(ring));
                    drawing.pictures.push_back(picture);
                    continue;
                }
            }
            Fill fill;
            paintFill(fill, area.paint, tint, place);
            fill.order = order;
            for (QPolygonF const& ring : area.rings)
                fill.outlines.push_back(place.map(ring));
            if (fill.color.alpha() > 0 || fill.swatch >= 0)
                drawing.fills.push_back(fill);
        }
        for (Shape::Line const& line : cached->lines) {
            Stroke stroke = strokeOf(line.points, line.style, place);
            stroke.color = tint.apply(line.style.colour);
            stroke.order = order;
            if (stroke.points.size() >= 2 && stroke.color.alpha() > 0)
                drawing.strokes.push_back(stroke);
        }
    }

    // The font a run of text names: Flash names a font as PostScript does ("Arial-BoldMT", "ComicSansMS-Bold"), so the
    // family is looked for as it is, then without its style and maker's mark, then with its words spaced out.
    static QFont fontOf(Node const* attrs)
    {
        QString const face = attrs ? attrs->attr(QStringLiteral("face"), QStringLiteral("Arial")) : QStringLiteral("Arial");
        QString const style = face.section(QLatin1Char('-'), 1).toLower();
        QString family = face.section(QLatin1Char('-'), 0, 0);
        for (QString const& mark : {QStringLiteral("PSMT"), QStringLiteral("MT"), QStringLiteral("PS")})
            if (family.endsWith(mark) && family.size() > mark.size() && !QFontDatabase::hasFamily(family))
                family.chop(mark.size());
        if (!QFontDatabase::hasFamily(face) && !QFontDatabase::hasFamily(family)) {
            QString spaced;
            for (int i = 0; i < family.size(); ++i) {
                if (i > 0 && family[i].isUpper() && (family[i - 1].isLower() || (i + 1 < family.size() && family[i + 1].isLower())))
                    spaced += QLatin1Char(' ');
                spaced += family[i];
            }
            if (QFontDatabase::hasFamily(spaced))
                family = spaced;
        }
        QFont font(QFontDatabase::hasFamily(face) ? face : family);
        double const size = attrs ? attrs->number(QStringLiteral("size"), 12.0) : 12.0;
        font.setPixelSize(std::max(1, int(std::lround(size))));
        font.setBold((attrs && attrs->attr(QStringLiteral("bold")) == QLatin1String("true")) || style.contains(QLatin1String("bold")));
        font.setItalic((attrs && attrs->attr(QStringLiteral("italic")) == QLatin1String("true")) || style.contains(QLatin1String("italic"))
                       || style.contains(QLatin1String("oblique")));
        font.setKerning(!attrs || attrs->attr(QStringLiteral("autoKern")) != QLatin1String("false"));
        if (attrs && attrs->attributes.contains(QStringLiteral("letterSpacing")))
            font.setLetterSpacing(QFont::AbsoluteSpacing, attrs->number(QStringLiteral("letterSpacing"), 0.0));
        return font;
    }

    // Words: each run in its font, size and colour, line under line (each aligned in the width of the text box), as the
    // shapes of their letters.
    void text(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
    {
        Node const* runs = element.child(QStringLiteral("textRuns"));
        if (!runs)
            return;
        struct Piece {
            QString words;
            QFont font;
            QColor colour;
        };
        struct Line {
            QVector<Piece> pieces;
            QString alignment;
            double height = -1.0;       // as the text says (or -1: as its fonts are)
            double spacing = 2.0;       // and the room under it
        };
        QVector<Line> lines(1);
        for (Node const& run : runs->children) {
            Node const* characters = run.child(QStringLiteral("characters"));
            Node const* attrsHolder = run.child(QStringLiteral("textAttrs"));
            Node const* attrs = attrsHolder ? attrsHolder->child(QStringLiteral("DOMTextAttrs")) : nullptr;
            QString const words = characters ? characters->attr(QStringLiteral("#text")) : QString();
            QColor colour(attrs ? attrs->attr(QStringLiteral("fillColor"), QStringLiteral("#000000")) : QStringLiteral("#000000"));
            colour.setAlphaF(float(attrs ? attrs->number(QStringLiteral("alpha"), 1.0) : 1.0));
            QFont const font = fontOf(attrs);
            QStringList const parts = QString(words).replace(QLatin1Char('\n'), QLatin1Char('\r')).split(QLatin1Char('\r'));
            for (int l = 0; l < parts.size(); ++l) {
                if (l > 0)
                    lines.push_back(Line());
                Line& line = lines.last();
                if (line.pieces.isEmpty() && attrs) {
                    line.alignment = attrs->attr(QStringLiteral("alignment"), QStringLiteral("left"));
                    line.height = attrs->number(QStringLiteral("lineHeight"), -1.0);
                    line.spacing = attrs->number(QStringLiteral("lineSpacing"), 2.0);
                }
                line.pieces.push_back({parts[l], font, colour});
            }
        }
        // (A text box has a gutter of 2 pixels all round.)
        double const left = element.number(QStringLiteral("left"), 0.0) + 2.0;
        double const width = std::max(0.0, element.number(QStringLiteral("width"), 0.0));
        double y = element.number(QStringLiteral("top"), 0.0) + 2.0;
        ++order;
        for (Line const& line : std::as_const(lines)) {
            double ascent = 0.0, descent = 0.0, across = 0.0;
            for (Piece const& piece : line.pieces) {
                QFontMetricsF const metrics(piece.font);
                ascent = std::max(ascent, metrics.ascent());
                descent = std::max(descent, metrics.descent());
                across += metrics.horizontalAdvance(piece.words);
            }
            if (line.pieces.isEmpty())
                ascent = 12.0;
            double x = left;
            if (line.alignment == QLatin1String("center"))
                x += (width - across) / 2.0;
            else if (line.alignment == QLatin1String("right"))
                x += width - across;
            double const baseline = y + ascent;
            for (Piece const& piece : line.pieces) {
                if (!piece.words.isEmpty()) {
                    QPainterPath path;
                    path.addText(QPointF(x, baseline), piece.font, piece.words);
                    Fill fill;
                    fill.color = tint.apply(piece.colour);
                    fill.order = order;
                    for (QPolygonF const& outline : path.toSubpathPolygons())
                        if (outline.size() >= 3)
                            fill.outlines.push_back(place.map(outline));
                    if (!fill.outlines.isEmpty())
                        drawing.fills.push_back(fill);
                }
                x += QFontMetricsF(piece.font).horizontalAdvance(piece.words);
            }
            y += (line.height > 0.0 ? line.height : ascent + descent) + line.spacing;
        }
    }

    // A bitmap of the library, read once.
    Bitmap const& bitmap(QString const& name)
    {
        auto found = bitmaps.find(name);
        if (found == bitmaps.end()) {
            Bitmap made;
            if (files && bitmapFiles.contains(name))
                made.image = bitmapOf(files->value(QStringLiteral("bin/") + bitmapFiles.value(name)), &made.shown);
            found = bitmaps.insert(name, made);
        }
        return *found;
    }

    // A video of the library: its frames, decoded once (none, when they could not be).
    struct Video {
        QStringList files;
        QSize size;
    };
    QHash<QString, Node> videoItems;        // the videos of the library, by their names
    QHash<QString, Video> videos;
    QHash<QString, QImage> videoFrames;     // the frames, by their files
    QString mediaFolder;
    QTemporaryDir* passing = nullptr;

    Video const& video(QString const& name)
    {
        auto found = videos.find(name);
        if (found != videos.end())
            return *found;
        Video made;
        Node const item = videoItems.value(name);
        made.size = QSize(int(item.number(QStringLiteral("width"), 0)), int(item.number(QStringLiteral("height"), 0)));
        int codec = 0;
        QVector<QByteArray> frames;
        QVector<bool> keys;
        QString why = tr("The video's data is in a form Eclipse does not know.");
        if (files && FlashVideo::flaFrames(files->value(QStringLiteral("bin/") + item.attr(QStringLiteral("videoDataHRef"))), codec, frames, keys)) {
            QString folder = mediaFolder;
            if (folder.isEmpty() && passing && passing->isValid())
                folder = passing->path();
            QString title = name;
            title.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
            made.files = FlashVideo::decode(codec, frames, item.number(QStringLiteral("fps"), out->fps), folder, title, keys, &why);
        }
        if (made.files.isEmpty()) {
            ++out->skippedVideo;
            out->videoProblem = why;
        }
        return *videos.insert(name, made);
    }

    void draw(Node const& element, int offset, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
    {
        if (depth > 24)
            return;
        QString const& kind = element.name;
        if (kind == QLatin1String("DOMShape")) {
            shapeInto(element, place, tint, drawing, order);
        } else if (kind == QLatin1String("DOMGroup")) {
            if (Node const* members = element.child(QStringLiteral("members")))
                for (Node const& m : members->children)
                    draw(m, offset, matrixOf(m) * place, tint, drawing, order, depth + 1);
        } else if (kind == QLatin1String("DOMSymbolInstance")) {
            Node const* t = timelineOf(element.attr(QStringLiteral("libraryItemName")));
            if (!t)
                return;
            int const total = length(*t);
            int const first = int(element.number(QStringLiteral("firstFrame"), 0));
            QString const loop = element.attr(QStringLiteral("loop"), QStringLiteral("loop"));
            QString const type = element.attr(QStringLiteral("symbolType"), QStringLiteral("graphic"));
            int local = 0;
            if (type == QLatin1String("button"))
                local = 0;
            else if (type != QLatin1String("graphic"))
                local = offset % total;     // a movie clip plays along on its own
            else if (loop == QLatin1String("single frame"))
                local = first;
            else if (loop == QLatin1String("play once"))
                local = std::min(first + offset, total - 1);
            else
                local = (first + offset) % total;
            timeline(*t, std::clamp(local, 0, total - 1), place, tint.then(tintOf(element)), drawing, order, depth + 1);
        } else if (kind == QLatin1String("DOMRectangleObject") || kind == QLatin1String("DOMOvalObject")) {
            QRectF const box(element.number(QStringLiteral("x"), 0.0), element.number(QStringLiteral("y"), 0.0),
                             element.number(QStringLiteral("objectWidth"), 0.0), element.number(QStringLiteral("objectHeight"), 0.0));
            QPolygonF outline;
            if (kind == QLatin1String("DOMOvalObject")) {
                for (int k = 0; k < 48; ++k) {
                    double const a = k * 2.0 * 3.14159265358979323846 / 48.0;
                    outline << box.center() + QPointF(box.width() / 2.0 * std::cos(a), box.height() / 2.0 * std::sin(a));
                }
            } else {
                // Its corners rounded so far each (top left, top right, bottom right, bottom left), or square.
                double r[4] = {element.number(QStringLiteral("topLeftRadius"), 0.0), element.number(QStringLiteral("topRightRadius"), 0.0),
                               element.number(QStringLiteral("bottomRightRadius"), 0.0), element.number(QStringLiteral("bottomLeftRadius"), 0.0)};
                double const most = std::min(std::abs(box.width()), std::abs(box.height())) / 2.0;
                bool round = false;
                for (double& v : r) {
                    v = std::min(std::abs(v), most);
                    round = round || v > 1e-6;
                }
                if (round) {
                    QPainterPath path;
                    double const x = box.left(), y = box.top(), w = box.width(), h = box.height();
                    path.moveTo(x + r[0], y);
                    path.lineTo(x + w - r[1], y);
                    path.arcTo(QRectF(x + w - 2 * r[1], y, 2 * r[1], 2 * r[1]), 90, -90);
                    path.lineTo(x + w, y + h - r[2]);
                    path.arcTo(QRectF(x + w - 2 * r[2], y + h - 2 * r[2], 2 * r[2], 2 * r[2]), 0, -90);
                    path.lineTo(x + r[3], y + h);
                    path.arcTo(QRectF(x, y + h - 2 * r[3], 2 * r[3], 2 * r[3]), 270, -90);
                    path.lineTo(x, y + r[0]);
                    path.arcTo(QRectF(x, y, 2 * r[0], 2 * r[0]), 180, -90);
                    path.closeSubpath();
                    outline = path.toFillPolygon();
                    if (outline.size() > 1 && outline.first() == outline.last())
                        outline.removeLast();
                } else {
                    outline = QPolygonF(box);
                    outline.removeLast();
                }
            }
            ++order;
            if (Node const* fill = element.child(QStringLiteral("fill"))) {
                Fill f;
                paintFill(f, paintOf(*fill), tint, place);
                f.order = order;
                f.outlines = {place.map(outline)};
                drawing.fills.push_back(f);
            }
            if (Node const* stroke = element.child(QStringLiteral("stroke")); stroke && !stroke->children.isEmpty()) {
                LineStyle const style = lineStyleOf(stroke->children.first());
                Stroke s = strokeOf(outline, style, place);
                s.color = tint.apply(style.colour);
                s.order = order;
                s.closed = true;
                drawing.strokes.push_back(s);
            }
        } else if (kind == QLatin1String("DOMStaticText") || kind == QLatin1String("DOMDynamicText") || kind == QLatin1String("DOMInputText")) {
            text(element, place, tint, drawing, order);
        } else if (kind == QLatin1String("DOMBitmapInstance")) {
            Bitmap const& found = bitmap(element.attr(QStringLiteral("libraryItemName")));
            if (found.image.isNull()) {
                ++out->skippedPictures;
                return;
            }
            Picture picture;
            picture.image = found.image;
            picture.size = found.image.size();
            picture.placement = QTransform::fromScale(found.shown.width() / found.image.width(), found.shown.height() / found.image.height()) * place;
            picture.order = ++order;
            drawing.pictures.push_back(picture);
        } else if (kind == QLatin1String("DOMVideoInstance")) {
            // A frame of the video a timeline frame (the last one held), at the size of its frame.
            Video const& v = video(element.attr(QStringLiteral("libraryItemName")));
            if (v.files.isEmpty())
                return;
            QString const file = v.files[std::clamp(offset, 0, int(v.files.size()) - 1)];
            auto image = videoFrames.find(file);
            if (image == videoFrames.end())
                image = videoFrames.insert(file, QImage(file).convertToFormat(QImage::Format_ARGB32_Premultiplied));
            if (image->isNull())
                return;
            double const width = element.number(QStringLiteral("frameRight"), v.size.width() * 20.0) / 20.0;
            double const height = element.number(QStringLiteral("frameBottom"), v.size.height() * 20.0) / 20.0;
            Picture picture;
            picture.image = *image;
            picture.size = image->size();
            picture.placement = QTransform::fromScale(width / image->width(), height / image->height()) * place;
            picture.order = ++order;
            if (!mediaFolder.isEmpty()) {
                picture.file = file;
                picture.linked = true;
            }
            drawing.pictures.push_back(picture);
        }
    }
};

// What a drawing is, to find another the same.
QByteArray fingerprint(Drawing const& d)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    for (Fill const& f : d.fills) {
        s << f.color.rgba() << f.order;
        for (QPolygonF const& o : f.outlines)
            s << o;
    }
    for (Stroke const& st : d.strokes) {
        s << st.color.rgba() << st.width << st.order;
        for (StrokePoint const& p : st.points)
            s << p.pos;
    }
    for (Picture const& p : d.pictures)
        s << p.image.cacheKey() << p.placement;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
}

}  // namespace

namespace FlaImport {

bool read(QByteArray const& bytes, Document& out, QString* error, QString const& mediaFolder)
{
    auto fail = [&](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    if (!bytes.startsWith("PK"))
        return fail(tr("This .fla is not the kind saved by Flash CS5 or after (a zip of XML): it may be from an older Flash. "
                       "Open it in Flash or Animate and save it again, or export a .swf and import that."));
    QHash<QString, QByteArray> const files = Zip::read(bytes);
    if (!files.contains(QStringLiteral("DOMDocument.xml")))
        return fail(tr("The .fla could not be read (no DOMDocument.xml in it)."));
    Node document;
    if (!parse(files.value(QStringLiteral("DOMDocument.xml")), document))
        return fail(tr("The .fla's document could not be read."));
    out = Document();
    out.size = QSize(int(document.number(QStringLiteral("width"), 550)), int(document.number(QStringLiteral("height"), 400)));
    out.fps = document.number(QStringLiteral("frameRate"), 24.0);
    out.background = QColor(document.attr(QStringLiteral("backgroundColor"), QStringLiteral("#FFFFFF")));
    Reader reader;
    reader.out = &out;
    reader.files = &files;
    reader.mediaFolder = mediaFolder;
    QTemporaryDir passing;
    reader.passing = &passing;
    // The bitmaps of the library: each by its name, with the file in bin/ that holds it.  And its sounds: Flash keeps
    // them as plain samples ("44kHz 16bit Mono"), or as the MP3 they came as.
    QHash<QString, Sound> sounds;
    if (Node const* media = document.child(QStringLiteral("media")))
        for (Node const& item : media->children) {
            if (item.name == QLatin1String("DOMBitmapItem")) {
                reader.bitmapFiles.insert(item.attr(QStringLiteral("name")), item.attr(QStringLiteral("bitmapDataHRef")));
            } else if (item.name == QLatin1String("DOMVideoItem")) {
                reader.videoItems.insert(item.attr(QStringLiteral("name")), item);
            } else if (item.name == QLatin1String("DOMSoundItem")) {
                QByteArray const data = files.value(QStringLiteral("bin/") + item.attr(QStringLiteral("soundDataHRef")));
                QString const format = item.attr(QStringLiteral("format"));
                int const rate = format.startsWith(QLatin1String("5kHz")) ? 5512
                                 : format.startsWith(QLatin1String("11kHz")) ? 11025
                                 : format.startsWith(QLatin1String("22kHz")) ? 22050
                                                                              : 44100;
                int const bits = format.contains(QLatin1String("8bit")) ? 8 : 16;
                int const channels = format.contains(QLatin1String("Stereo")) ? 2 : 1;
                qint64 const samples = qint64(item.number(QStringLiteral("sampleCount"), 0.0));
                Sound sound;
                sound.name = item.attr(QStringLiteral("name"));
                if (data.startsWith("ID3") || (data.size() > 2 && quint8(data[0]) == 0xff && (quint8(data[1]) & 0xe0) == 0xe0
                                               && data.size() != samples * channels * bits / 8)) {
                    sound.bytes = data;
                    sound.suffix = QStringLiteral("mp3");
                } else if (!data.isEmpty()) {
                    sound.bytes = SwfImport::wav(data, rate, bits, channels);
                    sound.suffix = QStringLiteral("wav");
                }
                if (!sound.bytes.isEmpty())
                    sounds.insert(sound.name, sound);
            }
        }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!it.key().startsWith(QLatin1String("LIBRARY/")) || !it.key().endsWith(QLatin1String(".xml")))
            continue;
        Node symbol;
        if (parse(it.value(), symbol) && symbol.name == QLatin1String("DOMSymbolItem"))
            reader.symbols.insert(symbol.attr(QStringLiteral("name")), symbol);
    }
    Node const* timelines = document.child(QStringLiteral("timelines"));
    Node const* main = timelines && !timelines->children.isEmpty() ? &timelines->children.first() : nullptr;
    Node const* layers = main ? main->child(QStringLiteral("layers")) : nullptr;
    if (!layers)
        return fail(tr("The .fla has no timeline."));
    out.frames = reader.length(*main);
    // Each layer of the main timeline, from the bottom up.
    QHash<int, int> made;       // the index of a layer in the document: its index in `out.layers`
    for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
        Node const& layerNode = layers->children[l];
        QString const type = layerNode.attr(QStringLiteral("layerType"));
        if (type == QLatin1String("guide") || type == QLatin1String("folder"))
            continue;
        Layer layer;
        layer.name = layerNode.attr(QStringLiteral("name"), QStringLiteral("Layer"));
        layer.visible = layerNode.attr(QStringLiteral("visible")) != QLatin1String("false");
        layer.locked = layerNode.attr(QStringLiteral("locked")) == QLatin1String("true");
        layer.colour = QColor(layerNode.attr(QStringLiteral("color")));
        layer.mask = type == QLatin1String("mask");
        layer.cells.fill(-1, out.frames);
        QHash<QByteArray, int> known;
        Node const* frames = layerNode.child(QStringLiteral("frames"));
        for (int k = 0; frames && k < frames->children.size(); ++k) {
            Node const& f = frames->children[k];
            int const index = int(f.number(QStringLiteral("index"), 0)), duration = int(f.number(QStringLiteral("duration"), 1));
            // Its label; its script (kept as a note: it is not run); its sound.
            if (f.attributes.contains(QStringLiteral("name")) && type != QLatin1String("guide"))
                out.labels.push_back({index, std::max(1, duration), f.attr(QStringLiteral("name")), f.attr(QStringLiteral("labelType")) == QLatin1String("comment"),
                                      QString()});
            if (Node const* actions = f.child(QStringLiteral("Actionscript"))) {
                Node const* script = actions->child(QStringLiteral("script"));
                QString const code = script ? script->attr(QStringLiteral("#text")).trimmed() : QString();
                if (!code.isEmpty())
                    out.labels.push_back({index, 1, tr("Script"), true, code});
            }
            if (f.attributes.contains(QStringLiteral("soundName")) && f.attr(QStringLiteral("soundSync")) != QLatin1String("stop")) {
                QString const name = f.attr(QStringLiteral("soundName"));
                if (sounds.contains(name)) {
                    Sound sound = sounds.value(name);
                    sound.frame = index;
                    out.sounds.push_back(sound);
                } else {
                    ++out.skippedSounds;
                }
            }
            Node const* elements = f.child(QStringLiteral("elements"));
            if (!elements || elements->children.isEmpty())
                continue;
            for (int frame = index; frame < std::min(index + duration, out.frames); ++frame) {
                Drawing drawing;
                int order = 0;
                reader.layer(layerNode, frame, QTransform(), Tint(), drawing, order, 0);
                if (drawing.fills.isEmpty() && drawing.strokes.isEmpty() && drawing.pictures.isEmpty())
                    continue;
                QByteArray const print = fingerprint(drawing);
                auto found = known.constFind(print);
                if (found == known.cend()) {
                    found = known.insert(print, int(layer.drawings.size()));
                    layer.drawings.push_back(drawing);
                }
                layer.cells[frame] = *found;
            }
        }
        made.insert(l, int(out.layers.size()));
        out.layers.push_back(layer);
    }
    // Which layer masks which (a masked layer names the mask layer above it).
    for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
        Node const& layerNode = layers->children[l];
        if (!made.contains(l) || !layerNode.attributes.contains(QStringLiteral("parentLayerIndex")))
            continue;
        int const parent = int(layerNode.number(QStringLiteral("parentLayerIndex"), -1));
        if (made.contains(parent) && out.layers[made[parent]].mask)
            out.layers[made[l]].maskedBy = made[parent];
    }
    bool any = false;
    for (Layer const& layer : std::as_const(out.layers))
        any = any || !layer.drawings.isEmpty();
    if (!any)
        return fail(tr("Nothing Eclipse can read is drawn in this .fla (only video)."));
    std::stable_sort(out.labels.begin(), out.labels.end(), [](Label const& a, Label const& b) { return a.frame < b.frame; });
    // The document's swatches: its solid colours, each once.
    if (Node const* lists = document.child(QStringLiteral("swatchLists")))
        for (Node const& list : lists->children)
            if (Node const* swatches = list.child(QStringLiteral("swatches")))
                for (Node const& s : swatches->children) {
                    if (s.name != QLatin1String("SolidSwatchItem"))
                        continue;
                    QColor c(s.attr(QStringLiteral("color"), QStringLiteral("#000000")));
                    c.setAlphaF(float(s.number(QStringLiteral("alpha"), 1.0)));
                    if (c.isValid() && !out.swatches.contains(c))
                        out.swatches.push_back(c);
                }
    return true;
}

}
