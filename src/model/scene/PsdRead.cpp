// Eclipse - reading Photoshop files (PSD or PSB): the flattened picture, or the layers one by one.
#include "model/scene/PsdRead.h"

#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {

QString tr(char const* text) { return QCoreApplication::translate("Psd", text); }

struct Bytes {
    QByteArray data;
    qint64 at = 0;
    bool ok = true;

    bool has(qint64 n) const { return ok && n >= 0 && at + n <= data.size(); }
    quint64 number(int size)
    {
        if (!has(size)) {
            ok = false;
            return 0;
        }
        quint64 v = 0;
        for (int i = 0; i < size; ++i)
            v = (v << 8) | quint8(data[at + i]);
        at += size;
        return v;
    }
    qint32 signed32() { return qint32(quint32(number(4))); }
    QByteArray take(qint64 n)
    {
        if (!has(n)) {
            ok = false;
            return {};
        }
        QByteArray const part = data.mid(at, n);
        at += n;
        return part;
    }
    void skip(quint64 n)
    {
        if (!has(qint64(n)))
            ok = false;
        else
            at += qint64(n);
    }
};

// The header: what every part of the file is read with.
struct Header {
    bool big = false;           // PSB: some lengths take eight bytes
    int channels = 0, width = 0, height = 0, depth = 8, mode = 3;
};

QString modeName(int mode)
{
    switch (mode) {
    case 2: return tr("Indexed");
    case 4: return tr("CMYK");
    case 8: return tr("Duotone");
    case 9: return tr("Lab");
    default: return QString::number(mode);
    }
}

bool readHeader(Bytes& b, Header& h, QString* why)
{
    if (b.data.left(4) != "8BPS") {
        *why = tr("This is not a Photoshop file.");
        return false;
    }
    b.at = 4;
    int const version = int(b.number(2));
    if (version != 1 && version != 2) {
        *why = tr("Unknown Photoshop file version (%1).").arg(version);
        return false;
    }
    h.big = version == 2;
    b.skip(6);
    h.channels = int(b.number(2));
    h.height = int(b.number(4));
    h.width = int(b.number(4));
    h.depth = int(b.number(2));
    h.mode = int(b.number(2));
    if (!b.ok || h.channels < 1 || h.width < 1 || h.height < 1 || qint64(h.width) * h.height > 400'000'000LL) {
        *why = tr("The file is damaged.");
        return false;
    }
    if (h.mode != 0 && h.mode != 1 && h.mode != 3 && h.mode != 7) {
        *why = tr("Unsupported PSD image mode (%1).\nSupported modes are RGB, Grayscale, Bitmap (Black and White) and Multichannel.").arg(modeName(h.mode));
        return false;
    }
    if (h.depth != 1 && h.depth != 8 && h.depth != 16 && h.depth != 32) {
        *why = tr("Unsupported PSD bit depth (%1).").arg(h.depth);
        return false;
    }
    return true;
}

qint64 rowBytesOf(int width, int depth) { return depth == 1 ? (qint64(width) + 7) / 8 : qint64(width) * (depth / 8); }

// PackBits: a count n then n+1 bytes as they are, or a count -n then one byte n+1 times.
bool unpack(char const* from, qint64 length, char* to, qint64 wanted)
{
    qint64 in = 0, out = 0;
    while (in < length && out < wanted) {
        int const n = qint8(from[in++]);
        if (n >= 0) {
            qint64 const run = std::min<qint64>(n + 1, wanted - out);
            if (in + run > length)
                return false;
            std::memcpy(to + out, from + in, size_t(run));
            in += n + 1;
            out += run;
        } else if (n != -128) {
            if (in >= length)
                return false;
            qint64 const run = std::min<qint64>(1 - n, wanted - out);
            std::memset(to + out, from[in++], size_t(run));
            out += run;
        }
    }
    return out == wanted;
}

