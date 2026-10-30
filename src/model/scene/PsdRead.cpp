// Eclipse - reading the flattened picture of a Photoshop file (PSD or PSB).
#include "model/scene/PsdRead.h"

#include <QCoreApplication>
#include <QFile>
#include <algorithm>
#include <cstring>

namespace {

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
    void skip(quint64 n)
    {
        if (!has(qint64(n)))
            ok = false;
        else
            at += qint64(n);
    }
};

QString tr(char const* text) { return QCoreApplication::translate("Psd", text); }

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

}

namespace Psd {

bool isPsd(QString const& file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) && f.read(4) == "8BPS";
}

QImage read(QString const& file, QString* error)
{
    auto fail = [error](QString const& why) {
        if (error)
            *error = why;
        return QImage();
    };
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    Bytes b{f.readAll()};
    if (b.data.left(4) != "8BPS")
        return fail(tr("This is not a Photoshop file."));
    b.at = 4;
    int const version = int(b.number(2));
    if (version != 1 && version != 2)
        return fail(tr("Unknown Photoshop file version (%1).").arg(version));
    bool const big = version == 2;
    b.skip(6);
    int const channels = int(b.number(2));
    int const height = int(b.number(4));
    int const width = int(b.number(4));
    int const depth = int(b.number(2));
    int const mode = int(b.number(2));
    if (!b.ok || channels < 1 || width < 1 || height < 1 || qint64(width) * height > 400'000'000LL)
        return fail(tr("The file is damaged."));
    if (mode != 0 && mode != 1 && mode != 3 && mode != 7)
        return fail(tr("Unsupported PSD image mode (%1).\nSupported modes are RGB, Grayscale, Bitmap (Black and White) and Multichannel.").arg(modeName(mode)));
    if (depth != 1 && depth != 8 && depth != 16 && depth != 32)
        return fail(tr("Unsupported PSD bit depth (%1).").arg(depth));
    b.skip(b.number(4));                    // colour mode data
    b.skip(b.number(4));                    // image resources
    b.skip(b.number(big ? 8 : 4));          // layers and masks
    int const compression = int(b.number(2));
    if (!b.ok)
        return fail(tr("The file is damaged."));
    if (compression != 0 && compression != 1)
        return fail(tr("This Photoshop file is compressed in a way that cannot be read (%1).").arg(compression));

    // The channels wanted: grey or red, green, blue, and see-through.
    bool const colour = mode == 3 && channels >= 3;
    int const used = std::min(channels, colour ? 4 : 2);
    qint64 const rowBytes = depth == 1 ? (width + 7) / 8 : qint64(width) * (depth / 8);
    QVector<QByteArray> planes(used);
    if (compression == 0) {
        for (int c = 0; c < channels; ++c) {
            if (!b.has(rowBytes * height))
                return fail(tr("The file is damaged."));
            if (c < used)
                planes[c] = b.data.mid(b.at, rowBytes * height);
            b.at += rowBytes * height;
        }
    } else {
        QVector<quint64> lengths(qsizetype(channels) * height);
        for (quint64& length : lengths)
            length = b.number(big ? 4 : 2);
        for (int c = 0; c < channels && b.ok; ++c) {
            QByteArray plane(rowBytes * height, Qt::Uninitialized);
            for (int y = 0; y < height; ++y) {
                quint64 const length = lengths[qsizetype(c) * height + y];
                if (!b.has(qint64(length)))
                    return fail(tr("The file is damaged."));
                if (c < used && !unpack(b.data.constData() + b.at, qint64(length), plane.data() + rowBytes * y, rowBytes))
                    return fail(tr("The file is damaged."));
                b.at += qint64(length);
            }
            if (c < used)
                planes[c] = plane;
        }
    }
    if (!b.ok)
        return fail(tr("The file is damaged."));

    // A channel's value at a pixel, 0..255.
    auto value = [&](int c, int x, int y) -> int {
        char const* const row = planes[c].constData() + rowBytes * y;
        switch (depth) {
        case 1: return (quint8(row[x / 8]) >> (7 - x % 8)) & 1 ? 0 : 255;   // 1 is black
        case 8: return quint8(row[x]);
        case 16: return quint8(row[x * 2]);
        default: {
            quint32 bits = 0;
            for (int i = 0; i < 4; ++i)
                bits = (bits << 8) | quint8(row[x * 4 + i]);
            float v;
            std::memcpy(&v, &bits, 4);
            return int(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        }
    };
    int const alpha = colour ? (used == 4 ? 3 : -1) : (used == 2 && mode != 7 ? 1 : -1);
    QImage image(width, height, alpha >= 0 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int const r = value(0, x, y);
            int const g = colour ? value(1, x, y) : r;
            int const bl = colour ? value(2, x, y) : r;
            out[x] = qRgba(r, g, bl, alpha >= 0 ? value(alpha, x, y) : 255);
        }
    }
    return image;
}

}
