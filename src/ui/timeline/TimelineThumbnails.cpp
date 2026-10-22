// Eclipse - the Timeline: the thumbnails along a drawing layer's track, a picture at the start of each exposure.
#include "ui/timeline/Timeline.h"
#include "ui/timeline/TimelinePaintSpace.h"

#include <QPainter>
#include <QPixmap>

#include "canvas/Canvas.h"
#include "model/document/Commands.h"

namespace {

QPixmap picture(Scene const& scene, Drawing const& drawing, QSize size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QRectF const bounds = drawing.bounds();
    if (bounds.isEmpty() || size.width() < 6 || size.height() < 6)
        return pixmap;
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    double const scale = std::min((size.width() - 4) / bounds.width(), (size.height() - 4) / bounds.height());
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    Canvas::paintDrawing(painter, scene, drawing, 1.0);
    return pixmap;
}

}  // namespace

void Timeline::paintThumbnails(QPainter& p, TimelinePaintSpace const& space, int layerIndex, int y, int rh)
{
    Scene const& scene = space.scene;
    Layer const& layer = scene.layers[layerIndex];
    int const height = rh - 8;
    QSize const full(height * 4 / 3, height);
    for (int f = space.firstFrame; f <= space.lastFrame; ++f) {
        int const d = layer.drawingAt(f);
        if (d == Layer::kNoDrawing || (f > space.firstFrame && layer.drawingAt(f - 1) == d && !layer.isKey(f)))
            continue;
        // As wide as the exposure leaves room for, up to the picture's own width.
        int end = f + 1;
        while (end < scene.frameCount() && layer.drawingAt(end) == d && !layer.isKey(end))
            ++end;
        int const room = (end - f) * space.cw - 3;
        if (room < 10)
            continue;
        QSize const size(std::min(room, full.width()), height);
        quint64 const key = (quint64(quint32(layer.id)) << 32) | (quint64(quint32(d)) << 12) | quint64(size.width());
        if (!m_thumbnails.contains(key))
            m_thumbnails.insert(key, picture(scene, layer.drawings[d], size));
        int const x = kHeaderWidth + f * space.cw - space.sx + 2;
        p.drawPixmap(x, y + 4, m_thumbnails.value(key));
        p.setPen(QColor(40, 40, 40));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x, y + 4, size.width() - 1, size.height() - 1);
    }
}
