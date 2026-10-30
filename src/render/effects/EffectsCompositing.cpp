// Eclipse - compositing effects: channels, crop, mattes, blending, curves, transforms, glue.
#include "render/effects/Effects.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <cmath>

void Effects::channelSelect(QImage& image, int channel)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const c = line[x];
            int const a = qAlpha(c);
            if (channel == 3) {
                line[x] = qRgba(a, a, a, 255);
                continue;
            }
            // (Premultiplied: the channel as it would be with the picture opaque, then as see-through as it was.)
            int const v = a > 0 ? std::min(255, (channel == 0 ? qRed(c) : channel == 1 ? qGreen(c) : qBlue(c)) * 255 / a) : 0;
            int const p = v * a / 255;
            line[x] = qRgba(p, p, p, a);
        }
    }
}

void Effects::crop(QImage& image, QRectF const& keep)
{
    QPainter p(&image);
    p.resetTransform();
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    QPainterPath outside;
    outside.addRect(QRectF(0, 0, image.width(), image.height()));
    QPainterPath inside;
    inside.addRect(keep);
    p.fillPath(outside.subtracted(inside), Qt::black);
}

void Effects::removeTransparency(QImage& image, double threshold)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const edge = std::clamp(int(std::lround(threshold * 255.0)), 0, 255);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const c = line[x];
            int const a = qAlpha(c);
            if (a == 0 || a < edge) {
                line[x] = 0;
                continue;
            }
            line[x] = qRgba(std::min(255, qRed(c) * 255 / a), std::min(255, qGreen(c) * 255 / a), std::min(255, qBlue(c) * 255 / a), 255);
        }
    }
}

void Effects::matteComposite(QImage& image)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(line[x]);
            line[x] = qRgba(a, a, a, a);
        }
    }
}

void Effects::blendOnto(QImage& under, QImage const& top, int mode)
{
    static QPainter::CompositionMode const kModes[Layer::BlendCount] = {
        QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Multiply, QPainter::CompositionMode_Screen,
        QPainter::CompositionMode_Overlay, QPainter::CompositionMode_Darken, QPainter::CompositionMode_Lighten,
        QPainter::CompositionMode_ColorDodge, QPainter::CompositionMode_ColorBurn, QPainter::CompositionMode_HardLight,
        QPainter::CompositionMode_SoftLight, QPainter::CompositionMode_Difference, QPainter::CompositionMode_Exclusion,
        QPainter::CompositionMode_Plus};
    QPainter p(&under);
    p.resetTransform();
    p.setCompositionMode(kModes[std::clamp(mode, 0, int(Layer::BlendCount) - 1)]);
    p.drawImage(0, 0, top);
}

void Effects::variableBlur(QImage& image, QImage const& matte, double radius)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage blurred = image;
    blur(blurred, radius);
    QImage const matte8 = matte.format() == QImage::Format_ARGB32_Premultiplied ? matte : matte.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height() && y < matte.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        QRgb const* const soft = reinterpret_cast<QRgb const*>(blurred.constScanLine(y));
        QRgb const* const by = reinterpret_cast<QRgb const*>(matte8.constScanLine(y));
        for (int x = 0; x < image.width() && x < matte.width(); ++x) {
            // How much: the matte's brightness, where it is (premultiplied: its grey is already as faint as it is).
            int const k = (qRed(by[x]) * 299 + qGreen(by[x]) * 587 + qBlue(by[x]) * 114) / 1000;
            if (k == 0)
                continue;
            QRgb const a = line[x], b = soft[x];
            auto mix = [k](int p, int q) { return p + (q - p) * k / 255; };
            line[x] = qRgba(mix(qRed(a), qRed(b)), mix(qGreen(a), qGreen(b)), mix(qBlue(a), qBlue(b)), mix(qAlpha(a), qAlpha(b)));
        }
    }
}

void Effects::fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width)
{
    fieldChart(image, frame, colour, width, 0, false);
}

