// Eclipse - the Camera view: cursors drawn for the paint tools, the Ink tool and the Dropper.
#include "canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

namespace {

enum Badge { NoBadge, Minus, Square, Arrows, Dots };

constexpr int kSize = 32;

QPixmap blank()
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

// A shape traced black over a white edge, so it shows on dark and light alike.
void outlined(QPainter& p, QPainterPath const& path, bool filled)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(Qt::black, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(filled ? QBrush(Qt::white) : QBrush(Qt::NoBrush));
    p.drawPath(path);
}

void badge(QPainter& p, Badge kind)
{
    if (kind == NoBadge)
        return;
    QRectF const box(20.5, 20.5, 10, 10);
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(Qt::white);
    p.drawRect(box);
    p.setBrush(Qt::black);
    QPointF const c = box.center();
    if (kind == Minus) {
        p.drawRect(QRectF(c.x() - 3, c.y() - 0.5, 6, 1));
    } else if (kind == Square) {
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(c.x() - 2.5, c.y() - 2.5, 5, 5));
    } else if (kind == Arrows) {
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(c.x() - 3, c.y() - 3, 6, 6), 30 * 16, 300 * 16);
        p.drawLine(QPointF(c.x() + 3, c.y() - 3), QPointF(c.x() + 3, c.y() - 0.5));
    } else {
        for (QPointF const d : {QPointF(-2, -2), QPointF(2, -2), QPointF(0, 0), QPointF(-2, 2), QPointF(2, 2)})
            p.drawRect(QRectF(c + d - QPointF(0.5, 0.5), QSizeF(1, 1)));
    }
}

// A pail tipped to the left, the paint running from its lip: the drop's end is the point painted.
QCursor pailCursor(Badge kind)
{
    QPixmap pixmap = blank();
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath pail;
    pail.moveTo(9, 10);
    pail.lineTo(19, 4);
    pail.lineTo(26, 14);
    pail.lineTo(16, 22);
    pail.closeSubpath();
    outlined(p, pail, true);
    QPainterPath handle;
    handle.moveTo(12, 8);
    handle.quadTo(14, 0, 22, 6);
    outlined(p, handle, false);
    QPainterPath drop;
    drop.moveTo(9, 10);
    drop.cubicTo(5, 12, 3, 16, 3, 24);
    drop.cubicTo(1, 26, 1, 29, 3, 29);
    drop.cubicTo(5, 29, 5, 26, 3, 24);
    outlined(p, drop, false);
    badge(p, kind);
    p.end();
    return QCursor(pixmap, 3, 29);
}

// A pen nib pointing down and left: its tip is the point inked.
QCursor nibCursor()
{
    QPixmap pixmap = blank();
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath nib;
    nib.moveTo(2, 29);
    nib.lineTo(6, 17);
    nib.lineTo(18, 5);
    nib.lineTo(26, 13);
    nib.lineTo(14, 25);
    nib.closeSubpath();
    outlined(p, nib, true);
    QPainterPath slit;
    slit.moveTo(2, 29);
    slit.lineTo(10, 21);
    slit.addEllipse(QPointF(11, 20), 1.5, 1.5);
    outlined(p, slit, false);
    QPainterPath grip;
    grip.moveTo(21, 2);
    grip.lineTo(29, 10);
    outlined(p, grip, false);
    p.end();
    return QCursor(pixmap, 2, 29);
}

// A pipette, its tip down and left: the colour (or texture) is taken from under the tip.
QCursor pipetteCursor(Badge kind)
{
    QPixmap pixmap = blank();
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath tube;
    tube.moveTo(2, 29);
    tube.lineTo(4, 23);
    tube.lineTo(16, 11);
    tube.lineTo(20, 15);
    tube.lineTo(8, 27);
    tube.closeSubpath();
    outlined(p, tube, true);
    QPainterPath bulb;
    bulb.moveTo(15, 10);
    bulb.lineTo(21, 16);
    bulb.addEllipse(QRectF(19, 3, 9, 9));
    outlined(p, bulb, true);
    badge(p, kind);
    p.end();
    return QCursor(pixmap, 2, 29);
}

}  // namespace

QCursor Canvas::toolCursor() const
{
    static QCursor const paint = pailCursor(NoBadge), unpaint = pailCursor(Minus), unpainted = pailCursor(Square), repaint = pailCursor(Arrows);
    static QCursor const ink = nibCursor(), dropper = pipetteCursor(NoBadge), textureDropper = pipetteCursor(Dots);
    switch (m_tool) {
    case Paint: return paint;
    case Unpaint: return unpaint;
    case PaintUnpainted: return unpainted;
    case Repaint: return repaint;
    case Ink: return ink;
    case Dropper: return m_dropperTexture ? textureDropper : dropper;
    default: return QCursor(Qt::PointingHandCursor);
    }
}
