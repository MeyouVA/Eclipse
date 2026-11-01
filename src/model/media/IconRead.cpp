#include "model/media/IconRead.h"
#include "render/images/ImageFiles.h"
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QtEndian>

namespace {
QByteArray unpack(QByteArray const& bytes, int count, int channels, int header)
{
    QByteArray planes(count * channels, '\0');
    qsizetype at = header;
    for (int channel = 0; channel < channels; ++channel) {
        int out = 0;
        while (out < count) {
            if (at >= bytes.size()) return {};
            int const token = quint8(bytes[at++]);
            int const length = token < 128 ? token + 1 : token - 125;
            if (length > count - out || (token < 128 ? at + length > bytes.size() : at >= bytes.size())) return {};
            for (int i = 0; i < length; ++i) planes[channel * count + out++] = bytes[token < 128 ? at++ : at];
            if (token >= 128) ++at;
        }
    }
    return planes;
}
QVector<QRgb> palette(int depth)
{
    if (depth == 1) return {qRgb(255,255,255), qRgb(0,0,0)};
    if (depth == 4) return {qRgb(255,255,255),qRgb(255,255,0),qRgb(255,102,0),qRgb(221,0,0),qRgb(255,0,153),qRgb(51,0,153),qRgb(0,0,221),qRgb(0,153,255),qRgb(0,187,0),qRgb(0,102,0),qRgb(102,51,0),qRgb(153,102,51),qRgb(187,187,187),qRgb(136,136,136),qRgb(68,68,68),qRgb(0,0,0)};
    QVector<QRgb> colors;
    for (int r = 255; r >= 0; r -= 51) for (int g = 255; g >= 0; g -= 51) for (int b = 255; b >= 0; b -= 51)
        if (r || g || b) colors.push_back(qRgb(r,g,b));
    for (int channel = 0; channel < 4; ++channel) for (int value : {238,221,187,170,136,119,85,68,34,17})
        colors.push_back(qRgb(channel == 0 || channel == 3 ? value : 0, channel == 1 || channel == 3 ? value : 0, channel == 2 || channel == 3 ? value : 0));
    colors.push_back(qRgb(0,0,0));
    return colors;
}
}

QImage IconRead::read(QString const& path, QString* error)
{
    auto fail = [&](QString const& why) { if (error) *error = why; return QImage(); };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QByteArray const data = file.readAll();
    if (data.size() < 8 || !data.startsWith("icns")) return fail(QStringLiteral("Invalid icon header."));
    quint32 const length = qFromBigEndian<quint32>(data.constData() + 4);
    if (length < 8 || length > data.size()) return fail(QStringLiteral("Truncated icon file."));
    QHash<QByteArray,QByteArray> parts;
    for (qsizetype at = 8; at < length;) {
        if (at + 8 > length) return fail(QStringLiteral("Truncated icon element."));
        quint32 const size = qFromBigEndian<quint32>(data.constData() + at + 4);
        if (size < 8 || size > length - at) return fail(QStringLiteral("Invalid icon element size."));
        parts.insert(data.mid(at,4), data.mid(at+8,size-8)); at += size;
    }
    QImage best;
    auto keep = [&](QImage const& image) { if (image.width() > best.width() || (image.width() == best.width() && image.hasAlphaChannel())) best = image; };
    struct Entry { char const* rgb; char const* mask; int width; int height; int depth; };
    static Entry const entries[] = {
        {"ics#","ics#",16,16,1},{"icm#","icm#",16,12,1},{"ICN#","ICN#",32,32,1},{"ich#","ich#",48,48,1},
        {"ics4","ics#",16,16,4},{"icm4","icm#",16,12,4},{"icl4","ICN#",32,32,4},{"ich4","ich#",48,48,4},
        {"ics8","ics#",16,16,8},{"icm8","icm#",16,12,8},{"icl8","ICN#",32,32,8},{"ich8","ich#",48,48,8},
        {"is32","s8mk",16,16,24},{"il32","l8mk",32,32,24},{"ih32","h8mk",48,48,24},{"it32","t8mk",128,128,24},
        {"icp4","s8mk",16,16,24},{"icp5","l8mk",32,32,24},{"icp6","h8mk",48,48,24},
        {"ic04","",16,16,32},{"ic05","",32,32,32},{"icsb","",18,18,32}
    };
    for (Entry const& entry : entries) {
        QByteArray const bytes = parts.value(entry.rgb);
        if (bytes.isEmpty() || bytes.startsWith("\x89PNG") || bytes.mid(4,4) == "jP  ") continue;
        int const count = entry.width * entry.height;
        QByteArray const mask = parts.value(entry.mask);
        QImage image(entry.width,entry.height,QImage::Format_ARGB32);
        if (entry.depth <= 8) {
            if (bytes.size() < count * entry.depth / 8) continue;
            QVector<QRgb> const colors = palette(entry.depth);
            for (int i = 0; i < count; ++i) {
                int const shift = 8 - entry.depth - (i * entry.depth) % 8;
                int const index = (quint8(bytes[i * entry.depth / 8]) >> shift) & ((1 << entry.depth) - 1);
                int const alpha = mask.size() >= count/4 ? ((quint8(mask[count/8 + i/8]) >> (7-i%8)) & 1) * 255 : 255;
                image.setPixel(i%entry.width,i/entry.width,(colors[index] & 0xffffff) | (quint32(alpha)<<24));
            }
        } else {
            bool const argb = entry.depth == 32;
            int const header = argb && bytes.startsWith("ARGB") ? 4 : QByteArray(entry.rgb) == "it32" && bytes.startsWith(QByteArray(4,'\0')) ? 4 : 0;
            bool const raw = !argb && bytes.size() == count*4;
            QByteArray const planes = raw ? QByteArray() : unpack(bytes,count,argb?4:3,header);
            if (!raw && planes.isEmpty()) continue;
            for (int i = 0; i < count; ++i) {
                int const offset = argb ? count : 0;
                int const alpha = argb ? quint8(planes[i]) : mask.size() >= count ? quint8(mask[i]) : 255;
                int const r = raw ? quint8(bytes[i*4+1]) : quint8(planes[offset+i]);
                int const g = raw ? quint8(bytes[i*4+2]) : quint8(planes[offset+count+i]);
                int const b = raw ? quint8(bytes[i*4+3]) : quint8(planes[offset+count*2+i]);
                image.setPixel(i%entry.width,i/entry.width,qRgba(r,g,b,alpha));
            }
        }
        keep(image);
    }
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) {
        if (it->startsWith("\x89PNG")) keep(QImage::fromData(*it,"PNG"));
        else if (it->mid(4,4) == "jP  " || it->startsWith(QByteArray::fromHex("ff4fff51"))) {
            QTemporaryDir temp;
            if (!temp.isValid()) continue;
            QString const name = temp.filePath(QStringLiteral("icon.jp2")); QFile output(name);
            if (!output.open(QIODevice::WriteOnly) || output.write(*it) != it->size()) continue;
            output.close(); keep(ImageFiles::load(name));
        }
    }
    if (best.isNull()) return fail(QStringLiteral("No readable image in the icon file."));
    return best;
}
