#include "ui/views/DepthView.h"

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include <QPainter>
#include <cmath>

void DepthView::setPlanningPreferences(double defaultZoom, int thumbnailSize)
{
    m_defaultZoom = std::isfinite(defaultZoom) ? std::clamp(defaultZoom, 0.2, 40.0) : 1.0;
    int const size = std::clamp(thumbnailSize, 0, 256);
    if (m_thumbnailSize != size) {
        m_thumbnailSize = size;
        m_thumbnails.clear();
    }
    update();
}

QRectF DepthView::thumbnailRect(QPointF a, QPointF b) const
{
    QPointF const centre = (a + b) / 2.0;
    QPointF const offset = m_mode == Top ? QPointF(-m_thumbnailSize / 2.0, 8) : QPointF(8, -m_thumbnailSize / 2.0);
    return QRectF(centre + offset, QSizeF(m_thumbnailSize, m_thumbnailSize));
}

QImage DepthView::thumbnail(int index)
{
    auto const cached = m_thumbnails.constFind(index);
    if (cached != m_thumbnails.cend())
        return *cached;
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[index];
    Pose const pose = scene.poseOf(index, m_frame);
    Drawing const drawing = Deform::ridden(scene, index, m_frame,
        Deform::shown(layer.shownAt(m_frame), layer, pose, layer.drawingAt(m_frame)));
    QRectF const bounds = drawing.bounds();
    QImage image(m_thumbnailSize, m_thumbnailSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (!bounds.isEmpty()) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        double const scale = std::max(1.0, m_thumbnailSize - 4.0) / std::max(bounds.width(), bounds.height());
        painter.translate(m_thumbnailSize / 2.0, m_thumbnailSize / 2.0);
        painter.scale(scale, scale);
        painter.translate(-bounds.center());
        Canvas::paintDrawing(painter, scene, drawing, layer.opacity * pose.opacity);
    }
    m_thumbnails.insert(index, image);
    return image;
}