// ZIP: a zlib stream; with prediction each row is kept as the change from the sample before (32-bit: by byte, its bytes laid out by place).
bool unzip(QByteArray const& packed, QByteArray& plane, qint64 wanted, int width, int depth, bool predicted)
{
    QByteArray sized(4, '\0');
    for (int i = 0; i < 4; ++i)
        sized[i] = char((quint64(wanted) >> (8 * (3 - i))) & 0xff);
    plane = qUncompress(sized + packed);
    if (plane.size() < wanted)
        return false;
    plane.truncate(wanted);
    if (!predicted)
        return true;
    qint64 const rowBytes = rowBytesOf(width, depth);
    int const rows = rowBytes > 0 ? int(wanted / rowBytes) : 0;
    for (int y = 0; y < rows; ++y) {
        char* const row = plane.data() + rowBytes * y;
        if (depth == 8) {
            for (int x = 1; x < width; ++x)
                row[x] = char(quint8(row[x]) + quint8(row[x - 1]));
        } else if (depth == 16) {
            quint16 before = quint16((quint8(row[0]) << 8) | quint8(row[1]));
            for (int x = 1; x < width; ++x) {
                quint16 const v = quint16(before + ((quint8(row[x * 2]) << 8) | quint8(row[x * 2 + 1])));
                row[x * 2] = char(v >> 8);
                row[x * 2 + 1] = char(v & 0xff);
                before = v;
            }
        } else if (depth == 32) {
            for (qint64 i = 1; i < rowBytes; ++i)
                row[i] = char(quint8(row[i]) + quint8(row[i - 1]));
            QByteArray const laid(row, rowBytes);
            for (int x = 0; x < width; ++x)
                for (int k = 0; k < 4; ++k)
                    row[x * 4 + k] = laid[qsizetype(k) * width + x];
        }
    }
    return true;
}

// One channel of a layer: its compression, then its rows.
bool readChannel(Bytes& b, qint64 length, int width, int height, Header const& h, QByteArray& plane)
{
    qint64 const end = b.at + length;
    if (!b.has(length) || length < 2)
        return false;
    int const compression = int(b.number(2));
    qint64 const rowBytes = rowBytesOf(width, h.depth);
    qint64 const wanted = rowBytes * height;
    bool fine = true;
    if (wanted <= 0) {
        plane.clear();
    } else if (compression == 0) {
        plane = b.data.mid(b.at, wanted);
        fine = plane.size() == wanted;
    } else if (compression == 1) {
        plane = QByteArray(wanted, Qt::Uninitialized);
        int const countSize = h.big ? 4 : 2;
        qint64 data = b.at + qint64(height) * countSize;
        for (int y = 0; y < height && fine; ++y) {
            qint64 const n = qint64(b.number(countSize));
            fine = b.ok && data + n <= end && unpack(b.data.constData() + data, n, plane.data() + rowBytes * y, rowBytes);
            data += n;
        }
    } else if (compression == 2 || compression == 3) {
        fine = unzip(b.data.mid(b.at, end - b.at), plane, wanted, width, h.depth, compression == 3);
    } else {
        fine = false;
    }
    b.at = end;
    return fine;
}

// A channel's value at a pixel, 0..255 (32-bit colour, which is linear, made sRGB).
int valueAt(QByteArray const& plane, qint64 rowBytes, int depth, int x, int y, bool colour)
{
    char const* const row = plane.constData() + rowBytes * y;
    switch (depth) {
    case 1: return (quint8(row[x / 8]) >> (7 - x % 8)) & 1 ? 0 : 255;   // 1 is black
    case 8: return quint8(row[x]);
    case 16: return int(((quint8(row[x * 2]) << 8) | quint8(row[x * 2 + 1])) * 255 / 65535);
    default: {
        quint32 bits = 0;
        for (int i = 0; i < 4; ++i)
            bits = (bits << 8) | quint8(row[x * 4 + i]);
        float v;
        std::memcpy(&v, &bits, 4);
        double c = std::clamp(double(v), 0.0, 1.0);
        if (colour)
            c = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        return int(c * 255.0 + 0.5);
    }
    }
}

