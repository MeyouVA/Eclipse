#include "ui/views/PlaybackSurface.h"
#include <QColorSpace>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

PlaybackSurface::PlaybackSurface(QWidget* parent):QWidget(parent)
{
    setMinimumSize(160,100); setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setObjectName(QStringLiteral("PlaybackImage"));
    setToolTip(tr("Wheel to zoom, middle-drag to pan, double-click to fit."));
}
void PlaybackSurface::setImage(QImage const& image)
{
    m_image=image;
    buildDisplay();
    update();
}
void PlaybackSurface::setChannel(int channel) { m_channel=std::clamp(channel,0,4); buildDisplay(); update(); }
void PlaybackSurface::buildDisplay()
{
    m_display=m_image.colorSpace().isValid()?m_image.convertedToColorSpace(QColorSpace::SRgb):m_image;
    if(m_display.isNull()) m_display=m_image;
    if(m_channel==0 || m_display.isNull()) return;
    m_display=m_display.convertToFormat(QImage::Format_RGBA8888);
    for(int y=0;y<m_display.height();++y) {
        auto* row=m_display.scanLine(y);
        for(int x=0;x<m_display.width();++x) {
            uchar const value=row[x*4+m_channel-1];
            row[x*4]=row[x*4+1]=row[x*4+2]=value; row[x*4+3]=255;
        }
    }
    m_display.setColorSpace(QColorSpace::SRgb);
}
double PlaybackSurface::scale() const
{
    return m_fit && !m_image.isNull()?std::min(double(width())/m_image.width(),double(height())/m_image.height()):m_scale;
}
void PlaybackSurface::zoom(double factor) { m_scale=std::clamp(scale()*factor,.01,64.0); m_fit=false; update(); }
void PlaybackSurface::fit() { m_fit=true; m_pan={}; update(); }
void PlaybackSurface::actualSize() { m_fit=false; m_scale=1; m_pan={}; update(); }
void PlaybackSurface::paintEvent(QPaintEvent*)
{
    if(m_blank) { QPainter painter(this); painter.fillRect(rect(),Qt::black); return; }
    QPainter painter(this); painter.fillRect(rect(),QColor(42,42,42));
    if(m_image.isNull()) { painter.setPen(Qt::lightGray); painter.drawText(rect(),Qt::AlignCenter,tr("Load a folder of picture frames to begin.")); return; }
    double const s=scale(); QPointF const origin=QPointF(width()/2.0,height()/2.0)+m_pan-QPointF(m_image.width()*s/2,m_image.height()*s/2);
    QRectF const target(origin,QSizeF(m_image.size())*s);
    painter.save(); painter.setClipRect(target);
    for(int y=0;y<height();y+=16) for(int x=0;x<width();x+=16)
        painter.fillRect(QRect(x,y,16,16),((x/16+y/16)%2)?QColor(120,120,120):QColor(160,160,160));
    painter.setRenderHint(QPainter::SmoothPixmapTransform,s<1); painter.drawImage(target,m_display); painter.restore();
}
void PlaybackSurface::wheelEvent(QWheelEvent* event)
{
    if(m_image.isNull()) { event->ignore(); return; }
    double const before=scale(); QPointF const anchor=event->position()-QPointF(width()/2.0,height()/2.0);
    zoom(std::pow(1.2,event->angleDelta().y()/120.0));
    if(before>0) m_pan=anchor-(anchor-m_pan)*(scale()/before);
    update(); event->accept();
}
void PlaybackSurface::mousePressEvent(QMouseEvent* event)
{
    if(event->button()==Qt::MiddleButton) { m_drag=true; m_last=event->position(); setCursor(Qt::ClosedHandCursor); event->accept(); }
    else QWidget::mousePressEvent(event);
}
void PlaybackSurface::mouseMoveEvent(QMouseEvent* event)
{
    if(m_drag) { m_pan+=event->position()-m_last; m_last=event->position(); update(); event->accept(); }
    else QWidget::mouseMoveEvent(event);
}
void PlaybackSurface::mouseReleaseEvent(QMouseEvent* event)
{
    if(m_drag && event->button()==Qt::MiddleButton) { m_drag=false; unsetCursor(); event->accept(); }
    else QWidget::mouseReleaseEvent(event);
}
void PlaybackSurface::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button()==Qt::LeftButton) { fit(); event->accept(); }
    else QWidget::mouseDoubleClickEvent(event);
}
