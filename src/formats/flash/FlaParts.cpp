// Eclipse - reading the parts of a Flash document (.fla) from its XML.
#include "formats/flash/FlaParts.h"

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

QString tr(char const* text)
{
    return QCoreApplication::translate("FlaImport", text);
}

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

LineStyle lineStyleOf(Node const& kind)
{
    LineStyle st;
    if (kind.name == QLatin1String("RaggedStroke")) {
        st.ragged = true;
        static QStringList const heights{QStringLiteral("flat"), QStringLiteral("wavy"), QStringLiteral("veryWavy"), QStringLiteral("wild")};
        static QStringList const lengths{QStringLiteral("veryShort"), QStringLiteral("short"), QStringLiteral("medium"), QStringLiteral("long")};
        static QStringList const patterns{QStringLiteral("solid"), QStringLiteral("simple"), QStringLiteral("random"), QStringLiteral("dotted"),
                                          QStringLiteral("randomDotted"), QStringLiteral("tripleDotted"), QStringLiteral("randomTripleDotted")};
        st.waveHeight = std::max(0, int(heights.indexOf(kind.attr(QStringLiteral("waveHeight"), QStringLiteral("wavy")))));
        int const length = int(lengths.indexOf(kind.attr(QStringLiteral("waveLength"), QStringLiteral("medium"))));
        st.waveLength = length < 0 ? 2 : length;
        st.pattern = std::max(0, int(patterns.indexOf(kind.attr(QStringLiteral("pattern"), QStringLiteral("solid")))));
    } else if (kind.name == QLatin1String("DashedStroke")) {
        st.dashOn = std::max(0.25, kind.number(QStringLiteral("dash1"), 6.0));
        st.dashOff = std::max(0.25, kind.number(QStringLiteral("dash2"), 6.0));
    } else if (kind.name == QLatin1String("DottedStroke")) {
        st.dashOn = 0.01;
        st.dashOff = std::max(1.0, kind.number(QStringLiteral("dotSpace"), 3.0));
    }
    st.weight = kind.number(QStringLiteral("weight"), 1.0);
    QString const scale = kind.attr(QStringLiteral("scaleMode"));
    st.scaling = scale == QLatin1String("none") ? 1 : scale == QLatin1String("horizontal") ? 2 : scale == QLatin1String("vertical") ? 3 : 0;
    QString const caps = kind.attr(QStringLiteral("caps"));
    st.cap = caps == QLatin1String("none") || caps == QLatin1String("square") ? Stroke::FlatCap : Stroke::RoundCap;
    st.square = caps == QLatin1String("square");
    QString const joints = kind.attr(QStringLiteral("joints"));
    st.join = joints == QLatin1String("bevel") ? Stroke::BevelJoin : joints == QLatin1String("miter") ? Stroke::MitreJoin : Stroke::RoundJoin;
    st.colour = kind.child(QStringLiteral("fill")) ? colourOf(*kind.child(QStringLiteral("fill"))) : QColor(Qt::black);
    // Its width markers: where along it, and how far it reaches to each side (as shares of the weight).
    if (Node const* widths = kind.child(QStringLiteral("VariablePointWidth"))) {
        for (Node const& marker : widths->children)
            if (marker.name == QLatin1String("WidthMarker"))
                st.profile.push_back(QPointF(std::clamp(marker.number(QStringLiteral("position"), 0.0), 0.0, 1.0),
                                             std::max(0.0, marker.number(QStringLiteral("left"), 0.5) + marker.number(QStringLiteral("right"), 0.5))));
        std::stable_sort(st.profile.begin(), st.profile.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
    }
    return st;
}

double widthAlong(QVector<QPointF> const& profile, double at)
{
    if (profile.isEmpty())
        return 1.0;
    if (at <= profile.first().x())
        return profile.first().y();
    for (int i = 1; i < profile.size(); ++i)
        if (at <= profile[i].x()) {
            double const span = profile[i].x() - profile[i - 1].x();
            double const t = span > 1e-12 ? (at - profile[i - 1].x()) / span : 1.0;
            return profile[i - 1].y() + (profile[i].y() - profile[i - 1].y()) * t;
        }
    return profile.last().y();
}

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
    if (st.profile.isEmpty()) {
        for (QPointF const& p : std::as_const(at))
            stroke.points.push_back({p, 1.0f});
        return stroke;
    }
    // Of varying width: as wide as its widest at full pressure, each point's pressure what its place along it asks.
    double widest = 0.0;
    for (QPointF const& m : st.profile)
        widest = std::max(widest, m.y());
    if (widest <= 0.0)
        widest = 1.0;
    stroke.width = float(std::max(0.25, st.weight * grow * widest));
    QVector<double> along(at.size(), 0.0);
    for (int i = 1; i < at.size(); ++i)
        along[i] = along[i - 1] + QLineF(at[i - 1], at[i]).length();
    double const length = along.isEmpty() ? 0.0 : along.last();
    for (int i = 0; i < at.size(); ++i) {
        double const share = widthAlong(st.profile, length > 1e-9 ? along[i] / length : 0.0) / widest;
        stroke.points.push_back({at[i], float(std::clamp(share, 0.0, 1.0))});
    }
    return stroke;
}

