// Eclipse - the Flash movie writer: a drawing's areas, lines and pictures as one shape, each over the one before.
#include "formats/flash/SwfExportPrivate.h"

#include <QBuffer>
#include <QObject>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"

namespace swfout {

namespace {

QVector<QVector<QPoint>> twipRings(QVector<QPolygonF> const& outlines, QTransform const& toTwips)
{
    QVector<QVector<QPoint>> rings;
    for (QPolygonF const& outline : outlines) {
        QVector<QPoint> ring;
        for (QPointF const& p : outline) {
            QPointF const q = toTwips.map(p);
            QPoint const t(int(std::lround(q.x())), int(std::lround(q.y())));
            if (ring.isEmpty() || ring.last() != t)
                ring.push_back(t);
        }
        if (ring.size() > 1 && ring.first() == ring.last())
            ring.removeLast();
        if (ring.size() >= 3)
            rings.push_back(ring);
    }
    return rings;
}

}  // namespace

Shapes::Shapes(Scene const& scene, SwfExport::Options const& options, SwfExport::Report& report)
    : m_scene(scene), m_options(options), m_report(report)
{
}

QByteArray Shapes::take()
{
    QByteArray const out = m_defs;
    m_defs.clear();
    return out;
}

void Shapes::leftOut(QString const& what)
{
    if (!m_report.leftOut.contains(what))
        m_report.leftOut.push_back(what);
}

int Shapes::shapeOf(QVector<Paint> const& paints)
{
    if (paints.isEmpty())
        return 0;
    QByteArray const body = shape(0, paints).mid(2);
    auto found = m_shapes.constFind(body);
    if (found != m_shapes.cend())
        return *found;
    int const id = m_nextId++;
    m_shapes.insert(body, id);
    QByteArray withId;
    u16(withId, quint16(id));
    m_defs += tag(83, withId + body);
    ++m_report.shapes;
    return id;
}

int Shapes::bitmapOf(QImage const& image)
{
    auto found = m_bitmaps.constFind(image.cacheKey());
    if (found != m_bitmaps.cend())
        return *found;
    int const id = m_nextId++;
    m_bitmaps.insert(image.cacheKey(), id);
    QByteArray body;
    u16(body, quint16(id));
    if (m_options.jpegQuality >= 100) {
        // Without loss: each pixel opacity, red, green, blue (multiplied by the opacity), packed.
        QImage const argb = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        u8(body, 5);
        u16(body, quint16(argb.width()));
        u16(body, quint16(argb.height()));
        QByteArray pixels;
        pixels.reserve(argb.width() * argb.height() * 4);
        for (int y = 0; y < argb.height(); ++y) {
            QRgb const* const line = reinterpret_cast<QRgb const*>(argb.constScanLine(y));
            for (int x = 0; x < argb.width(); ++x) {
                pixels.append(char(qAlpha(line[x])));
                pixels.append(char(qRed(line[x])));
                pixels.append(char(qGreen(line[x])));
                pixels.append(char(qBlue(line[x])));
            }
        }
        body += qCompress(pixels).mid(4);
        m_defs += tag(36, body);
    } else {
        // A JPEG, and its opacity apart: a byte a pixel, packed.
        QImage const argb = image.convertToFormat(QImage::Format_ARGB32);
        QByteArray jpeg;
        {
            QBuffer buffer(&jpeg);
            buffer.open(QIODevice::WriteOnly);
            argb.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPG", std::clamp(m_options.jpegQuality, 1, 99));
        }
        QByteArray alpha;
        alpha.reserve(argb.width() * argb.height());
        for (int y = 0; y < argb.height(); ++y) {
            QRgb const* const line = reinterpret_cast<QRgb const*>(argb.constScanLine(y));
            for (int x = 0; x < argb.width(); ++x)
                alpha.append(char(qAlpha(line[x])));
        }
        u32(body, quint32(jpeg.size()));
        body += jpeg + qCompress(alpha).mid(4);
        m_defs += tag(35, body);
    }
    ++m_report.bitmaps;
    return id;
}

Style Shapes::styleOf(int swatch, QColor const& own, QPointF from, QPointF to, QTransform const& toTwips)
{
    Style style;
    Swatch const* const s = m_scene.findSwatch(swatch);
    style.colour = s ? s->color : own;
    if (s && s->isTexture()) {
        // A texture: the picture over and over, laid as the swatch lays it.
        double const length = QLineF(from, to).length();
        double const scale = length > 1e-6 ? length / s->texture.width() : 1.0;
        double const angle = length > 1e-6 ? std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / 3.14159265358979323846 : 0.0;
        QTransform place;
        place.translate(from.x(), from.y());
        place.rotate(angle);
        place.scale(scale, scale);
        place.translate(0.0, -s->texture.height() / 2.0);
        style.kind = Style::Bitmap;
        style.repeat = true;
        style.bitmap = bitmapOf(s->texture);
        style.matrix = place * toTwips;     // (Flash's bitmap fills count in the picture's pixels)
    } else if (s && s->isGradient()) {
        // Flash's gradient lies in a square from -16384 to 16384: there to where the swatch lays it.
        style.kind = s->kind == Swatch::Radial ? Style::Radial : Style::Linear;
        for (GradientStop const& stop : s->stops)
            style.stops.push_back({stop.at, stop.color});
        double const length = std::max(1e-6, QLineF(from, to).length());
        QTransform place;
        if (style.kind == Style::Radial) {
            place = QTransform::fromScale(length / 16384.0, length / 16384.0) * QTransform::fromTranslate(from.x(), from.y());
        } else {
            double const angle = std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / 3.14159265358979323846;
            QPointF const middle = (from + to) / 2.0;
            place = QTransform::fromScale(length / 32768.0, length / 32768.0) * QTransform().rotate(angle) * QTransform::fromTranslate(middle.x(), middle.y());
        }
        style.matrix = place * toTwips;
    }
    return style;
}

void Shapes::fill(QVector<Paint>& out, Fill const& f, QTransform const& toTwips)
{
    Paint paint;
    QVector<QPolygonF> outlines = f.outlines;
    if (f.link != 0) {
        // Painted all round where its outlines cross (not odd-even): made into outlines that do not.
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (QPolygonF const& o : f.outlines)
            path.addPolygon(o);
        outlines = path.simplified().toSubpathPolygons();
    }
    paint.rings = twipRings(outlines, toTwips);
    if (paint.rings.isEmpty())
        return;
    if (f.texture != 0)
        leftOut(QObject::tr("pencil textures"));
    QPointF from, to;
    m_scene.gradientLine(f, from, to);
    paint.style = styleOf(f.swatch, f.color, from, to, toTwips);
    if (paint.style.kind == Style::Solid && paint.style.colour.alpha() == 0)
        return;
    out.push_back(paint);
}

void Shapes::stroke(QVector<Paint>& out, Stroke const& s, QTransform const& toTwips, double lineScale)
{
    QPointF from, to;
    m_scene.gradientLine(s, from, to);
    Style const style = styleOf(s.swatch, s.color, from, to, toTwips);
    if (style.kind == Style::Solid && style.colour.alpha() == 0)
        return;
    if (s.texture != 0)
        leftOut(QObject::tr("pencil textures"));
    QVector<StrokePoint> const along = s.path();
    bool const asLine = !m_options.linesAsAreas && s.thickness.isEmpty() && s.uniform() && along.size() >= 2 && style.kind != Style::Bitmap;
    if (!asLine) {
        Paint paint;
        paint.style = style;
        paint.rings = twipRings(s.outlinePolygons(), toTwips);
        if (!paint.rings.isEmpty())
            out.push_back(paint);
        return;
    }
    // A line of Flash's: its middle, as thick as it is, its tips and joins.
    Paint paint;
    paint.line = true;
    paint.style = style;
    for (int i = 0; i < along.size(); ++i) {
        QPointF const p = toTwips.map(along[i].pos);
        if (i == 0)
            paint.path.moveTo(p);
        else
            paint.path.lineTo(p);
    }
    if (s.closed)
        paint.path.lineTo(toTwips.map(along.first().pos));
    double const thickness = double(s.width) * along.first().pressure;
    double const grows = std::sqrt(std::abs(toTwips.determinant()));
    paint.noScale = m_options.fixedLineWidth;
    paint.width = int(std::lround(thickness * (paint.noScale ? 20.0 * lineScale : grows)));
    paint.startCap = s.capStart == Stroke::RoundCap ? 0 : 1;
    paint.endCap = s.capEnd == Stroke::RoundCap ? 0 : 1;
    paint.join = s.join == Stroke::RoundJoin ? 0 : s.join == Stroke::BevelJoin ? 1 : 2;
    paint.mitre = Stroke::kMitreLimit;
    out.push_back(paint);
}

void Shapes::picture(QVector<Paint>& out, Picture const& p, QTransform const& toTwips)
{
    if (p.image.isNull()) {
        leftOut(QObject::tr("pictures whose files are missing"));
        return;
    }
    Paint paint;
    QTransform const place = p.placement * toTwips;
    paint.rings = twipRings(p.clip.isEmpty() ? QVector<QPolygonF>{QPolygonF(QRectF(QPointF(0, 0), QSizeF(p.size)))} : p.clip, place);
    if (paint.rings.isEmpty())
        return;
    paint.style.kind = Style::Bitmap;
    paint.style.bitmap = bitmapOf(p.image);
    // From the picture's pixels (twentieths of them, as Flash counts) to where it is drawn.
    paint.style.matrix = QTransform::fromScale(double(p.size.width()) / std::max(1, p.image.width()), double(p.size.height()) / std::max(1, p.image.height())) * place;
    out.push_back(paint);
}

int Shapes::drawing(Drawing const& d, QTransform const& toTwips, double lineScale)
{
    QVector<Paint> paints;
    // In the order the Camera view paints them: pictures under everything; each sheet from the underlay up.
    bool placed = false;
    for (Picture const& p : d.pictures) {
        if (!p.painted && p.order < 0)
            picture(paints, p, toTwips);
        placed = placed || (!p.painted && p.order >= 0);
    }
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        for (Picture const& p : d.pictures)
            if (p.painted && p.sheet == sheet)
                picture(paints, p, toTwips);
        QVector<Fill> const& fills = d.fillsOf(sheet);
        QVector<Stroke> const& strokes = d.strokesOf(sheet);
        struct Item {
            int order, kind, index;
        };
        QVector<Item> items;
        bool const withPictures = placed && sheet == Drawing::LineSheet;
        bool const sorted = withPictures || Drawing::topOrder(fills, strokes) != Drawing::bottomOrder(fills, strokes);
        for (int i = 0; i < fills.size(); ++i)
            items.push_back({sorted ? fills[i].order : 0, 0, i});
        for (int i = 0; i < strokes.size(); ++i)
            items.push_back({sorted ? strokes[i].order : 0, 1, i});
        if (withPictures)
            for (int i = 0; i < d.pictures.size(); ++i)
                if (!d.pictures[i].painted && d.pictures[i].order >= 0)
                    items.push_back({d.pictures[i].order, -1, i});
        std::stable_sort(items.begin(), items.end(), [](Item const& a, Item const& b) {
            return a.order != b.order ? a.order < b.order : a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
        });
        for (Item const& item : std::as_const(items)) {
            if (item.kind < 0)
                picture(paints, d.pictures[item.index], toTwips);
            else if (item.kind == 0)
                fill(paints, fills[item.index], toTwips);
            else
                stroke(paints, strokes[item.index], toTwips, lineScale);
        }
    }
    return shapeOf(paints);
}

int Shapes::card(QColor const& colour, QRect const& twips)
{
    Paint paint;
    paint.style.colour = colour;
    paint.rings = {{twips.topLeft(), QPoint(twips.right() + 1, twips.top()), QPoint(twips.right() + 1, twips.bottom() + 1), QPoint(twips.left(), twips.bottom() + 1)}};
    return shapeOf({paint});
}

}  // namespace swfout
