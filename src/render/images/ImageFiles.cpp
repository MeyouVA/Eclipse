// Eclipse - writing rendered frames as picture files.
#include "render/images/ImageFiles.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {

QString said(char const* text)
{
    return QCoreApplication::translate("ImageFiles", text);
}

// Bytes in the order of the file: little-endian (TGA, TIFF, EXR) or big-endian (SGI, PSD, DPX).
struct Bytes {
    QByteArray data;
    bool big = false;

    void u8(int v) { data.append(char(quint8(v))); }
    void u16(quint32 v)
    {
        if (big) {
            u8(int(v >> 8));
            u8(int(v));
        } else {
            u8(int(v));
            u8(int(v >> 8));
        }
    }
    void u32(quint32 v)
    {
        if (big) {
            u16(v >> 16);
            u16(v & 0xffff);
        } else {
            u16(v & 0xffff);
            u16(v >> 16);
        }
    }
    void u64(quint64 v)
    {
        u32(quint32(v & 0xffffffffu));
        u32(quint32(v >> 32));
    }
    void f32(float v)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }
    void text(QByteArray const& t) { data.append(t); }
    void zeros(int n) { data.append(QByteArray(n, '\0')); }
    void putU32(qsizetype at, quint32 v)
    {
        Bytes b;
        b.big = big;
        b.u32(v);
        std::memcpy(data.data() + at, b.data.constData(), 4);
    }
};

bool writeFile(QString const& path, QByteArray const& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = said("Error when writing image to %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

QImage straight(QImage const& image)
{
    return image.convertToFormat(QImage::Format_ARGB32);
}

// Truevision TGA: 32-bit, run-length packed, top row first.
QByteArray tga(QImage const& source)
{
    QImage const image = straight(source);
    Bytes out;
    out.u8(0);
    out.u8(0);
    out.u8(10);
    out.zeros(5);
    out.u16(0);
    out.u16(0);
    out.u16(quint32(image.width()));
    out.u16(quint32(image.height()));
    out.u8(32);
    out.u8(0x28);
    auto pixel = [&out](QRgb c) {
        out.u8(qBlue(c));
        out.u8(qGreen(c));
        out.u8(qRed(c));
        out.u8(qAlpha(c));
    };
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        int x = 0;
        int const w = image.width();
        while (x < w) {
            int run = 1;
            while (x + run < w && run < 128 && row[x + run] == row[x])
                ++run;
            if (run > 1) {
                out.u8(0x80 | (run - 1));
                pixel(row[x]);
                x += run;
                continue;
            }
            int raw = 1;
            while (x + raw < w && raw < 128 && (x + raw + 1 >= w || row[x + raw] != row[x + raw + 1]))
                ++raw;
            out.u8(raw - 1);
            for (int i = 0; i < raw; ++i)
                pixel(row[x + i]);
            x += raw;
        }
    }
    out.zeros(8);
    out.text("TRUEVISION-XFILE.");
    out.u8(0);
    return out.data;
}

// Baseline TIFF: one strip of 8-bit red, green, blue and (unassociated) alpha, not packed.
QByteArray tiff(QImage const& source)
{
    QImage const image = straight(source);
    quint32 const w = quint32(image.width()), h = quint32(image.height());
    quint32 const pixels = w * h * 4;
    Bytes out;
    out.text("II");
    out.u16(42);
    out.u32(8);
    struct Entry {
        quint16 tag, type;
        quint32 count, value;
    };
    int const count = 14;
    quint32 const ifdEnd = 8 + 2 + count * 12 + 4;
    quint32 const bits = ifdEnd, resolution = bits + 8, data = resolution + 16;
    Entry const entries[count] = {
        {256, 4, 1, w}, {257, 4, 1, h}, {258, 3, 4, bits}, {259, 3, 1, 1}, {262, 3, 1, 2}, {273, 4, 1, data}, {277, 3, 1, 4},
        {278, 4, 1, h}, {279, 4, 1, pixels}, {282, 5, 1, resolution}, {283, 5, 1, resolution + 8}, {284, 3, 1, 1}, {296, 3, 1, 2}, {338, 3, 1, 2}};
    out.u16(count);
    for (Entry const& e : entries) {
        out.u16(e.tag);
        out.u16(e.type);
        out.u32(e.count);
        if (e.type == 3 && e.count == 1) {
            out.u16(e.value);
            out.u16(0);
        } else {
            out.u32(e.value);
        }
    }
    out.u32(0);
    for (int i = 0; i < 4; ++i)
        out.u16(8);
    for (int i = 0; i < 2; ++i) {
        out.u32(72);
        out.u32(1);
    }
    out.data.reserve(out.data.size() + qsizetype(pixels));
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            out.u8(qRed(row[x]));
            out.u8(qGreen(row[x]));
            out.u8(qBlue(row[x]));
            out.u8(qAlpha(row[x]));
        }
    }
    return out.data;
}