void Effects::fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width, int fields, bool opaque)
{
    QPainter p(&image);
    p.resetTransform();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (opaque)
        p.fillRect(frame, Qt::white);
    p.setPen(QPen(colour, std::max(0.5, width)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);
    // Twelve (or sixteen) fields across: half either side of the middle, and as many down as the frame's shape allows.
    int const half = fields == 1 ? 8 : 6;
    QPointF const middle = frame.center();
    double const field = frame.width() / (2.0 * half);
    for (int k = 1; k <= half; ++k) {
        p.drawLine(QPointF(middle.x() - k * field, frame.top()), QPointF(middle.x() - k * field, frame.bottom()));
        p.drawLine(QPointF(middle.x() + k * field, frame.top()), QPointF(middle.x() + k * field, frame.bottom()));
        if (k * field <= frame.height() / 2.0 + 0.5) {
            p.drawLine(QPointF(frame.left(), middle.y() - k * field), QPointF(frame.right(), middle.y() - k * field));
            p.drawLine(QPointF(frame.left(), middle.y() + k * field), QPointF(frame.right(), middle.y() + k * field));
        }
    }
    p.setPen(QPen(colour, std::max(1.0, width * 2.0)));
    p.drawLine(QPointF(middle.x(), frame.top()), QPointF(middle.x(), frame.bottom()));
    p.drawLine(QPointF(frame.left(), middle.y()), QPointF(frame.right(), middle.y()));
    p.setPen(QPen(colour, std::max(0.5, width), Qt::DashLine));
    p.drawLine(frame.topLeft(), frame.bottomRight());
    p.drawLine(frame.topRight(), frame.bottomLeft());
}

QVector<int> Effects::curveLevels(QVector<QPointF> const& points)
{
    QVector<int> levels(256);
    int const n = int(points.size());
    if (n == 0) {
        for (int i = 0; i < 256; ++i)
            levels[i] = i;
        return levels;
    }
    QVector<double> slope(n, 0.0);
    QVector<double> step(std::max(0, n - 1), 0.0);
    for (int k = 0; k + 1 < n; ++k) {
        double const dx = points[k + 1].x() - points[k].x();
        step[k] = dx > 1e-9 ? (points[k + 1].y() - points[k].y()) / dx : 0.0;
    }
    for (int k = 0; k < n; ++k) {
        if (k == 0)
            slope[k] = n > 1 ? step[0] : 0.0;
        else if (k == n - 1)
            slope[k] = step[k - 1];
        else
            slope[k] = step[k - 1] * step[k] <= 0.0 ? 0.0 : (step[k - 1] + step[k]) / 2.0;
    }
    for (int k = 0; k + 1 < n; ++k) {
        if (std::abs(step[k]) < 1e-12) {
            slope[k] = slope[k + 1] = 0.0;
            continue;
        }
        double const a = slope[k] / step[k], b = slope[k + 1] / step[k];
        if (a * a + b * b > 9.0) {
            double const t = 3.0 / std::sqrt(a * a + b * b);
            slope[k] = t * a * step[k];
            slope[k + 1] = t * b * step[k];
        }
    }
    for (int i = 0; i < 256; ++i) {
        double const x = i / 255.0;
        double y = 0.0;
        if (x <= points.front().x()) {
            y = points.front().y();
        } else if (x >= points.back().x()) {
            y = points.back().y();
        } else {
            int k = 0;
            while (k + 1 < n - 1 && x > points[k + 1].x())
                ++k;
            double const h = points[k + 1].x() - points[k].x();
            double const t = h > 1e-9 ? (x - points[k].x()) / h : 0.0;
            double const t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * points[k].y() + (t3 - 2 * t2 + t) * h * slope[k] + (-2 * t3 + 3 * t2) * points[k + 1].y()
                + (t3 - t2) * h * slope[k + 1];
        }
        levels[i] = std::clamp(int(std::lround(y * 255.0)), 0, 255);
    }
    return levels;
}

void Effects::colourCurves(QImage& image, QVector<QPointF> const (&curves)[4], double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    // A table for each colour: the curve of the three together, then its own.
    QVector<int> const all = curveLevels(curves[0]);
    int table[3][256];
    for (int c = 0; c < 3; ++c) {
        QVector<int> const own = curveLevels(curves[c + 1]);
        for (int i = 0; i < 256; ++i) {
            int const to = own[all[i]];
            table[c][i] = std::clamp(int(std::lround(i + (to - i) * amount)), 0, 255);
        }
    }
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            QRgb const px = line[x];
            int const a = qAlpha(px);
            if (a == 0)
                continue;
            // (On the colours as they are, not as they are multiplied by how opaque.)
            QRgb const plain = qUnpremultiply(px);
            line[x] = qPremultiply(qRgba(table[0][qRed(plain)], table[1][qGreen(plain)], table[2][qBlue(plain)], a));
        }
    }
}

void Effects::transformImage(QImage& image, QTransform const& transform)
{
    if (transform.isIdentity())
        return;
    double const dpr = image.devicePixelRatio();
    QImage source = image;
    source.setDevicePixelRatio(1.0);
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(transform);
        p.drawImage(0, 0, source);
    }
    out.setDevicePixelRatio(dpr);
    image = out;
}

void Effects::scaleOutput(QImage& image, double scale)
{
    scale = std::clamp(scale, 0.01, 1.0);
    if (scale >= 1.0 || image.isNull())
        return;
    double const dpr = image.devicePixelRatio();
    QSize const small(std::max(1, int(std::lround(image.width() * scale))), std::max(1, int(std::lround(image.height() * scale))));
    QImage out = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    image = out;
}

void Effects::glue(QImage& under, QImage const& top, double radius)
{
    double const dpr = under.devicePixelRatio();
    if (under.format() != QImage::Format_ARGB32_Premultiplied)
        under = under.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage both = under;
    both.setDevicePixelRatio(1.0);
    {
        QPainter p(&both);
        QImage t = top;
        t.setDevicePixelRatio(1.0);
        p.drawImage(0, 0, t);
    }
    // Where the glue goes: what is painted grown and shrunk again by as much - the gaps narrower than that closed.
    QImage closed = both;
    matteResize(closed, radius / 2.0, false);
    matteResize(closed, radius / 2.0, true);
    // Its colours: those of what is painted nearby, spread out.
    QImage spread = both;
    blur(spread, radius);
    QImage out(both.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < both.height(); ++y) {
        QRgb const* const b = reinterpret_cast<QRgb const*>(both.constScanLine(y));
        QRgb const* const c = reinterpret_cast<QRgb const*>(closed.constScanLine(y));
        QRgb const* const s = reinterpret_cast<QRgb const*>(spread.constScanLine(y));
        QRgb* const o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < both.width(); ++x) {
            int const glueAlpha = std::max(0, qAlpha(c[x]) - qAlpha(b[x]));
            if (glueAlpha == 0 || qAlpha(s[x]) == 0) {
                o[x] = b[x];
                continue;
            }
            // The glue, under what is painted there.
            QRgb const colour = qUnpremultiply(s[x]);
            int const a = qAlpha(b[x]), ga = glueAlpha * (255 - a) / 255;
            auto mix = [&](int painted, int glued) { return std::clamp(painted + glued * ga / 255, 0, 255); };
            o[x] = qRgba(mix(qRed(b[x]), qRed(colour)), mix(qGreen(b[x]), qGreen(colour)), mix(qBlue(b[x]), qBlue(colour)), std::min(255, a + ga));
        }
    }
    out.setDevicePixelRatio(dpr);
    under = out;
}