// The pixels of planes: grey or red, green and blue, and how see-through (none: solid).
QImage composed(Header const& h, int width, int height, QByteArray const* grey, QByteArray const* rgb[3], QByteArray const* alpha)
{
    if (width <= 0 || height <= 0)
        return {};
    qint64 const rowBytes = rowBytesOf(width, h.depth);
    QImage image(width, height, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int r, g, b;
            if (rgb[0] && rgb[1] && rgb[2]) {
                r = valueAt(*rgb[0], rowBytes, h.depth, x, y, true);
                g = valueAt(*rgb[1], rowBytes, h.depth, x, y, true);
                b = valueAt(*rgb[2], rowBytes, h.depth, x, y, true);
            } else {
                r = g = b = grey ? valueAt(*grey, rowBytes, h.depth, x, y, true) : 0;
            }
            out[x] = qRgba(r, g, b, alpha ? valueAt(*alpha, rowBytes, h.depth, x, y, false) : 255);
        }
    }
    return image;
}

// A vector mask: path records in fractions of the picture (8.24 fixed point), turned into the shape it lets show.
QPainterPath vectorMask(QByteArray const& body, QSize size, bool* inverted)
{
    Bytes b{body};
    b.number(4);                        // version
    quint32 const flags = quint32(b.number(4));
    *inverted = flags & 1;
    if (flags & 4)
        return {};                      // turned off
    auto point = [&b, size]() {
        double const y = double(b.signed32()) / (1 << 24) * size.height();
        double const x = double(b.signed32()) / (1 << 24) * size.width();
        return QPointF(x, y);
    };
    QPainterPath shape, sub;
    bool startFull = false, closed = false, have = false;
    int operation = 1;
    QPointF firstIn, firstAnchor, lastOut;
    auto finish = [&] {
        if (!have)
            return;
        if (closed) {
            sub.cubicTo(lastOut, firstIn, firstAnchor);
            sub.closeSubpath();
        }
        sub.setFillRule(Qt::WindingFill);
        switch (operation) {
        case 2: shape = shape.subtracted(sub); break;
        case 3: shape = shape.intersected(sub); break;
        case 0: shape = shape.united(sub).subtracted(shape.intersected(sub)); break;
        default: shape = shape.united(sub); break;
        }
        sub = QPainterPath();
        have = false;
    };
    while (b.has(26)) {
        qint64 const start = b.at;
        int const type = int(b.number(2));
        if (type == 0 || type == 3) {
            finish();
            closed = type == 0;
            b.number(2);                // knot count
            int const op = int(qint16(quint16(b.number(2))));
            operation = op >= 0 && op <= 3 ? op : 1;
        } else if (type == 1 || type == 2 || type == 4 || type == 5) {
            QPointF const in = point(), anchor = point(), out = point();
            if (!have) {
                sub.moveTo(anchor);
                firstIn = in;
                firstAnchor = anchor;
                have = true;
            } else {
                sub.cubicTo(lastOut, in, anchor);
            }
            lastOut = out;
        } else if (type == 8) {
            startFull = b.number(2) == 1;
        }
        b.at = start + 26;
    }
    finish();
    if (startFull) {
        QPainterPath all;
        all.addRect(QRectF(QPointF(0, 0), QSizeF(size)));
        shape = all.subtracted(shape);
    }
    return shape;
}

// The alpha of `image` (at `at` in the picture) multiplied by a mask's values.
void masked(QImage& image, QPoint at, std::function<int(int, int)> const& maskAt)
{
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const m = maskAt(at.x() + x, at.y() + y);
            if (m < 255)
                row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), qAlpha(row[x]) * m / 255);
        }
    }
}