QVector<Stroke> styled(Stroke stroke, LineStyle const& st)
{
    bool const dashed = st.dashOff > 0.0;
    bool const broken = st.ragged && st.pattern >= 3;
    if ((!st.ragged && !dashed) || stroke.points.size() < 2)
        return {stroke};
    if (stroke.closed && stroke.points.first().pos != stroke.points.last().pos)
        stroke.points.push_back(stroke.points.first());
    // The same line always comes out the same: its "random" is seeded from where it is.
    quint32 seed = 2166136261u;
    for (StrokePoint const& p : std::as_const(stroke.points))
        seed = (seed ^ quint32(qint64(p.pos.x() * 16.0) * 73856093 ^ qint64(p.pos.y() * 16.0) * 19349663)) * 16777619u;
    auto random = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return double(seed >> 8) / double(1u << 24);       // 0..1
    };
    double const w = std::max(1.0, double(stroke.width));
    if (st.ragged && st.waveHeight > 0) {
        // Points every so often along it, each pushed off the line to one side or the other.
        static double const lengths[4] = {1.5, 3.0, 6.0, 12.0};
        static double const heights[4] = {0.0, 0.35, 0.75, 1.6};
        double const step = lengths[std::clamp(st.waveLength, 0, 3)] * w;
        double const height = heights[std::clamp(st.waveHeight, 0, 3)] * w;
        QVector<StrokePoint> out;
        out.push_back(stroke.points.first());
        double carried = 0.0;
        for (int i = 1; i < stroke.points.size(); ++i) {
            StrokePoint const a = stroke.points[i - 1], b = stroke.points[i];
            QLineF const seg(a.pos, b.pos);
            double const len = seg.length();
            if (len < 1e-9)
                continue;
            QPointF const across(-(b.pos.y() - a.pos.y()) / len, (b.pos.x() - a.pos.x()) / len);
            double at = step - carried;
            for (; at < len; at += step) {
                double const t = at / len;
                StrokePoint p;
                p.pos = seg.pointAt(t) + across * ((random() * 2.0 - 1.0) * height);
                p.pressure = a.pressure + (b.pressure - a.pressure) * float(t);
                out.push_back(p);
            }
            carried = len - (at - step);
            out.push_back(b);
        }
        stroke.points = out;
    }
    if (!dashed && !broken)
        return {stroke};
    // Broken into pieces: so long drawn, so long left out, along it.
    auto onOff = [&](int k, double& on, double& off) {
        if (dashed) {
            on = st.dashOn;
            off = st.dashOff;
            return;
        }
        // Ragged dots: short pieces, regularly or at random, or in threes.
        bool const randomly = st.pattern == 4 || st.pattern == 6;
        on = 0.5 * w * (randomly ? 0.5 + random() : 1.0);
        off = (st.pattern >= 5 ? (k % 3 == 2 ? 3.0 : 0.6) : 1.6) * w * (randomly ? 0.5 + random() : 1.0);
    };
    QVector<Stroke> pieces;
    Stroke piece = stroke;
    piece.points.clear();
    piece.closed = false;
    int k = 0;
    double on = 0.0, off = 0.0;
    onOff(k, on, off);
    bool drawing = true;
    double left = on;
    auto finish = [&] {
        if (piece.points.size() == 1) {
            // (A dot: a point and a hair beyond it.)
            StrokePoint p = piece.points.first();
            p.pos += QPointF(0.01, 0.0);
            piece.points.push_back(p);
        }
        if (piece.points.size() >= 2)
            pieces.push_back(piece);
        piece.points.clear();
    };
    piece.points.push_back(stroke.points.first());
    for (int i = 1; i < stroke.points.size(); ++i) {
        StrokePoint a = stroke.points[i - 1];
        StrokePoint const b = stroke.points[i];
        double len = QLineF(a.pos, b.pos).length();
        while (len > left) {
            double const t = left / len;
            StrokePoint cut;
            cut.pos = a.pos + (b.pos - a.pos) * t;
            cut.pressure = a.pressure + (b.pressure - a.pressure) * float(t);
            if (drawing) {
                piece.points.push_back(cut);
                finish();
                left = off;
            } else {
                piece.points.push_back(cut);
                ++k;
                onOff(k, on, off);
                left = on;
            }
            drawing = !drawing;
            a = cut;
            len = QLineF(a.pos, b.pos).length();
        }
        left -= len;
        if (drawing)
            piece.points.push_back(b);
    }
    if (drawing)
        finish();
    return pieces;
}

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
            && out.lines.last().style.ragged == st.ragged && out.lines.last().style.dashOn == st.dashOn
            && key(out.lines.last().points.last()) == key(p.points.first())) {
            out.lines.last().points += p.points.mid(1);
            continue;
        }
        out.lines.push_back({st, p.points});
    }
    return out;
}

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

}  // namespace fla
