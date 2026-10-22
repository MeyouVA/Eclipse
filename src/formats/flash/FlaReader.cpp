// Eclipse - the Flash document reader: paint, gradients, tints, shapes, text, bitmaps and video.
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

// The gradient, among the document's (the one already there that is the same, or added).
int Reader::gradient(Swatch const& s)
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

Paint Reader::paintOf(Node const& fill)
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
void Reader::paintFill(Fill& fill, Paint const& paint, Tint const& tint, QTransform const& place)
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

// The gradient as a colour effect leaves it (the same one when it changes nothing).
int Reader::tinted(int gradientIndex, Tint const& tint)
{
    bool plain = true;
    for (int k = 0; k < 4; ++k)
        plain = plain && tint.mult[k] == 1.0 && tint.add[k] == 0.0;
    if (plain || gradientIndex < 0 || gradientIndex >= out->gradients.size())
        return gradientIndex;
    Swatch s = out->gradients[gradientIndex];
    for (GradientStop& stop : s.stops)
        stop.color = tint.apply(stop.color);
    s.color = s.stops.first().color;
    return gradient(s);
}

void Reader::shapeInto(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
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
            for (Stroke const& piece : styled(stroke, line.style))
                drawing.strokes.push_back(piece);
    }
}

QFont Reader::fontOf(Node const* attrs)
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

void Reader::text(Node const& element, QTransform const& place, Tint const& tint, Drawing& drawing, int& order)
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
Reader::Bitmap const& Reader::bitmap(QString const& name)
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

Reader::Video const& Reader::video(QString const& name)
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
    QByteArray const data = files ? files->value(QStringLiteral("bin/") + item.attr(QStringLiteral("videoDataHRef"))) : QByteArray();
    QString folder = mediaFolder;
    if (folder.isEmpty() && passing && passing->isValid())
        folder = passing->path();
    QString title = name;
    title.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (QByteArray const movie = FlashVideo::flaMovie(data); !movie.isEmpty()) {
        // H.264, embedded as the MP4 it came as.
        QTemporaryDir work;
        QFile file(work.filePath(QStringLiteral("video.mp4")));
        if (work.isValid() && file.open(QIODevice::WriteOnly) && file.write(movie) == movie.size()) {
            file.close();
            made.files = FlashVideo::decodeFile(file.fileName(), folder, title, false, &why);
        }
    } else if (FlashVideo::flaFrames(data, codec, frames, keys)) {
        made.files = FlashVideo::decode(codec, frames, item.number(QStringLiteral("fps"), out->fps), folder, title, keys, &why);
    }
    if (made.files.isEmpty()) {
        ++out->skippedVideo;
        out->videoProblem = why;
    }
    return *videos.insert(name, made);
}

}  // namespace fla
