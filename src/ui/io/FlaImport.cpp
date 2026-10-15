// Eclipse - the Flash document reader.
#include "ui/io/FlaImport.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QHash>
#include <QPolygonF>
#include <QTransform>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>

#include "model/scene/Scene.h"
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

// A shape as read: its fills as rings, and its lines, in the shape's own pixels.
struct Shape {
    struct Area {
        QColor colour;
        QVector<QPolygonF> rings;
    };
    struct Line {
        QColor colour;
        double weight = 1.0;
        bool keepWidth = false;
        QPolygonF points;
    };
    QVector<Area> areas;
    QVector<Line> lines;
};

Shape readShape(Node const& shape)
{
    Shape out;
    QHash<int, QColor> fills;
    struct StrokeStyle {
        QColor colour;
        double weight = 1.0;
        bool keepWidth = false;
    };
    QHash<int, StrokeStyle> strokes;
    if (Node const* f = shape.child(QStringLiteral("fills")))
        for (Node const& style : f->children)
            fills.insert(int(style.number(QStringLiteral("index"), 0)), colourOf(style));
    if (Node const* s = shape.child(QStringLiteral("strokes")))
        for (Node const& style : s->children) {
            if (style.children.isEmpty())
                continue;
            Node const& kind = style.children.first();
            StrokeStyle st;
            st.weight = kind.number(QStringLiteral("weight"), 1.0);
            st.keepWidth = kind.attr(QStringLiteral("scaleMode")) == QLatin1String("none");
            if (Node const* fill = kind.child(QStringLiteral("fill")))
                st.colour = colourOf(*fill);
            else
                st.colour = Qt::black;
            strokes.insert(int(style.number(QStringLiteral("index"), 0)), st);
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
        area.colour = it.value();
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
        StrokeStyle const& st = strokes[p.stroke];
        if (!out.lines.isEmpty() && out.lines.last().colour == st.colour && out.lines.last().weight == st.weight
            && key(out.lines.last().points.last()) == key(p.points.first())) {
            out.lines.last().points += p.points.mid(1);
            continue;
        }
        out.lines.push_back({st.colour, st.weight, st.keepWidth, p.points});
    }
    return out;
}

