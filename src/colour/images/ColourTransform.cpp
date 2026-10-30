// Eclipse - running colour conversions on colours and pictures, and keeping the ones made.
#include "colour/images/ColourTransform.h"

#include "colour/luts/Luts.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QRgba64>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace {

using namespace Cms;

std::atomic<quint64> gNextId{1};

quint8 toByte(float v)
{
    return quint8(std::clamp(int(std::lround(v * 255.0f)), 0, 255));
}

quint16 toWord(float v)
{
    return quint16(std::clamp(int(std::lround(v * 65535.0f)), 0, 65535));
}

bool isDeep(QImage::Format f)
{
    return f == QImage::Format_RGBA64_Premultiplied || f == QImage::Format_RGBA64 || f == QImage::Format_RGBX64;
}

struct Kept {
    Transform transform;
    QString error;
};

QMutex gKeptLock;
QHash<QString, Kept> gKept;
quint64 gKeptGeneration = 0;

Kept const* kept(QString const& key)
{
    if (gKeptGeneration != configGeneration()) {
        gKept.clear();
        gKeptGeneration = configGeneration();
    }
    auto it = gKept.constFind(key);
    return it == gKept.constEnd() ? nullptr : &it.value();
}

}  // namespace

namespace Cms {

struct Transform::Data {
    Chain chain;
    quint64 id = 0;
    bool perChannel = false;
    std::array<std::array<quint8, 256>, 3> table{};
    // For pictures of 16 bits a channel: the same tables with a value for each of their 65536 levels (made the first time they are wanted).
    mutable std::once_flag deepMade;
    mutable std::vector<quint16> deepTable;
};

Transform::Transform(Chain chain)
{
    chain = simplified(chain);
    if (chain.isEmpty())
        return;
    auto data = std::make_shared<Data>();
    data->chain = chain;
    data->id = gNextId++;
    data->perChannel = std::all_of(chain.cbegin(), chain.cend(), [](Op const& op) { return op.perChannel(); });
    if (data->perChannel)
        for (int v = 0; v < 256; ++v) {
            float rgb[3] = {v / 255.0f, v / 255.0f, v / 255.0f};
            run(chain, rgb);
            for (int c = 0; c < 3; ++c)
                data->table[size_t(c)][size_t(v)] = toByte(rgb[c]);
        }
    d = data;
}

quint64 Transform::id() const
{
    return d ? d->id : 0;
}

Chain Transform::chain() const
{
    return d ? d->chain : Chain();
}

void Transform::apply(float* rgb) const
{
    if (d)
        run(d->chain, rgb);
}

QColor Transform::apply(QColor const& colour) const
{
    if (!d || !colour.isValid())
        return colour;
    QColor const c = colour.toRgb();
    float rgb[3] = {c.redF(), c.greenF(), c.blueF()};
    run(d->chain, rgb);
    return QColor::fromRgbF(std::clamp(rgb[0], 0.0f, 1.0f), std::clamp(rgb[1], 0.0f, 1.0f), std::clamp(rgb[2], 0.0f, 1.0f), c.alphaF());
}

QRgb Transform::applyStraight(QRgb c) const
{
    if (!d)
        return c;
    if (d->perChannel)
        return qRgba(d->table[0][size_t(qRed(c))], d->table[1][size_t(qGreen(c))], d->table[2][size_t(qBlue(c))], qAlpha(c));
    float rgb[3] = {qRed(c) / 255.0f, qGreen(c) / 255.0f, qBlue(c) / 255.0f};
    run(d->chain, rgb);
    return qRgba(toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]), qAlpha(c));
}

void Transform::apply(QImage* image) const
{
    if (!d || !image || image->isNull())
        return;
    QImage::Format const f = image->format();
    bool const floats = f == QImage::Format_RGBA32FPx4 || f == QImage::Format_RGBA32FPx4_Premultiplied || f == QImage::Format_RGBX32FPx4
                        || f == QImage::Format_RGBA16FPx4 || f == QImage::Format_RGBA16FPx4_Premultiplied || f == QImage::Format_RGBX16FPx4;
    if (floats) {
        *image = toFloat(*image);
        return;
    }
    if (isDeep(f)) {
        applyDeep(image);
        return;
    }
    if (f != QImage::Format_ARGB32_Premultiplied && f != QImage::Format_ARGB32 && f != QImage::Format_RGB32)
        *image = image->convertToFormat(image->hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    bool const premultiplied = image->format() == QImage::Format_ARGB32_Premultiplied;
    // (Pictures of drawings have few colours: each worked out once, then looked up.)
    std::vector<quint32> keys, values;
    if (!d->perChannel) {
        keys.assign(65536, 0);
        values.assign(65536, 0);
    }
    for (int y = 0; y < image->height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image->scanLine(y));
        for (int x = 0; x < image->width(); ++x) {
            QRgb const c = row[x];
            int const a = qAlpha(c);
            if (premultiplied && a == 0)
                continue;
            QRgb const straight = premultiplied && a < 255 ? qUnpremultiply(c) : c;
            QRgb outColour;
            if (d->perChannel) {
                outColour = qRgba(d->table[0][size_t(qRed(straight))], d->table[1][size_t(qGreen(straight))], d->table[2][size_t(qBlue(straight))], a);
            } else {
                quint32 const key = (straight & 0xffffffu) | 0x1000000u;
                quint32 const slot = ((key * 2654435761u) >> 16) & 0xffffu;
                if (keys[slot] == key) {
                    outColour = (values[slot] & 0xffffffu) | (quint32(a) << 24);
                } else {
                    outColour = applyStraight(straight | 0xff000000u);
                    keys[slot] = key;
                    values[slot] = outColour;
                    outColour = (outColour & 0xffffffu) | (quint32(a) << 24);
                }
            }
            row[x] = premultiplied && a < 255 ? qPremultiply(outColour) : outColour;
        }
    }
}

