#include "canvas/CanvasPaintPrivate.h"

#include <QCache>
#include <QMutex>
#include <algorithm>

canvas_paint::PictureWash& canvas_paint::pictureWashState()
{
    static thread_local PictureWash state;
    return state;
}

QImage canvas_paint::washedPicture(QImage const& image, int percent, QColor const& colour)
{
    static thread_local QCache<QString, QImage> cache(64 * 1024);
    QString const key = QString::number(image.cacheKey()) + QLatin1Char(':') + QString::number(percent) + QLatin1Char(':') + QString::number(colour.rgba());
    if (QImage const* const found = cache.object(key))
        return *found;
    QImage washed = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    washed.setDevicePixelRatio(image.devicePixelRatio());
    {
        QPainter painter(&washed);
        painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        QColor tint = colour;
        tint.setAlpha(std::clamp(percent, 0, 100) * 255 / 100);
        painter.fillRect(QRectF(QPointF(), QSizeF(washed.size()) / washed.devicePixelRatio()), tint);
    }
    int const cost = int(std::min<qsizetype>((washed.sizeInBytes() + 1023) / 1024, 64 * 1024 + 1));
    cache.insert(key, new QImage(washed), cost);
    return washed;
}

QImage canvas_paint::convertedPicture(QImage const& image, Cms::Transform const& transform)
{
    if (transform.isIdentity() || image.isNull())
        return image;
    static QMutex lock;
    static QCache<QPair<qint64, quint64>, QImage> cache(256 * 1024);
    QPair<qint64, quint64> const key(image.cacheKey(), transform.id());
    {
        QMutexLocker const locker(&lock);
        if (QImage const* const found = cache.object(key))
            return *found;
    }
    QImage converted = transform.applied(image);
    converted.setDevicePixelRatio(image.devicePixelRatio());
    int const cost = int(std::min<qsizetype>((converted.sizeInBytes() + 1023) / 1024, 256 * 1024 + 1));
    QMutexLocker const locker(&lock);
    cache.insert(key, new QImage(converted), cost);
    return converted;
}