// The records of the layers and their pixels, from the bottom up.
bool readLayerInfo(Bytes& b, qint64 end, Header const& h, Psd::Layered& out, QString* why)
{
    int const count = std::abs(int(qint16(quint16(b.number(2)))));
    struct Channel {
        int id;
        qint64 length;
    };
    struct Record {
        Psd::Layer layer;
        QRect box;
        QVector<Channel> channels;
        QRect maskBox;
        int maskDefault = 255;
        bool maskOff = false;
        QPainterPath vector;
        bool vectorInverted = false;
        bool hasVector = false;
    };
    QVector<Record> records(count);
    int const lengthSize = h.big ? 8 : 4;
    for (Record& r : records) {
        int const top = b.signed32(), left = b.signed32(), bottom = b.signed32(), right = b.signed32();
        r.box = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
        int const channels = int(b.number(2));
        for (int c = 0; c < channels && b.ok; ++c)
            r.channels.push_back({int(qint16(quint16(b.number(2)))), qint64(b.number(lengthSize))});
        b.skip(4);                      // "8BIM"
        r.layer.blend = b.take(4);
        int const opacity = int(b.number(1));
        r.layer.clipped = b.number(1) != 0;
        int const flags = int(b.number(1));
        b.skip(1);
        r.layer.opacity = opacity / 255.0;
        r.layer.visible = !(flags & 2);
        qint64 const extra = qint64(b.number(4));
        qint64 const extraEnd = b.at + extra;
        // The layer mask: its box, the value outside it, whether it is turned off.
        qint64 const maskLength = qint64(b.number(4));
        qint64 const maskEnd = b.at + maskLength;
        if (maskLength >= 18) {
            int const mt = b.signed32(), ml = b.signed32(), mb = b.signed32(), mr = b.signed32();
            r.maskBox = QRect(QPoint(ml, mt), QPoint(mr - 1, mb - 1));
            r.maskDefault = int(b.number(1));
            r.maskOff = b.number(1) & 2;
        }
        b.at = maskEnd;
        b.skip(b.number(4));            // blending ranges
        int const nameLength = int(b.number(1));
        r.layer.name = QString::fromLatin1(b.take(nameLength));
        b.skip(quint64((4 - (1 + nameLength) % 4) % 4));
        while (b.ok && b.at + 12 <= extraEnd) {
            QByteArray const signature = b.take(4);
            if (signature != "8BIM" && signature != "8B64")
                break;
            QByteArray const key = b.take(4);
            static QList<QByteArray> const longer = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
            qint64 const length = qint64(b.number(h.big && longer.contains(key) ? 8 : 4));
            QByteArray const body = b.take(length);
            if (key == "luni" && body.size() >= 4) {
                Bytes u{body};
                int const n = int(u.number(4));
                QString name;
                for (int i = 0; i < n && u.has(2); ++i)
                    name += QChar(char16_t(u.number(2)));
                if (!name.isEmpty())
                    r.layer.name = name;
            } else if ((key == "lsct" || key == "lsdk") && body.size() >= 4) {
                Bytes s{body};
                int const type = int(s.number(4));
                if (type == 1 || type == 2) {
                    r.layer.kind = Psd::Layer::GroupStart;
                    r.layer.closed = type == 2;
                } else if (type == 3) {
                    r.layer.kind = Psd::Layer::GroupEnd;
                }
                if (body.size() >= 12)
                    r.layer.blend = body.mid(8, 4);
            } else if (key == "iOpa" && !body.isEmpty()) {
                r.layer.opacity *= quint8(body[0]) / 255.0;
            } else if ((key == "vmsk" || key == "vsms") && !body.isEmpty()) {
                r.vector = vectorMask(body, QSize(h.width, h.height), &r.vectorInverted);
                r.hasVector = !r.vector.isEmpty();
            } else if (key == "lfx2" || key == "lrFX" || key == "lmfx") {
                r.layer.styled = true;
            } else if (QList<QByteArray>{"brit", "levl", "curv", "expA", "vibA", "hue2", "blnc", "blwh", "phfl", "mixr", "clrL", "nvrt", "post", "thrs",
                                         "grdm", "selc", "CgEd", "SoCo", "GdFl", "PtFl"}.contains(key)
                       && r.box.isEmpty()) {
                r.layer.kind = Psd::Layer::Adjustment;
            }
        }
        b.at = extraEnd;
        if (!b.ok) {
            *why = tr("The file is damaged.");
            return false;
        }
    }

    // The pixels, layer by layer, channel by channel.
    for (Record& r : records) {
        QByteArray const* rgb[3] = {nullptr, nullptr, nullptr};
        QByteArray const* grey = nullptr;
        QByteArray const* alpha = nullptr;
        QByteArray planes[3], greyPlane, alphaPlane, maskPlane;
        bool haveMask = false;
        for (Channel const& c : std::as_const(r.channels)) {
            QRect const box = c.id == -2 ? r.maskBox : r.box;
            if (c.id == -3 || (c.id < -1 && c.id != -2) || box.isEmpty()) {
                b.skip(quint64(c.length));
                continue;
            }
            QByteArray plane;
            if (!readChannel(b, c.length, box.width(), box.height(), h, plane)) {
                *why = tr("The pixels of layer \"%1\" could not be read.").arg(r.layer.name);
                return false;
            }
            if (c.id == -1) {
                alphaPlane = plane;
                alpha = &alphaPlane;
            } else if (c.id == -2) {
                maskPlane = plane;
                haveMask = true;
            } else if (h.mode == 3 && c.id >= 0 && c.id < 3) {
                planes[c.id] = plane;
                rgb[c.id] = &planes[c.id];
            } else if (c.id == 0) {
                greyPlane = plane;
                grey = &greyPlane;
            }
        }
        if (b.at > end) {
            *why = tr("The file is damaged.");
            return false;
        }
        if (r.layer.kind != Psd::Layer::Pixels || r.box.isEmpty())
            continue;
        r.layer.at = r.box.topLeft();
        r.layer.image = composed(h, r.box.width(), r.box.height(), grey, rgb, alpha);
        if (haveMask && !r.maskOff && !r.maskBox.isEmpty()) {
            qint64 const maskRow = rowBytesOf(r.maskBox.width(), h.depth);
            QRect const box = r.maskBox;
            int const fallback = r.maskDefault;
            int const depth = h.depth;
            masked(r.layer.image, r.layer.at, [&](int x, int y) {
                return box.contains(x, y) ? valueAt(maskPlane, maskRow, depth, x - box.left(), y - box.top(), false) : fallback;
            });
        }
        if (r.hasVector) {
            // The vector mask drawn as a matte the size of the layer.
            QImage matte(r.layer.image.size(), QImage::Format_Grayscale8);
            matte.fill(r.vectorInverted ? 255 : 0);
            QPainter p(&matte);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(-r.layer.at);
            p.fillPath(r.vector, r.vectorInverted ? Qt::black : Qt::white);
            p.end();
            QPoint const at = r.layer.at;
            masked(r.layer.image, r.layer.at, [&](int x, int y) { return int(matte.constScanLine(y - at.y())[x - at.x()]); });
        }
    }

    // Clipping: a clipped layer shows only where the layer it rests on does, and as faintly.
    Psd::Layer const* base = nullptr;
    for (Record& r : records) {
        Psd::Layer& layer = r.layer;
        if (layer.kind == Psd::Layer::GroupEnd || layer.kind == Psd::Layer::GroupStart) {
            base = nullptr;
            continue;
        }
        if (!layer.clipped) {
            base = layer.kind == Psd::Layer::Pixels ? &layer : nullptr;
            continue;
        }
        if (!base || base->image.isNull()) {
            layer.image = QImage();
            continue;
        }
        layer.opacity *= base->opacity;
        layer.visible = layer.visible && base->visible;
        if (!layer.image.isNull()) {
            QImage const& under = base->image;
            QPoint const at = base->at;
            masked(layer.image, layer.at, [&](int x, int y) {
                return QRect(at, under.size()).contains(x, y) ? qAlpha(under.pixel(x - at.x(), y - at.y())) : 0;
            });
        }
    }
    for (Record const& r : std::as_const(records))
        out.layers.push_back(r.layer);
    return true;
}

}