void Transform::applyDeep(QImage* image) const
{
    bool const premultiplied = image->format() == QImage::Format_RGBA64_Premultiplied;
    if (d->perChannel)
        std::call_once(d->deepMade, [this] {
            d->deepTable.resize(3 * 65536);
            for (int v = 0; v < 65536; ++v) {
                float rgb[3] = {v / 65535.0f, v / 65535.0f, v / 65535.0f};
                run(d->chain, rgb);
                for (int c = 0; c < 3; ++c)
                    d->deepTable[size_t(c * 65536 + v)] = toWord(rgb[c]);
            }
        });
    // (Otherwise each colour is worked out once and kept in a cache, as for 8 bits.)
    std::vector<quint64> keys, values;
    if (!d->perChannel) {
        keys.assign(65536, ~quint64(0));
        values.assign(65536, 0);
    }
    for (int y = 0; y < image->height(); ++y) {
        QRgba64* const row = reinterpret_cast<QRgba64*>(image->scanLine(y));
        for (int x = 0; x < image->width(); ++x) {
            QRgba64 const c = row[x];
            quint16 const a = c.alpha();
            if (premultiplied && a == 0)
                continue;
            QRgba64 const straight = premultiplied && a < 65535 ? c.unpremultiplied() : c;
            QRgba64 outColour;
            if (d->perChannel) {
                outColour = QRgba64::fromRgba64(d->deepTable[straight.red()], d->deepTable[size_t(65536 + straight.green())], d->deepTable[size_t(2 * 65536 + straight.blue())], a);
            } else {
                quint64 const key = quint64(straight.red()) | quint64(straight.green()) << 16 | quint64(straight.blue()) << 32;
                quint32 const slot = quint32(((key * 0x9E3779B97F4A7C15ull) >> 48) & 0xffffu);
                if (keys[slot] != key) {
                    float rgb[3] = {straight.red() / 65535.0f, straight.green() / 65535.0f, straight.blue() / 65535.0f};
                    run(d->chain, rgb);
                    keys[slot] = key;
                    values[slot] = quint64(toWord(rgb[0])) | quint64(toWord(rgb[1])) << 16 | quint64(toWord(rgb[2])) << 32;
                }
                quint64 const v = values[slot];
                outColour = QRgba64::fromRgba64(quint16(v), quint16(v >> 16), quint16(v >> 32), a);
            }
            row[x] = premultiplied && a < 65535 ? outColour.premultiplied() : outColour;
        }
    }
}

QImage Transform::applied(QImage const& image) const
{
    QImage out = image;
    apply(&out);
    return out;
}

QImage Transform::toFloat(QImage const& image) const
{
    QImage out = image.convertToFormat(QImage::Format_RGBA32FPx4_Premultiplied);
    if (!d)
        return out;
    for (int y = 0; y < out.height(); ++y) {
        float* const row = reinterpret_cast<float*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            float* const p = row + x * 4;
            float const a = p[3];
            if (a <= 0.0f)
                continue;
            float rgb[3] = {p[0] / a, p[1] / a, p[2] / a};
            run(d->chain, rgb);
            for (int c = 0; c < 3; ++c)
                p[c] = rgb[c] * a;
        }
    }
    return out;
}

Transform between(QString const& from, QString const& to, QString* error)
{
    if (from.compare(to, Qt::CaseInsensitive) == 0) {
        if (error)
            error->clear();
        return {};
    }
    QString const key = QStringLiteral("cs\n") + from + QLatin1Char('\n') + to;
    QMutexLocker locker(&gKeptLock);
    if (Kept const* k = kept(key)) {
        if (error)
            *error = k->error;
        return k->transform;
    }
    locker.unlock();
    Chain chain;
    QString why;
    Kept made;
    if (config()->convert(from, to, &chain, &why))
        made.transform = Transform(chain);
    else
        made.error = why;
    if (error)
        *error = made.error;
    locker.relock();
    gKept.insert(key, made);
    return made.transform;
}

Transform toDisplay(QString const& from, QString const& display, QString const& view, QString* error)
{
    QString const key = QStringLiteral("view\n") + from + QLatin1Char('\n') + display + QLatin1Char('\n') + view;
    QMutexLocker locker(&gKeptLock);
    if (Kept const* k = kept(key)) {
        if (error)
            *error = k->error;
        return k->transform;
    }
    locker.unlock();
    Chain chain;
    QString why;
    Kept made;
    if (config()->toDisplay(from, display, view, &chain, &why))
        made.transform = Transform(chain);
    else
        made.error = why;
    if (error)
        *error = made.error;
    locker.relock();
    gKept.insert(key, made);
    return made.transform;
}

Transform ofLut(QString const& path, QString* error)
{
    QFileInfo const info(path);
    QString const key = QStringLiteral("lut\n") + info.absoluteFilePath() + QLatin1Char('\n') + QString::number(info.lastModified().toMSecsSinceEpoch())
                        + QLatin1Char('\n') + QString::number(info.size());
    QMutexLocker locker(&gKeptLock);
    if (Kept const* k = kept(key)) {
        if (error)
            *error = k->error;
        return k->transform;
    }
    locker.unlock();
    Chain chain;
    QString why;
    Kept made;
    if (Luts::read(path, &chain, &why))
        made.transform = Transform(chain);
    else
        made.error = why;
    if (error)
        *error = made.error;
    locker.relock();
    gKept.insert(key, made);
    return made.transform;
}

}  // namespace Cms
