#pragma once
#include <QImage>
#include <QWidget>

class PlaybackSurface : public QWidget {
public:
    explicit PlaybackSurface(QWidget* parent=nullptr);
    void setImage(QImage const& image);
    QImage image() const { return m_image; }
    void zoom(double factor);
    void fit();
    void actualSize();
protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    double scale() const;
    QImage m_image,m_display;
    double m_scale=1;
    bool m_fit=true,m_drag=false;
    QPointF m_pan,m_last;
};
