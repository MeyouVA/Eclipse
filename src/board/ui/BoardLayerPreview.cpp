#include "board/ui/BoardLayerPreview.h"
#include "board/model/BoardProject.h"
#include "canvas/Canvas.h"

#include <QPainter>

QPixmap boardLayerPreview(BoardProject const& project, BoardLayer const& layer, QSize const& size)
{
    Scene scene;
    scene.width = project.width;
    scene.height = project.height;
    QPixmap thumbnail(size * 2);
    thumbnail.fill(QColor(238, 238, 238));
    QPainter painter(&thumbnail);
    double const scale = std::min(double(thumbnail.width()) / project.width, double(thumbnail.height()) / project.height);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.translate((thumbnail.width() - project.width * scale) / 2, (thumbnail.height() - project.height * scale) / 2);
    painter.scale(scale, scale);
    painter.setClipRect(QRectF(0, 0, project.width, project.height));
    Canvas::paintDrawing(painter, scene, layer.drawing, 1.0);
    painter.end();
    return thumbnail.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
