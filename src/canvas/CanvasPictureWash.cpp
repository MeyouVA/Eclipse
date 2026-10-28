#include "canvas/CanvasPaintPrivate.h"

#include <QCache>
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
