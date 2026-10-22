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
#include "formats/flash/SwfReader.h"

using namespace swf;

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

bool read(QByteArray const& bytes, Movie& out, QString* error, QString const& mediaFolder, Progress const& progress, Layout layout)
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
    out = Movie();
    out.size = QSize(std::max(1, int(std::lround((xmax - xmin) / 20.0))), std::max(1, int(std::lround((ymax - ymin) / 20.0))));
    out.fps = std::max(1.0, u16(data, p) / 256.0);
    p += 4;

    swf::Reader reader(data, out, mediaFolder, progress);
    QTransform const toPixels = QTransform::fromTranslate(-xmin, -ymin) * QTransform::fromScale(1.0 / 20.0, 1.0 / 20.0);
    if (!reader.readTags(p) || !reader.decodeVideo()
        || !(layout == Layout::Tracks ? reader.drawTracks(toPixels) : reader.drawFrames(toPixels)))
        return fail(reader.why);
    bool any = false;
    auto drawn = [&](Drawing const& d) { any = any || !d.fills.isEmpty() || !d.strokes.isEmpty() || !d.pictures.isEmpty(); };
    for (Drawing const& d : out.frames)
        drawn(d);
    for (Track const& t : std::as_const(out.tracks))
        for (Drawing const& d : t.drawings)
            drawn(d);
    any = any || !out.sounds.isEmpty();     // (a movie of sound alone is read too)
    if (!any)
        return fail(tr("The movie has nothing on its main timeline that Eclipse can read (only code)."));
    return true;
}

}