// Silicon Graphics image: four 8-bit planes, bottom row first, not packed.
QByteArray sgi(QImage const& source, QString const& name)
{
    QImage const image = straight(source);
    Bytes out;
    out.big = true;
    out.u16(474);
    out.u8(0);
    out.u8(1);
    out.u16(3);
    out.u16(quint32(image.width()));
    out.u16(quint32(image.height()));
    out.u16(4);
    out.u32(0);
    out.u32(255);
    out.u32(0);
    QByteArray title = name.toLatin1().left(79);
    title.append(QByteArray(80 - title.size(), '\0'));
    out.text(title);
    out.u32(0);
    out.zeros(404);
    for (int channel = 0; channel < 4; ++channel)
        for (int y = image.height() - 1; y >= 0; --y) {
            QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                out.u8(channel == 0 ? qRed(row[x]) : channel == 1 ? qGreen(row[x]) : channel == 2 ? qBlue(row[x]) : qAlpha(row[x]));
        }
    return out.data;
}

// Half-precision float bits of `value`, rounded to the nearest.
quint16 half(float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    quint32 const sign = (bits >> 16) & 0x8000u;
    int const exponent = int((bits >> 23) & 0xff) - 127 + 15;
    quint32 mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10)
            return quint16(sign);
        mantissa |= 0x800000u;
        int const shift = 14 - exponent;
        quint32 const rounded = (mantissa + (1u << (shift - 1))) >> shift;
        return quint16(sign | rounded);
    }
    if (exponent >= 31)
        return quint16(sign | 0x7c00u);
    quint32 result = sign | (quint32(exponent) << 10) | (mantissa >> 13);
    if (mantissa & 0x1000u)
        ++result;
    return quint16(result);
}

std::array<float, 256> const& linearTable()
{
    static std::array<float, 256> const table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            double const c = i / 255.0;
            t[size_t(i)] = float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return t;
    }();
    return table;
}

void attribute(Bytes& out, char const* name, char const* type, QByteArray const& value)
{
    out.text(name);
    out.u8(0);
    out.text(type);
    out.u8(0);
    out.u32(quint32(value.size()));
    out.text(value);
}

QByteArray box(int right, int bottom)
{
    Bytes b;
    b.u32(0);
    b.u32(0);
    b.u32(quint32(right));
    b.u32(quint32(bottom));
    return b.data;
}

// OpenEXR: linear light, half floats, the colours multiplied by the alpha as the format has them; not packed.
QByteArray exr(QImage const& source)
{
    QImage const image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    Bytes out;
    out.u32(20000630);
    out.u32(2);
    Bytes channels;
    for (char const* c : {"A", "B", "G", "R"}) {
        channels.text(c);
        channels.u8(0);
        channels.u32(1);
        channels.u8(0);
        channels.zeros(3);
        channels.u32(1);
        channels.u32(1);
    }
    channels.u8(0);
    attribute(out, "channels", "chlist", channels.data);
    attribute(out, "compression", "compression", QByteArray(1, '\0'));
    attribute(out, "dataWindow", "box2i", box(w - 1, h - 1));
    attribute(out, "displayWindow", "box2i", box(w - 1, h - 1));
    attribute(out, "lineOrder", "lineOrder", QByteArray(1, '\0'));
    Bytes one;
    one.f32(1.0f);
    attribute(out, "pixelAspectRatio", "float", one.data);
    Bytes centre;
    centre.f32(0.0f);
    centre.f32(0.0f);
    attribute(out, "screenWindowCenter", "v2f", centre.data);
    attribute(out, "screenWindowWidth", "float", one.data);
    out.u8(0);
    quint64 const lineBytes = quint64(w) * 2 * 4;
    quint64 const first = quint64(out.data.size()) + quint64(h) * 8;
    for (int y = 0; y < h; ++y)
        out.u64(first + quint64(y) * (8 + lineBytes));
    std::array<float, 256> const& linear = linearTable();
    for (int y = 0; y < h; ++y) {
        out.u32(quint32(y));
        out.u32(quint32(lineBytes));
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int channel = 0; channel < 4; ++channel)
            for (int x = 0; x < w; ++x) {
                QRgb const c = row[x];
                int const a = qAlpha(c);
                if (channel == 0) {
                    out.u16(half(a / 255.0f));
                    continue;
                }
                int const v = channel == 1 ? qBlue(c) : channel == 2 ? qGreen(c) : qRed(c);
                float const straightValue = a ? linear[size_t(std::min(255, v * 255 / a))] : 0.0f;
                out.u16(half(straightValue * (a / 255.0f)));
            }
    }
    return out.data;
}

