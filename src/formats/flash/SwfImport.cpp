// Eclipse - the Flash movie reader.
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
#include "formats/flash/SwfParts.h"

using namespace swf;

namespace {
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

bool read(QByteArray const& bytes, Movie& out, QString* error, QString const& mediaFolder, Progress const& progress)
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
    out = Movie{out.size, out.fps, {}, {}, {}, {}, 0, 0, QString(), 0};
    // (Of a thousand: the first fifth reading, the next decoding video, the rest drawing the frames.)
    auto told = [&](qint64 done) { return !progress || progress(done, 1000); };

    // ---- 1. What the movie defines, and each timeline as its display list stands on each of its frames.
    struct Stream {
        int codec = 0;
        QSize size;
        QVector<QByteArray> frames;
    };
    QHash<int, Stream> streams;
    QHash<int, Shape> shapes;
    QHash<int, MorphShape> morphs;
    QHash<int, Font> fonts;
    QHash<int, QImage> bitmaps;
    QHash<int, QVector<DisplayList>> sprites;       // movie clips: their frames
    // A movie clip's own sounds: an event sound (by its id) or its streamed soundtrack, and the frame of the clip it starts on.
    struct ClipSound {
        int soundId = -1;
        Sound sound;
        int frame = 0;
    };
    QHash<int, QVector<ClipSound>> spriteSounds;
    QByteArray jpegTables;
    QHash<int, Sound> sounds;           // event sounds, by their ids
    QHash<QString, int> classes;        // ids by the names ActionScript knows them by
    int streamFormat = -1;              // the sound streamed along with the frames: its form, and what has come of it
    Sound stream;
    stream.frame = -1;
    QByteArray streamed;
    QVector<DisplayList> mainFrames;
    DisplayList list;
    auto label = [&](int frame, QString const& name) {
        for (Label const& l : std::as_const(out.labels))
            if (l.frame == frame && l.name == name)
                return;
        out.labels.push_back({frame, name});
    };
    // A video frame (the main timeline's, or a movie clip's).
    auto videoFrame = [&](QByteArray const& body) {
        auto found = streams.find(u16(body, 0));
        int const number = u16(body, 2);
        if (found != streams.end()) {
            if (number >= found->frames.size())
                found->frames.resize(number + 1);
            found->frames[number] = body.mid(4);
        }
    };
    while (p + 2 <= data.size()) {
        if (!told(qint64(p) * 200 / std::max<qsizetype>(1, data.size())))
            return fail(tr("The import was stopped."));
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
        } else if (code == 46 || code == 84) {
            morphs.insert(u16(body, 0), morph(body, code == 46 ? 1 : 2));
        } else if (code == 10 || code == 48 || code == 75) {
            fonts.insert(u16(body, 0), font(body, code == 10 ? 1 : code == 48 ? 2 : 3));
        } else if (code == 11 || code == 33) {
            shapes.insert(u16(body, 0), textShape(body, code == 33, fonts));
        } else if (code == 8) {
            jpegTables = body;
        } else if (code == 6) {
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
        } else if (code == 39) {
            // A movie clip: its own timeline, its tags inside.
            QVector<DisplayList> frames;
            DisplayList own;
            QVector<ClipSound> heard;
            int ownStreamFormat = -1, ownStreamStart = -1;
            QByteArray ownStreamed;
            int q = 4;
            while (q + 2 <= body.size()) {
                quint16 const innerHead = u16(body, q);
                q += 2;
                int const innerCode = innerHead >> 6;
                int innerSize = innerHead & 0x3f;
                if (innerSize == 0x3f) {
                    innerSize = int(u32(body, q));
                    q += 4;
                }
                if (innerSize < 0 || q + innerSize > body.size() || innerCode == 0)
                    break;
                QByteArray const inner = body.mid(q, innerSize);
                q += innerSize;
                if (innerCode == 1)
                    frames.push_back(own);
                else if (innerCode == 61)
                    videoFrame(inner);
                else if (innerCode == 12 || innerCode == 59)
                    ++out.scripts;
                else if (innerCode == 15 || innerCode == 89) {
                    // An event sound started on this frame of the clip.
                    int r = 0;
                    int id = -1;
                    if (innerCode == 15) {
                        id = u16(inner, 0);
                        r = 2;
                    } else {
                        id = classes.value(text(inner, r), -1);
                    }
                    if (!(u8(inner, r) & 0x20))
                        heard.push_back({id, Sound(), int(frames.size())});
                } else if (innerCode == 18 || innerCode == 45) {
                    ownStreamFormat = u8(inner, 1);     // the clip's own soundtrack, streamed with its frames
                } else if (innerCode == 19) {
                    if (ownStreamFormat >= 0) {
                        if (ownStreamStart < 0)
                            ownStreamStart = int(frames.size());
                        ownStreamed += (ownStreamFormat >> 4) == 2 ? inner.mid(4) : inner;
                    }
                } else
                    control(innerCode, inner, own, int(frames.size()));
            }
            if (frames.isEmpty())
                frames.push_back(own);
            if (!ownStreamed.isEmpty()) {
                ClipSound track;
                track.frame = std::max(0, ownStreamStart);
                if (soundFile(ownStreamFormat, ownStreamed, track.sound))
                    heard.push_back(track);
                else
                    ++out.skippedSounds;
            }
            if (!heard.isEmpty())
                spriteSounds.insert(u16(body, 0), heard);
            sprites.insert(u16(body, 0), frames);
        } else if (code == 43) {
            int q = 0;
            label(int(mainFrames.size()), text(body, q));
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
                sound.frame = int(mainFrames.size());
                out.sounds.push_back(sound);
            }
        } else if (code == 18 || code == 45) {
            streamFormat = u8(body, 1);
        } else if (code == 19) {
            if (streamFormat >= 0) {
                if (stream.frame < 0)
                    stream.frame = int(mainFrames.size());
                // (Each block of an MP3 stream starts with a count of samples and a count to skip.)
                streamed += (streamFormat >> 4) == 2 ? body.mid(4) : body;
            }
        } else if (code == 60) {
            // A video stream: so many frames, so big, in a codec.
            Stream video;
            video.frames.resize(u16(body, 2));
            video.size = QSize(u16(body, 4), u16(body, 6));
            video.codec = u8(body, 9);
            streams.insert(u16(body, 0), video);
        } else if (code == 61) {
            videoFrame(body);
        } else if (code == 12 || code == 59 || code == 82) {
            ++out.scripts;
        } else if (code == 1) {
            mainFrames.push_back(list);
        } else {
            control(code, body, list, int(mainFrames.size()));
        }
    }
    if (!streamed.isEmpty()) {
        if (soundFile(streamFormat, streamed, stream))
            out.sounds.push_back(stream);
        else
            ++out.skippedSounds;
    }
    if (!spriteSounds.isEmpty()) {
        std::function<void(DisplayList const&, int, int, int)> clipSounds = [&](DisplayList const& list, int age, int mainFrame, int depth) {
            for (Placed const& placed : list) {
                auto const sprite = sprites.constFind(placed.character);
                if (sprite == sprites.cend() || sprite->isEmpty() || depth > 24)
                    continue;
                int const length = int(sprite->size());
                int const own = age - placed.since;     // how long it has been there
                if (own < 0)
                    continue;
                int const local = own % length;
                if (length > 1 || own == 0)
                    for (ClipSound const& cs : spriteSounds.value(placed.character)) {
                        if (cs.frame != local)
                            continue;
                        Sound sound = cs.soundId >= 0 ? sounds.value(cs.soundId) : cs.sound;
                        if (sound.bytes.isEmpty())
                            continue;
                        sound.frame = mainFrame;
                        out.sounds.push_back(sound);
                    }
                clipSounds((*sprite)[local], length > 1 ? local : own, mainFrame, depth + 1);
            }
        };
        for (int f = 0; f < mainFrames.size(); ++f)
            clipSounds(mainFrames[f], f, f, 0);
    }

    // ---- 2. The video, each stream decoded (into the media folder, or in passing).
    QTemporaryDir passing;
    QHash<int, QStringList> decoded;
    {
        int done = 0;
        for (auto it = streams.begin(); it != streams.end(); ++it) {
            if (!told(200 + 200 * done++ / std::max<qsizetype>(1, streams.size())))
                return fail(tr("The import was stopped."));
            QVector<QByteArray> frames = it->frames;
            while (!frames.isEmpty() && frames.last().isEmpty())
                frames.removeLast();
            if (frames.isEmpty())
                continue;
            QString why;
            QStringList const files = FlashVideo::decode(it->codec, frames, out.fps, mediaFolder.isEmpty() ? passing.path() : mediaFolder,
                                                         QStringLiteral("video %1").arg(it.key()), {}, &why);
            if (files.isEmpty()) {
                ++out.skippedVideo;
                out.videoProblem = why;
            }
            decoded.insert(it.key(), files);
        }
    }

    // ---- 3. Each frame drawn: each timeline from its deepest up, each thing on top of the one before; what lies under a
    // mask cut to it, the mask itself not seen.  A movie clip's frames are drawn once each, in its own coordinates, and
    // placed where they are used.
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
    QHash<QString, QImage> videoImages;
    QHash<QByteArray, QImage> tintedImages;
    auto tintedImage = [&](QImage const& image, ColourFx const& fx) {
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
    };
    // A shape drawn into `d`, placed by `place`, coloured by `fx`, at `order` among what is drawn.
    auto shapeInto = [&](Shape const& s, QTransform const& place, ColourFx const& fx, Drawing& d, int order) {
        for (Shape::Area const& area : s.areas) {
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
    };
    // A drawing put into another: placed, coloured, over what is there.
    auto placeInto = [&](Drawing const& inner, QTransform const& place, ColourFx const& fx, Drawing& d, int& order) {
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
    };
    QHash<QPair<int, int>, Drawing> spriteFrames;
    QHash<QPair<int, int>, Shape> morphShapes;
    std::function<void(DisplayList const&, int, Drawing&, int&, int)> drawList;
    // What a character looks like where it is placed (in the coordinates of the timeline it is on), on frame `frame` of it.
    std::function<void(Placed const&, int, Drawing&, int&, int)> drawPlaced = [&](Placed const& placed, int frame, Drawing& d, int& order, int depth) {
        ColourFx const fx = ColourFx::of(placed);
        if (auto const found = shapes.constFind(placed.character); found != shapes.cend()) {
            shapeInto(*found, placed.matrix, fx, d, ++order);
        } else if (auto const m = morphs.constFind(placed.character); m != morphs.cend()) {
            QPair<int, int> const key(placed.character, placed.ratio);
            auto made = morphShapes.constFind(key);
            if (made == morphShapes.cend())
                made = morphShapes.insert(key, morphAt(*m, placed.ratio / 65535.0));
            shapeInto(*made, placed.matrix, fx, d, ++order);
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
    };
    drawList = [&](DisplayList const& shown, int frame, Drawing& d, int& order, int depth) {
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
    };
    QTransform const toPixels = QTransform::fromTranslate(-xmin, -ymin) * QTransform::fromScale(1.0 / 20.0, 1.0 / 20.0);
    for (int f = 0; f < mainFrames.size(); ++f) {
        if (!told(400 + qint64(600) * f / std::max<qsizetype>(1, mainFrames.size())))
            return fail(tr("The import was stopped."));
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
    bool any = false;
    for (Drawing const& d : out.frames)
        any = any || !d.fills.isEmpty() || !d.strokes.isEmpty() || !d.pictures.isEmpty();
    any = any || !out.sounds.isEmpty();     // (a movie of sound alone is read too)
    if (!any)
        return fail(tr("The movie has nothing on its main timeline that Eclipse can read (only code)."));
    return true;
}

}
