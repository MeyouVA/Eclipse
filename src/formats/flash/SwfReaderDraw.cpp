// Eclipse - the Flash movie (.swf) reader: each frame drawn - each timeline from its deepest up, each thing on top of the one
// before; what lies under a mask cut to it, the mask itself not seen; a movie clip's frames drawn once each, in its own
// coordinates, and placed where they are used.
#include "formats/flash/SwfReader.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "formats/flash/FlashCut.h"

namespace swf {

int Reader::gradientOf(Swatch const& s)
{
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
}

QImage Reader::tintedImage(QImage const& image, ColourFx const& fx)
{
    if (fx.plain())
        return image;
    QByteArray const key = QByteArray::number(image.cacheKey()) + '/' + fx.key();
    auto found = tintedImages.constFind(key);
    if (found != tintedImages.cend())
        return *found;
    QImage made = image.convertToFormat(QImage::Format_ARGB32);
    // (Each part through a table: the same sum for every pixel, worked out once for each of the 256 values.)
    quint8 table[4][256];
    for (int k = 0; k < 4; ++k)
        for (int v = 0; v < 256; ++v)
            table[k][v] = quint8(std::clamp(v * fx.mult[k] / 256 + fx.add[k], 0, 255));
    for (int y = 0; y < made.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(made.scanLine(y));
        for (int x = 0; x < made.width(); ++x) {
            QRgb const c = line[x];
            line[x] = qRgba(table[0][qRed(c)], table[1][qGreen(c)], table[2][qBlue(c)], table[3][qAlpha(c)]);
        }
    }
    made = made.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    tintedImages.insert(key, made);
    return made;
}

void Reader::shapeInto(Shape const& s, QTransform const& place, ColourFx const& fx, Drawing& d, int& next)
{
    // Each set of styles of the shape over the one before (a set's lines over its areas, as the same place in the order).
    int const base = ++next;
    int order = base;
    for (Shape::Area const& area : s.areas) {
        order = base + area.group;
        next = std::max(next, order);
        if (area.style.bitmap >= 0) {
            // Filled with a bitmap: the picture where the fill's matrix puts it, only where the shape is.
            QImage const image = bitmaps.value(area.style.bitmap);
            if (!image.isNull()) {
                Picture picture;
                picture.image = tintedImage(image, fx);
                picture.size = image.size();
                picture.placement = area.style.matrix * place;
                picture.order = order;
                QTransform const back = area.style.matrix.inverted();
                for (QPolygonF const& ring : area.rings)
                    picture.clip.push_back(back.map(ring));
                d.pictures.push_back(picture);
                continue;
            }
        }
        Fill fill;
        fill.color = fx.apply(area.style.colour);
        fill.order = order;
        for (QPolygonF const& ring : area.rings)
            fill.outlines.push_back(place.map(ring));
        if (area.style.gradient.isGradient()) {
            Swatch g = area.style.gradient;
            for (GradientStop& stop : g.stops)
                stop.color = fx.apply(stop.color);
            g.color = g.stops.first().color;
            QTransform const at = area.style.matrix * place;
            fill.swatch = gradientOf(g);
            fill.gradPlaced = true;
            fill.gradFrom = at.map(g.kind == Swatch::Linear ? QPointF(-16384.0, 0.0) : QPointF(0.0, 0.0));
            fill.gradTo = at.map(QPointF(16384.0, 0.0));
        }
        d.fills.push_back(fill);
    }
    for (Shape::Line const& line : s.lines) {
        order = base + line.group;
        next = std::max(next, order);
        Stroke stroke;
        stroke.color = fx.apply(line.colour);
        stroke.order = order;
        stroke.width = float(line.width * std::sqrt(std::abs(place.determinant())));
        if (line.keepWidth) {
            stroke.width = float(line.width / 20.0);
            stroke.texture = kKeepWidthMark;
        }
        stroke.capStart = line.startCap == 0 ? Stroke::RoundCap : Stroke::FlatCap;
        stroke.capEnd = line.endCap == 0 ? Stroke::RoundCap : Stroke::FlatCap;
        stroke.join = line.join == 1 ? Stroke::BevelJoin : line.join == 2 ? Stroke::MitreJoin : Stroke::RoundJoin;
        QPolygonF points = place.map(line.points);
        // A square cap: flat, half the thickness beyond the end.
        auto reach = [&](int end, int towards) {
            QPointF const dd = points[end] - points[towards];
            double const length = std::hypot(dd.x(), dd.y());
            if (length > 1e-9)
                points[end] += dd * (stroke.width / 2.0 / length);
        };
        if (points.size() >= 2 && points.first() != points.last()) {
            if (line.startCap == 2)
                reach(0, 1);
            if (line.endCap == 2)
                reach(int(points.size()) - 1, int(points.size()) - 2);
        }
        for (QPointF const& point : std::as_const(points))
            stroke.points.push_back({point, 1.0f});
        if (stroke.points.size() >= 2)
            d.strokes.push_back(stroke);
    }
}

void Reader::placeInto(Drawing const& inner, QTransform const& place, ColourFx const& fx, Drawing& d, int& order)
{
    int const base = order;
    int top = order;
    for (Fill fill : inner.fills) {
        fill.transform(place);
        fill.color = fx.apply(fill.color);
        if (fill.swatch >= 0 && !fx.plain() && fill.swatch < out.gradients.size()) {
            Swatch g = out.gradients[fill.swatch];
            for (GradientStop& stop : g.stops)
                stop.color = fx.apply(stop.color);
            g.color = g.stops.first().color;
            fill.swatch = gradientOf(g);
        }
        fill.order += base;
        top = std::max(top, fill.order);
        d.fills.push_back(fill);
    }
    for (Stroke stroke : inner.strokes) {
        float const width = stroke.width;
        stroke.transform(place);
        if (stroke.texture == kKeepWidthMark)
            stroke.width = width;
        stroke.color = fx.apply(stroke.color);
        stroke.order += base;
        top = std::max(top, stroke.order);
        d.strokes.push_back(stroke);
    }
    for (Picture picture : inner.pictures) {
        picture.transform(place);
        picture.image = tintedImage(picture.image, fx);
        if (picture.order >= 0) {
            picture.order += base;
            top = std::max(top, picture.order);
        }
        d.pictures.push_back(picture);
    }
    order = top;
}

void Reader::drawPlaced(Placed const& placed, int frame, Drawing& d, int& order, int depth)
{
    ColourFx const fx = ColourFx::of(placed);
    if (auto const found = shapes.constFind(placed.character); found != shapes.cend()) {
        shapeInto(*found, placed.matrix, fx, d, order);
    } else if (auto const m = morphs.constFind(placed.character); m != morphs.cend()) {
        QPair<int, int> const key(placed.character, placed.ratio);
        auto made = morphShapes.constFind(key);
        if (made == morphShapes.cend())
            made = morphShapes.insert(key, morphAt(*m, placed.ratio / 65535.0));
        shapeInto(*made, placed.matrix, fx, d, order);
    } else if (auto const sprite = sprites.constFind(placed.character); sprite != sprites.cend()) {
        if (depth > 24 || sprite->isEmpty())
            return;
        // A movie clip plays on from when it was put there, round and round.
        int const local = ((frame - placed.since) % int(sprite->size()) + int(sprite->size())) % int(sprite->size());
        QPair<int, int> const key(placed.character, local);
        auto inner = spriteFrames.constFind(key);
        if (inner == spriteFrames.cend()) {
            Drawing made;
            int innerOrder = 0;
            drawList((*sprite)[local], local, made, innerOrder, depth + 1);
            inner = spriteFrames.insert(key, made);
        }
        placeInto(*inner, placed.matrix, fx, d, order);
    } else if (auto const video = streams.constFind(placed.character); video != streams.cend()) {
        // A frame of a video: as big as the stream says (in pixels), where it is placed.
        QStringList const files = decoded.value(placed.character);
        if (files.isEmpty())
            return;
        QString const file = files[std::clamp(placed.ratio, 0, int(files.size()) - 1)];
        auto image = videoImages.find(file);
        if (image == videoImages.end())
            image = videoImages.insert(file, QImage(file).convertToFormat(QImage::Format_ARGB32_Premultiplied));
        if (image->isNull())
            return;
        Picture picture;
        picture.image = tintedImage(*image, fx);
        picture.size = image->size();
        picture.order = ++order;
        picture.placement = QTransform::fromScale(20.0 * video->size.width() / image->width(), 20.0 * video->size.height() / image->height()) * placed.matrix;
        if (!mediaFolder.isEmpty() && fx.plain()) {
            picture.file = file;
            picture.linked = true;
        }
        d.pictures.push_back(picture);
    }
}

void Reader::drawList(DisplayList const& shown, int frame, Drawing& d, int& order, int depth)
{
    struct Mask {
        int upTo;
        std::shared_ptr<FlashCut::Cutter> cutter;
    };
    QVector<Mask> masks;
    for (auto it = shown.cbegin(); it != shown.cend(); ++it) {
        masks.removeIf([&](Mask const& m) { return it.key() > m.upTo; });
        if (it->clipDepth > 0) {
            Drawing shape;
            int maskOrder = 0;
            drawPlaced(*it, frame, shape, maskOrder, depth);
            masks.push_back({it->clipDepth, std::make_shared<FlashCut::Cutter>(FlashCut::areaOf(shape))});
            continue;
        }
        if (masks.isEmpty()) {
            drawPlaced(*it, frame, d, order, depth);
            continue;
        }
        // Under masks: drawn apart, then cut to each of them in turn.
        Drawing piece;
        drawPlaced(*it, frame, piece, order, depth);
        for (Mask const& m : std::as_const(masks)) {
            Drawing cut;
            m.cutter->cut(piece, cut);
            piece = cut;
        }
        d.fills += piece.fills;
        d.strokes += piece.strokes;
        d.pictures += piece.pictures;
    }
}

bool Reader::drawFrames(QTransform const& toPixels)
{
    for (int f = 0; f < mainFrames.size(); ++f) {
        if (!told(400 + qint64(600) * f / std::max<qsizetype>(1, mainFrames.size()))) {
            why = tr("The import was stopped.");
            return false;
        }
        Drawing frame;
        int order = 0;
        drawList(mainFrames[f], f, frame, order, 0);
        Drawing placed;
        int placedOrder = 0;
        placeInto(frame, toPixels, ColourFx(), placed, placedOrder);
        for (Stroke& s : placed.strokes) {
            s.width = std::max(0.5f, s.width);
            if (s.texture == kKeepWidthMark)
                s.texture = 0;
        }
        out.frames.push_back(placed);
    }
    return true;
}

}  // namespace swf