namespace Psd {

bool isPsd(QString const& file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) && f.read(4) == "8BPS";
}

QImage read(QString const& file, QString* error)
{
    QString why;
    auto fail = [error, &why]() {
        if (error)
            *error = why;
        return QImage();
    };
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        why = f.errorString();
        return fail();
    }
    Bytes b{f.readAll()};
    Header h;
    if (!readHeader(b, h, &why))
        return fail();
    b.skip(b.number(4));                    // colour mode data
    b.skip(b.number(4));                    // image resources
    // The layers: only their count is wanted - below nought, the flat picture's first extra channel is its transparency.
    qint64 const layersLength = qint64(b.number(h.big ? 8 : 4));
    qint64 const afterLayers = b.at + layersLength;
    // (A file without layers, as simpler programs write them, keeps its transparency there too.)
    bool transparent = true;
    if (layersLength > 0) {
        qint64 const infoLength = qint64(b.number(h.big ? 8 : 4));
        if (infoLength > 0)
            transparent = qint16(quint16(b.number(2))) < 0;
    }
    b.at = afterLayers;
    int const compression = int(b.number(2));
    if (!b.ok) {
        why = tr("The file is damaged.");
        return fail();
    }
    qint64 const rowBytes = rowBytesOf(h.width, h.depth);
    qint64 const planeBytes = rowBytes * h.height;
    QVector<QByteArray> planes(h.channels);
    if (compression == 0) {
        for (int c = 0; c < h.channels; ++c)
            planes[c] = b.take(planeBytes);
    } else if (compression == 1) {
        int const countSize = h.big ? 4 : 2;
        QVector<qint64> lengths(qsizetype(h.channels) * h.height);
        for (qint64& length : lengths)
            length = qint64(b.number(countSize));
        for (int c = 0; c < h.channels && b.ok; ++c) {
            planes[c] = QByteArray(planeBytes, Qt::Uninitialized);
            for (int y = 0; y < h.height && b.ok; ++y) {
                qint64 const length = lengths[qsizetype(c) * h.height + y];
                if (!b.has(length) || !unpack(b.data.constData() + b.at, length, planes[c].data() + rowBytes * y, rowBytes))
                    b.ok = false;
                b.at += length;
            }
        }
    } else if (compression == 2 || compression == 3) {
        QByteArray all;
        if (!unzip(b.data.mid(b.at), all, planeBytes * h.channels, h.width, h.depth, compression == 3))
            b.ok = false;
        for (int c = 0; c < h.channels && b.ok; ++c)
            planes[c] = all.mid(planeBytes * c, planeBytes);
    } else {
        why = tr("This Photoshop file is compressed in a way that cannot be read (%1).").arg(compression);
        return fail();
    }
    if (!b.ok) {
        why = tr("The file is damaged.");
        return fail();
    }
    bool const colour = h.mode == 3 && h.channels >= 3;
    QByteArray const* rgb[3] = {nullptr, nullptr, nullptr};
    if (colour)
        for (int c = 0; c < 3; ++c)
            rgb[c] = &planes[c];
    int const alphaChannel = colour ? 3 : 1;
    QByteArray const* alpha = transparent && h.channels > alphaChannel ? &planes[alphaChannel] : nullptr;
    return composed(h, h.width, h.height, colour ? nullptr : &planes[0], rgb, alpha);
}