// Digital Picture Exchange: 10-bit red, green and blue in 32-bit words (filled, method A), no alpha.
QByteArray dpx(QImage const& source, QString const& name)
{
    QImage const image = source.convertToFormat(QImage::Format_RGB32);
    quint32 const w = quint32(image.width()), h = quint32(image.height());
    quint32 const size = 2048 + w * h * 4;
    Bytes out;
    out.big = true;
    out.text("SDPX");
    out.u32(2048);
    out.text(QByteArray("V2.0", 4));
    out.zeros(4);
    out.u32(size);
    out.u32(1);
    out.u32(1664);
    out.u32(384);
    out.u32(0);
    auto fixed = [&out](QByteArray text, int length) {
        text = text.left(length - 1);
        out.text(text);
        out.zeros(length - int(text.size()));
    };
    fixed(name.toLatin1(), 100);
    fixed(QDateTime::currentDateTime().toString(QStringLiteral("yyyy:MM:dd:hh:mm:ss")).toLatin1(), 24);
    fixed(QByteArrayLiteral("Eclipse"), 100);
    fixed(QByteArray(), 200);
    fixed(QByteArray(), 200);
    out.u32(0xffffffffu);
    out.zeros(int(768 - out.data.size()));
    out.u16(0);
    out.u16(1);
    out.u32(w);
    out.u32(h);
    out.u32(0);
    out.u32(0);
    out.f32(0.0f);
    out.u32(1023);
    out.f32(2.047f);
    out.u8(50);
    out.u8(2);
    out.u8(2);
    out.u8(10);
    out.u16(1);
    out.u16(0);
    out.u32(2048);
    out.u32(0);
    out.u32(0);
    out.zeros(32);
    out.zeros(int(2048 - out.data.size()));
    out.data.reserve(qsizetype(size));
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            auto ten = [](int v) { return quint32(v * 1023 / 255); };
            out.u32(ten(qRed(row[x])) << 22 | ten(qGreen(row[x])) << 12 | ten(qBlue(row[x])) << 2);
        }
    }
    return out.data;
}

// The planes of a picture for Photoshop, red, green, blue and alpha, one byte a pixel.
QByteArray plane(QImage const& image, int channel)
{
    QByteArray bytes;
    bytes.reserve(qsizetype(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            bytes.append(char(channel == 0 ? qRed(row[x]) : channel == 1 ? qGreen(row[x]) : channel == 2 ? qBlue(row[x]) : qAlpha(row[x])));
    }
    return bytes;
}

QByteArray psd(QVector<ImageFiles::Layer> const& layers, QImage const& compositeSource)
{
    QImage const composite = straight(compositeSource);
    Bytes out;
    out.big = true;
    out.text("8BPS");
    out.u16(1);
    out.zeros(6);
    out.u16(4);
    out.u32(quint32(composite.height()));
    out.u32(quint32(composite.width()));
    out.u16(8);
    out.u16(3);
    out.u32(0);
    out.u32(0);
    if (layers.isEmpty()) {
        out.u32(0);
    } else {
        Bytes info;
        info.big = true;
        info.u16(quint32(quint16(qint16(-layers.size()))));
        QVector<QImage> images;
        for (ImageFiles::Layer const& layer : layers) {
            QImage const image = straight(layer.image.isNull() ? QImage(composite.size(), QImage::Format_ARGB32) : layer.image);
            images.push_back(image);
            quint32 const channelBytes = quint32(2 + qsizetype(image.width()) * image.height());
            info.u32(0);
            info.u32(0);
            info.u32(quint32(image.height()));
            info.u32(quint32(image.width()));
            info.u16(4);
            for (int id : {-1, 0, 1, 2}) {
                info.u16(quint32(quint16(qint16(id))));
                info.u32(channelBytes);
            }
            info.text("8BIM");
            info.text("norm");
            info.u8(255);
            info.u8(0);
            info.u8(layer.visible ? 0 : 2);
            info.u8(0);
            QByteArray name = layer.name.toLatin1().left(255);
            int const padded = int((name.size() + 1 + 3) / 4 * 4);
            info.u32(quint32(4 + 4 + padded));
            info.u32(0);
            info.u32(0);
            info.u8(int(name.size()));
            info.text(name);
            info.zeros(padded - 1 - int(name.size()));
        }
        for (QImage const& image : std::as_const(images))
            for (int channel : {3, 0, 1, 2}) {
                info.u16(0);
                info.text(plane(image, channel));
            }
        if (info.data.size() % 2)
            info.u8(0);
        out.u32(quint32(4 + info.data.size() + 4));
        out.u32(quint32(info.data.size()));
        out.text(info.data);
        out.u32(0);
    }
    out.u16(0);
    for (int channel = 0; channel < 4; ++channel)
        out.text(plane(composite, channel));
    return out.data;
}

}  // namespace

