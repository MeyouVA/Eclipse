// Eclipse - the Flash document reader: timelines, layers, symbols, tweens and their easing, placing what is built.
#include "formats/flash/FlaReader.h"

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
#include "formats/flash/FlaParts.h"

namespace fla {

Reader::Built const& Reader::symbolFrame(Node const& t, int frame, int depth)
{
    QPair<Node const*, int> const key(&t, frame);
    auto found = symbolFrames.constFind(key);
    if (found != symbolFrames.cend())
        return *found;
    Built made;
    int order = 0;
    QMap<QString, Drawing>* const outer = sides;
    sides = &made.sides;
    timeline(t, frame, QTransform(), Tint(), made.main, order, depth);
    sides = outer;
    return *symbolFrames.insert(key, made);
}

// The blend mode and filters a symbol is drawn with, under a name that says what they are (empty: none).
QString Reader::effectOf(Node const& element)
{
    Lift effect;
    QString const blend = element.attr(QStringLiteral("blendMode"));
    if (!blend.isEmpty() && blend != QLatin1String("normal") && blend != QLatin1String("layer"))
        effect.blend = blend;
    if (Node const* list = element.child(QStringLiteral("filters")))
        for (Node const& f : list->children) {
            if (f.attr(QStringLiteral("isEnabled")) == QLatin1String("false"))
                continue;
            FlaImport::Filter filter;
            filter.kind = f.name;
            filter.blurX = f.number(QStringLiteral("blurX"), 5.0);
            filter.blurY = f.number(QStringLiteral("blurY"), 5.0);
            filter.colour = QColor(f.attr(QStringLiteral("color"), f.name == QLatin1String("GlowFilter") ? QStringLiteral("#FF0000") : QStringLiteral("#000000")));
            filter.colour.setAlphaF(float(f.number(QStringLiteral("alpha"), 1.0)));
            filter.strength = f.number(QStringLiteral("strength"), 1.0);
            filter.distance = f.number(QStringLiteral("distance"), f.name == QLatin1String("DropShadowFilter") ? 5.0 : 0.0);
            filter.angle = f.number(QStringLiteral("angle"), 45.0);
            filter.inner = f.attr(QStringLiteral("inner")) == QLatin1String("true");
            if (filter.kind == QLatin1String("GlowFilter") || filter.kind == QLatin1String("BlurFilter") || filter.kind == QLatin1String("DropShadowFilter"))
                effect.filters.push_back(filter);
        }
    if (effect.blend.isEmpty() && effect.filters.isEmpty())
        return QString();
    QStringList parts;
    if (!effect.blend.isEmpty())
        parts << effect.blend;
    for (FlaImport::Filter const& f : std::as_const(effect.filters))
        parts << QStringLiteral("%1 %2x%3 %4 %5 %6@%7").arg(QString(f.kind).remove(QStringLiteral("Filter")).toLower()).arg(f.blurX).arg(f.blurY)
                     .arg(f.colour.name(QColor::HexArgb)).arg(f.strength).arg(f.distance).arg(f.angle);
    QString const name = parts.join(QStringLiteral(", "));
    effects.insert(name, effect);
    return name;
}

// A symbol's frame put into a drawing, what of it has an effect into the drawings of those effects.
void Reader::placeBuilt(Built const& built, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
{
    placeInto(built.main, place, tint, drawing, order);
    for (auto it = built.sides.cbegin(); it != built.sides.cend(); ++it)
        placeInto(it.value(), place, tint, sides ? (*sides)[it.key()] : drawing, order);
}

// A drawing put into another: placed, coloured by the effect, over what is there.
void Reader::placeInto(Drawing const& inner, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
{
    int const base = order;
    int top = order;
    for (Fill fill : inner.fills) {
        fill.transform(place);
        fill.color = tint.apply(fill.color);
        fill.swatch = tinted(fill.swatch, tint);
        fill.order += base;
        top = std::max(top, fill.order);
        if (fill.color.alpha() > 0 || fill.swatch >= 0)
            drawing.fills.push_back(fill);
    }
    for (Stroke stroke : inner.strokes) {
        stroke.transform(place);
        stroke.color = tint.apply(stroke.color);
        stroke.order += base;
        top = std::max(top, stroke.order);
        if (stroke.color.alpha() > 0)
            drawing.strokes.push_back(stroke);
    }
    for (Picture picture : inner.pictures) {
        picture.transform(place);
        if (picture.order >= 0) {
            picture.order += base;
            top = std::max(top, picture.order);
        }
        drawing.pictures.push_back(picture);
    }
    order = top;
}

// How many frames a timeline runs.
int Reader::length(Node const& timeline)
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

Node const* Reader::timelineOf(QString const& symbol)
{
    auto found = symbols.constFind(symbol);
    if (found == symbols.cend())
        return nullptr;
    Node const* t = found->child(QStringLiteral("timeline"));
    return t ? t->child(QStringLiteral("DOMTimeline")) : nullptr;
}

// What a layer shows on a frame, drawn into `drawing` placed by `place`.
void Reader::layer(Node const& layerNode, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
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
QPointF Reader::pivotOf(Node const& element)
{
    Node const* holder = element.child(QStringLiteral("transformationPoint"));
    Node const* point = holder ? holder->child(QStringLiteral("Point")) : nullptr;
    return point ? QPointF(point->number(QStringLiteral("x"), 0.0), point->number(QStringLiteral("y"), 0.0)) : QPointF();
}

double Reader::named(QString const& name, double t)
{
    double const pi = 3.14159265358979323846;
    auto in = [&](QString const& kind, double x) -> double {
        if (kind == QLatin1String("quad"))
            return x * x;
        if (kind == QLatin1String("cubic"))
            return x * x * x;
        if (kind == QLatin1String("quart"))
            return x * x * x * x;
        if (kind == QLatin1String("quint"))
            return x * x * x * x * x;
        if (kind == QLatin1String("sine"))
            return 1.0 - std::cos(x * pi / 2.0);
        if (kind == QLatin1String("expo") || kind == QLatin1String("exponential"))
            return x <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * (x - 1.0));
        if (kind == QLatin1String("circ"))
            return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x));
        if (kind == QLatin1String("back"))
            return x * x * (2.70158 * x - 1.70158);
        if (kind == QLatin1String("elastic"))
            return x <= 0.0 || x >= 1.0 ? x : -std::pow(2.0, 10.0 * (x - 1.0)) * std::sin((x - 1.075) * 2.0 * pi / 0.3);
        if (kind == QLatin1String("bounce")) {
            double const y = 1.0 - x;
            double out;
            if (y < 1.0 / 2.75)
                out = 7.5625 * y * y;
            else if (y < 2.0 / 2.75)
                out = 7.5625 * (y - 1.5 / 2.75) * (y - 1.5 / 2.75) + 0.75;
            else if (y < 2.5 / 2.75)
                out = 7.5625 * (y - 2.25 / 2.75) * (y - 2.25 / 2.75) + 0.9375;
            else
                out = 7.5625 * (y - 2.625 / 2.75) * (y - 2.625 / 2.75) + 0.984375;
            return 1.0 - out;
        }
        return -1.0;
    };
    for (QString const& way : {QStringLiteral("InOut"), QStringLiteral("In"), QStringLiteral("Out")}) {
        if (!name.endsWith(way))
            continue;
        QString const kind = name.left(name.size() - way.size());
        if (in(kind, 0.5) < 0.0)
            return -1.0;
        if (way == QLatin1String("In"))
            return in(kind, t);
        if (way == QLatin1String("Out"))
            return 1.0 - in(kind, 1.0 - t);
        return t < 0.5 ? in(kind, 2.0 * t) / 2.0 : 1.0 - in(kind, 2.0 - 2.0 * t) / 2.0;
    }
    return -1.0;
}

double Reader::eased(Node const& frame, double t)
{
    QString const method = frame.attr(QStringLiteral("easeMethodName"));
    if (!method.isEmpty() && method != QLatin1String("none") && method != QLatin1String("classicEase")) {
        double const v = named(method, t);
        if (v >= -0.5)
            return v;
    }
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

QTransform Reader::between(QTransform const& a, QTransform const& b, double t, QPointF pivot)
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

void Reader::timeline(Node const& t, int frame, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
{
    Node const* layers = t.child(QStringLiteral("layers"));
    if (!layers)
        return;
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
        if (shape.isEmpty()) {
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                drawing.fillsOf(sheet) += masked.fillsOf(sheet);
                drawing.strokesOf(sheet) += masked.strokesOf(sheet);
            }
            drawing.pictures += masked.pictures;
        } else
            FlashCut::cutInto(masked, FlashCut::areaOf(shape), drawing);
    }
}

void Reader::draw(Node const& element, int offset, QTransform const& place, Tint const& tint, Drawing& drawing, int& order, int depth)
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
        Built const& built = symbolFrame(*t, std::clamp(local, 0, total - 1), depth + 1);
        // (With a blend mode or filters, it goes to the drawing of that effect, when there is one to go to.)
        QString const effect = sides ? effectOf(element) : QString();
        placeBuilt(built, place, tint.then(tintOf(element)), effect.isEmpty() ? drawing : (*sides)[effect], order);
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
            for (Stroke const& piece : styled(s, style))
                drawing.strokes.push_back(piece);
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

}  // namespace fla
