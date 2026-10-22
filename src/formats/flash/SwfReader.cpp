// Eclipse - the Flash movie (.swf) reader: its tags read - shapes, morphs, fonts, pictures, movie clips, labels, sounds,
// video - and its video decoded.
#include "formats/flash/SwfReader.h"

#include <algorithm>
#include <functional>

#include "formats/flash/FlashVideo.h"

namespace swf {

void Reader::label(int frame, QString const& name)
{
    for (Label const& l : std::as_const(out.labels))
        if (l.frame == frame && l.name == name)
            return;
    out.labels.push_back({frame, name});
}

void Reader::videoFrame(QByteArray const& body)
{
    auto found = streams.find(u16(body, 0));
    int const number = u16(body, 2);
    if (found != streams.end()) {
        if (number >= found->frames.size())
            found->frames.resize(number + 1);
        found->frames[number] = body.mid(4);
    }
}

void Reader::readClip(QByteArray const& body)
{
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
}

bool Reader::readTags(int p)
{
    while (p + 2 <= data.size()) {
        if (!told(qint64(p) * 200 / std::max<qsizetype>(1, data.size()))) {
            why = tr("The import was stopped.");
            return false;
        }
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
            readClip(body);
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
    if (!spriteSounds.isEmpty())
        for (int f = 0; f < mainFrames.size(); ++f)
            clipSounds(mainFrames[f], f, f, 0);
    return true;
}

void Reader::clipSounds(DisplayList const& shown, int age, int mainFrame, int depth)
{
    for (Placed const& placed : shown) {
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
}

bool Reader::decodeVideo()
{
    int done = 0;
    for (auto it = streams.begin(); it != streams.end(); ++it) {
        if (!told(200 + 200 * done++ / std::max<qsizetype>(1, streams.size()))) {
            why = tr("The import was stopped.");
            return false;
        }
        QVector<QByteArray> frames = it->frames;
        while (!frames.isEmpty() && frames.last().isEmpty())
            frames.removeLast();
        if (frames.isEmpty())
            continue;
        QString problem;
        QStringList const files = FlashVideo::decode(it->codec, frames, out.fps, mediaFolder.isEmpty() ? passing.path() : mediaFolder,
                                                     QStringLiteral("video %1").arg(it.key()), {}, &problem);
        if (files.isEmpty()) {
            ++out.skippedVideo;
            out.videoProblem = problem;
        }
        decoded.insert(it.key(), files);
    }
    return true;
}

}  // namespace swf
