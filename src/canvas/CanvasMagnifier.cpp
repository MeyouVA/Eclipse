// Eclipse - the Camera view: the magnifier, a box beside the pointer showing what is under it enlarged.
#include "canvas/Canvas.h"

#include <QLabel>
#include <QSettings>

void Canvas::onActionMagnifier(bool on)
{
    if (!on) {
        if (m_magnifier)
            m_magnifier->hide();
        return;
    }
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    m_magnifyZoom = std::clamp(s.value(QStringLiteral("prefs/magnifyZoom"), 2.0).toDouble(), 1.0, 16.0);
    m_magnifySize = QSize(std::clamp(s.value(QStringLiteral("prefs/magnifyWidth"), 150).toInt(), 40, 800), std::clamp(s.value(QStringLiteral("prefs/magnifyHeight"), 150).toInt(), 40, 800));
    m_magnifyOffset = QPoint(s.value(QStringLiteral("prefs/magnifyOffsetX"), -90).toInt(), s.value(QStringLiteral("prefs/magnifyOffsetY"), 90).toInt());
    if (!m_magnifier) {
        QLabel* const box = new QLabel(this);
        box->setObjectName(QStringLiteral("CanvasMagnifier"));
        box->setAttribute(Qt::WA_TransparentForMouseEvents);
        box->setStyleSheet(QStringLiteral("border: 2px solid #303030; background: #202020;"));
        m_magnifier = box;
    }
    m_magnifier->resize(m_magnifySize);
    m_magnifier->show();
    updateMagnifier(m_hoverKnown ? m_hoverWidget : QPointF(width() / 2.0, height() / 2.0));
}

bool Canvas::magnifying() const
{
    return m_magnifier && m_magnifier->isVisible();
}

void Canvas::updateMagnifier(QPointF widgetPos)
{
    auto* const box = qobject_cast<QLabel*>(m_magnifier);
    if (!box || !box->isVisible())
        return;
    // What is under the pointer, so much smaller than the box, painted again at the box's size (without the box itself).
    QSizeF const seen(m_magnifySize.width() / m_magnifyZoom, m_magnifySize.height() / m_magnifyZoom);
    QRect const source = QRectF(widgetPos - QPointF(seen.width() / 2.0, seen.height() / 2.0), seen).toAlignedRect();
    qreal const dpr = devicePixelRatioF();
    QImage under((source.size() * dpr), QImage::Format_ARGB32_Premultiplied);
    under.setDevicePixelRatio(dpr);
    under.fill(QColor(32, 32, 32));
    render(&under, QPoint(), QRegion(source), QWidget::DrawWindowBackground);
    box->setPixmap(QPixmap::fromImage(under.scaled(m_magnifySize * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    QPoint centre = widgetPos.toPoint() + m_magnifyOffset;
    QPoint corner = centre - QPoint(m_magnifySize.width() / 2, m_magnifySize.height() / 2);
    corner.setX(std::clamp(corner.x(), 0, std::max(0, width() - m_magnifySize.width())));
    corner.setY(std::clamp(corner.y(), 0, std::max(0, height() - m_magnifySize.height())));
    box->move(corner);
    box->raise();
}