bool readLayers(QString const& file, Layered& out, QString* error)
{
    QString why;
    auto fail = [error, &why]() {
        if (error)
            *error = why;
        return false;
    };
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        why = f.errorString();
        return fail();
    }
    Bytes b{f.readAll()};
    Header h;
    if (!readHeader(b, h, &why))
        return fail();
    out = Layered();
    out.size = QSize(h.width, h.height);
    b.skip(b.number(4));
    b.skip(b.number(4));
    int const lengthSize = h.big ? 8 : 4;
    qint64 const layersLength = qint64(b.number(lengthSize));
    qint64 const afterLayers = b.at + layersLength;
    if (!b.ok || afterLayers > b.data.size()) {
        why = tr("The file is damaged.");
        return fail();
    }
    if (layersLength > 0) {
        qint64 const infoLength = qint64(b.number(lengthSize));
        qint64 const afterInfo = b.at + infoLength;
        if (infoLength > 0 && !readLayerInfo(b, afterInfo, h, out, &why))
            return fail();
        b.at = afterInfo;
        // 16 and 32 bits: the layers are kept further on, under a key of their own.
        if (out.layers.isEmpty() && b.ok) {
            b.skip(b.number(4));        // global layer mask
            while (b.ok && b.at + 12 <= afterLayers) {
                QByteArray const signature = b.take(4);
                if (signature != "8BIM" && signature != "8B64")
                    break;
                QByteArray const key = b.take(4);
                bool const layers = key == "Lr16" || key == "Lr32" || key == "Layr";
                static QList<QByteArray> const longer = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
                qint64 const length = qint64(b.number(h.big && longer.contains(key) ? 8 : 4));
                qint64 const next = b.at + length;
                if (layers && !readLayerInfo(b, next, h, out, &why))
                    return fail();
                b.at = next;
                if (layers)
                    break;
            }
        }
    }
    if (out.layers.isEmpty()) {
        // No layers: the flat picture, as one.
        Layer only;
        only.name = tr("Background");
        only.image = read(file, &why);
        if (only.image.isNull())
            return fail();
        out.layers.push_back(only);
    }
    return true;
}

}