class Reader {
public:
    QHash<QString, Node> symbols;           // library symbols, by their names: their timelines
    QHash<Node const*, std::shared_ptr<Shape>> shapes;
    QHash<Node const*, int> lengths;
    FlaImport::Document* out = nullptr;

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
            // A classic motion tween: each placed thing on its way to where it is on the next keyframe.
            Node const* next = k + 1 < frames->children.size() ? &frames->children[k + 1] : nullptr;
            Node const* nextElements = next ? next->child(QStringLiteral("elements")) : nullptr;
            bool const tween = f.attr(QStringLiteral("tweenType")) == QLatin1String("motion") && nextElements && duration > 0;
            double t = tween ? double(frame - index) / double(duration) : 0.0;
            if (tween) {
                double const ease = f.number(QStringLiteral("acceleration"), 0.0) / 100.0;
                t = std::clamp(t + ease * t * (1.0 - t), 0.0, 1.0);
            }
            for (int e = 0; e < elements->children.size(); ++e) {
                Node const& element = elements->children[e];
                QTransform own = matrixOf(element);
                if (tween && e < nextElements->children.size()) {
                    Node const& to = nextElements->children[e];
                    if (to.name == element.name && to.attr(QStringLiteral("libraryItemName")) == element.attr(QStringLiteral("libraryItemName")))
                        own = between(own, matrixOf(to), t);
                }
                draw(element, frame - index, own * place, tint, drawing, order, depth);
            }
            return;
        }
    }

    static QTransform between(QTransform const& a, QTransform const& b, double t)
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
        for (int k = 0; k < 6; ++k) {
            double d = pb[k] - pa[k];
            if (k < 2)
                d = std::remainder(d, 2.0 * 3.14159265358979323846);
            v[k] = pa[k] + d * t;
        }
        return QTransform(v[2] * std::cos(v[0]), v[2] * std::sin(v[0]), -v[3] * std::sin(v[1]), v[3] * std::cos(v[1]), v[4], v[5]);
    }

    void timeline(Node const& t, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
    {
        Node const* layers = t.child(QStringLiteral("layers"));
        if (!layers)
            return;
        // From the bottom layer up (the list goes from the top); guides, folders and masks not drawn.
        for (int l = int(layers->children.size()) - 1; l >= 0; --l) {
            Node const& layerNode = layers->children[l];
            QString const type = layerNode.attr(QStringLiteral("layerType"));
            if (type == QLatin1String("guide") || type == QLatin1String("folder") || type == QLatin1String("mask"))
                continue;
            if (layerNode.attr(QStringLiteral("visible")) == QLatin1String("false"))
                continue;
            layer(layerNode, frame, place, tint, drawing, order, depth);
        }
    }

    void shapeInto(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
    {
        std::shared_ptr<Shape>& cached = shapes[&element];
        if (!cached)
            cached = std::make_shared<Shape>(readShape(element));
        ++order;
        for (Shape::Area const& area : cached->areas) {
            Fill fill;
            fill.color = tint.apply(area.colour);
            fill.order = order;
            for (QPolygonF const& ring : area.rings)
                fill.outlines.push_back(place.map(ring));
            if (fill.color.alpha() > 0)
                drawing.fills.push_back(fill);
        }
        double const grow = std::sqrt(std::abs(place.determinant()));
        for (Shape::Line const& line : cached->lines) {
            Stroke stroke;
            stroke.color = tint.apply(line.colour);
            stroke.order = order;
            stroke.width = float(std::max(0.25, line.weight * (line.keepWidth ? 1.0 : grow)));
            for (QPointF const& p : place.map(line.points))
                stroke.points.push_back({p, 1.0f});
            if (stroke.points.size() >= 2 && stroke.color.alpha() > 0)
                drawing.strokes.push_back(stroke);
        }
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
                outline = QPolygonF(box);
                outline.removeLast();
            }
            ++order;
            if (Node const* fill = element.child(QStringLiteral("fill"))) {
                Fill f;
                f.color = tint.apply(colourOf(*fill));
                f.order = order;
                f.outlines = {place.map(outline)};
                drawing.fills.push_back(f);
            }
            if (Node const* stroke = element.child(QStringLiteral("stroke")); stroke && !stroke->children.isEmpty()) {
                Node const& st = stroke->children.first();
                Stroke s;
                s.color = tint.apply(st.child(QStringLiteral("fill")) ? colourOf(*st.child(QStringLiteral("fill"))) : QColor(Qt::black));
                s.order = order;
                s.closed = true;
                s.width = float(std::max(0.25, st.number(QStringLiteral("weight"), 1.0) * std::sqrt(std::abs(place.determinant()))));
                for (QPointF const& p : place.map(outline))
                    s.points.push_back({p, 1.0f});
                drawing.strokes.push_back(s);
            }
        } else if (kind.contains(QLatin1String("Text"))) {
            ++out->skippedText;
        } else if (kind == QLatin1String("DOMBitmapInstance") || kind == QLatin1String("DOMVideoInstance")) {
            ++out->skippedPictures;
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
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
}

}  // namespace

namespace FlaImport {

bool read(QByteArray const& bytes, Document& out, QString* error)
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
        layer.mask = type == QLatin1String("mask");
        layer.cells.fill(-1, out.frames);
        QHash<QByteArray, int> known;
        Node const* frames = layerNode.child(QStringLiteral("frames"));
        for (int k = 0; frames && k < frames->children.size(); ++k) {
            Node const& f = frames->children[k];
            int const index = int(f.number(QStringLiteral("index"), 0)), duration = int(f.number(QStringLiteral("duration"), 1));
            Node const* elements = f.child(QStringLiteral("elements"));
            if (!elements || elements->children.isEmpty())
                continue;
            for (int frame = index; frame < std::min(index + duration, out.frames); ++frame) {
                Drawing drawing;
                int order = 0;
                reader.layer(layerNode, frame, QTransform(), Tint(), drawing, order, 0);
                if (drawing.fills.isEmpty() && drawing.strokes.isEmpty())
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
        return fail(tr("Nothing Eclipse can read is drawn in this .fla (only text, bitmaps or video)."));
    return true;
}

}