namespace ImageFiles {

QVector<Format> const& formats()
{
    static QVector<Format> const list = {
        {QStringLiteral("png"), QStringLiteral("PNG"), true, false},
        {QStringLiteral("tga"), QStringLiteral("Targa (TGA)"), true, false},
        {QStringLiteral("tif"), QStringLiteral("TIFF"), true, false},
        {QStringLiteral("psd"), QStringLiteral("Photoshop (PSD)"), true, true},
        {QStringLiteral("exr"), QStringLiteral("OpenEXR (linear, half float)"), true, false},
        {QStringLiteral("sgi"), QStringLiteral("Silicon Graphics (SGI)"), true, false},
        {QStringLiteral("dpx"), QStringLiteral("DPX (10-bit)"), false, false},
        {QStringLiteral("jpg"), QStringLiteral("JPEG"), false, false},
        {QStringLiteral("bmp"), QStringLiteral("BMP"), false, false},
    };
    return list;
}

Format const* find(QString const& suffix)
{
    QString const s = suffix.toLower() == QLatin1String("tiff") ? QStringLiteral("tif") : suffix.toLower() == QLatin1String("jpeg") ? QStringLiteral("jpg") : suffix.toLower();
    for (Format const& f : formats())
        if (f.suffix == s)
            return &f;
    return nullptr;
}

bool save(QImage const& image, QString const& path, QString const& format, QString* error, int jpegQuality)
{
    Format const* const f = find(format);
    if (!f) {
        if (error)
            *error = said("Unknown image format: %1").arg(format);
        return false;
    }
    if (image.isNull()) {
        if (error)
            *error = said("There is no picture to write to %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    QString const name = QFileInfo(path).completeBaseName();
    if (f->suffix == QLatin1String("tga"))
        return writeFile(path, tga(image), error);
    if (f->suffix == QLatin1String("tif"))
        return writeFile(path, tiff(image), error);
    if (f->suffix == QLatin1String("sgi"))
        return writeFile(path, sgi(image, name), error);
    if (f->suffix == QLatin1String("exr"))
        return writeFile(path, exr(image), error);
    if (f->suffix == QLatin1String("dpx"))
        return writeFile(path, dpx(image, name), error);
    if (f->suffix == QLatin1String("psd"))
        return writeFile(path, psd({}, image), error);
    bool const ok = f->suffix == QLatin1String("jpg") ? image.convertToFormat(QImage::Format_RGB32).save(path, "JPG", jpegQuality)
                    : f->suffix == QLatin1String("bmp")  ? image.convertToFormat(QImage::Format_RGB32).save(path, "BMP")
                                                         : image.save(path, "PNG");
    if (!ok && error)
        *error = said("Error when writing image to %1").arg(QDir::toNativeSeparators(path));
    return ok;
}

bool saveLayered(QVector<Layer> const& layers, QImage const& composite, QString const& path, QString* error)
{
    if (composite.isNull()) {
        if (error)
            *error = said("There is no picture to write to %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return writeFile(path, psd(layers, composite), error);
}

}  // namespace ImageFiles
