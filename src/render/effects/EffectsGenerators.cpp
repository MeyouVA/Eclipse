// Eclipse - light, generators and moves: shine, lens flare, burn-in, turbulent noise, grid, refract, shake and quake.
#include "render/effects/Effects.h"
#include "render/effects/EffectsPrivate.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace Effects {

using namespace detail;

namespace {

QString& theSceneName()
{
    static QString name;
    return name;
}

// A painter on the image's own pixels (whatever its device pixel ratio).
struct PixelPainter {
    QImage& image;
    double dpr;
    explicit PixelPainter(QImage& on) : image(on), dpr(on.devicePixelRatio()) { image.setDevicePixelRatio(1.0); }
    ~PixelPainter() { image.setDevicePixelRatio(dpr); }
    PixelPainter(PixelPainter const&) = delete;
    PixelPainter& operator=(PixelPainter const&) = delete;
};

QRgb bilinear(QImage const& source, double x, double y)
{
    int const w = source.width(), h = source.height();
    x = std::clamp(x, 0.0, w - 1.0);
    y = std::clamp(y, 0.0, h - 1.0);
    int const x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    double const fx = x - x0, fy = y - y0;
    QRgb const* const top = reinterpret_cast<QRgb const*>(source.constScanLine(y0));
    QRgb const* const bottom = reinterpret_cast<QRgb const*>(source.constScanLine(y1));
    auto mix = [&](int (*part)(QRgb)) {
        double const a = part(top[x0]) + (part(top[x1]) - part(top[x0])) * fx;
        double const b = part(bottom[x0]) + (part(bottom[x1]) - part(bottom[x0])) * fx;
        return int(std::lround(a + (b - a) * fy));
    };
    return qRgba(mix(qRed), mix(qGreen), mix(qBlue), mix(qAlpha));
}

}  // namespace

void setSceneName(QString const& name)
{
    theSceneName() = name;
}

QString sceneName()
{
    return theSceneName();
}

void shine(QImage& image, QPointF light, double length, double threshold, double falloff, QColor const& colour, double gain, bool composite)
{
    if (length < 1.0)
        return;
    premultiplied(image);
    // What shines: the parts lighter than the threshold, as light as they are past it.
    QImage bright = image;
    perPixel(bright, [&](double& r, double& g, double& b, double& a, int, int) {
        double const l = luminance(r, g, b);
        double const k = l <= threshold ? 0.0 : (l - threshold) / std::max(1e-6, 1.0 - threshold);
        if (colour.isValid()) {
            r = colour.redF();
            g = colour.greenF();
            b = colour.blueF();
        }
        a *= k;
    });
    int const w = image.width(), h = image.height();
    int const samples = 32;
    QImage rays(image.size(), QImage::Format_ARGB32_Premultiplied);
    rays.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(rays.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QPointF const toLight = light - QPointF(x, y);
            double const distance = std::hypot(toLight.x(), toLight.y());
            if (distance < 1e-6)
                continue;
            QPointF const step = toLight / distance * (std::min(length, distance) / samples);
            double r = 0, g = 0, b = 0, a = 0;
            for (int s = 0; s < samples; ++s) {
                // Each point lit by what shines between it and the light, less the further that is.
                double const k = std::max(0.0, 1.0 - falloff * double(s) / samples);
                int const sx = int(std::lround(x + step.x() * s)), sy = int(std::lround(y + step.y() * s));
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    break;
                QRgb const p = reinterpret_cast<QRgb const*>(bright.constScanLine(sy))[sx];
                r += qRed(p) * k;
                g += qGreen(p) * k;
                b += qBlue(p) * k;
                a += qAlpha(p) * k;
            }
            double const by = gain / samples * 2.0;
            int const al = std::clamp(int(std::lround(a * by)), 0, 255);
            auto one = [&](double v) { return std::clamp(int(std::lround(v * by)), 0, al); };
            out[x] = qRgba(one(r), one(g), one(b), al);
        }
    }
    if (!composite) {
        rays.setDevicePixelRatio(image.devicePixelRatio());
        image = rays;
        return;
    }
    PixelPainter keep(image);
    QPainter p(&image);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.drawImage(0, 0, rays);
}

void lensFlare(QImage& image, QPointF light, QRectF const& frame, int kind, QColor const& colour, double intensity, bool screen, double blur)
{
    if (intensity <= 0.0 || frame.isEmpty())
        return;
    premultiplied(image);
    QImage flare(image.size(), QImage::Format_ARGB32_Premultiplied);
    flare.fill(Qt::transparent);
    double const size = frame.width() * 0.08 * std::sqrt(intensity);
    double const strength = std::min(1.0, intensity);
    {
        QPainter p(&flare);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        auto glow = [&](QPointF at, double radius, QColor c, double opacity) {
            QRadialGradient g(at, radius);
            QColor inner = c, outer = c;
            inner.setAlphaF(float(std::clamp(opacity * strength, 0.0, 1.0)));
            outer.setAlphaF(0.0f);
            g.setColorAt(0.0, inner);
            g.setColorAt(1.0, outer);
            p.setBrush(g);
            p.drawEllipse(at, radius, radius);
        };
        glow(light, size * 2.0, colour, 0.9);
        glow(light, size * 0.5, Qt::white, 1.0);
        if (kind == 2) {
            // A streak through the light, across the frame.
            QLinearGradient g(QPointF(light.x() - frame.width() * 0.6, light.y()), QPointF(light.x() + frame.width() * 0.6, light.y()));
            QColor edge = colour, middle = colour;
            edge.setAlphaF(0.0f);
            middle.setAlphaF(float(0.8 * strength));
            g.setColorAt(0.0, edge);
            g.setColorAt(0.5, middle);
            g.setColorAt(1.0, edge);
            p.setBrush(g);
            p.drawRect(QRectF(light.x() - frame.width() * 0.6, light.y() - size * 0.06, frame.width() * 1.2, size * 0.12));
        }
        // The ghosts along the line from the light through the middle of the frame.
        QPointF const axis = frame.center() - light;
        double const along[5] = {0.5, 0.9, 1.25, 1.6, 2.0}, sizes[5] = {0.35, 0.6, 0.25, 0.9, 0.5};
        for (int k = 0; k < 5; ++k) {
            QColor c = QColor::fromHsvF(std::fmod(std::max(0.0f, colour.hsvHueF()) + 0.12f * k, 1.0f), 0.5f, 1.0f);
            QPointF const at = light + axis * along[k];
            double const radius = size * sizes[k];
            if (kind == 1) {
                QPolygonF hexagon;
                for (int corner = 0; corner < 6; ++corner) {
                    double const turn = 3.14159265358979323846 / 3.0 * corner;
                    hexagon << at + QPointF(std::cos(turn), std::sin(turn)) * radius;
                }
                c.setAlphaF(float(0.18 * strength));
                p.setBrush(c);
                p.drawPolygon(hexagon);
            } else {
                glow(at, radius, c, 0.3);
            }
        }
    }
    if (blur > 0.0)
        Effects::blur(flare, blur * size * 0.3);
    PixelPainter keep(image);
    QPainter p(&image);
    p.setCompositionMode(screen ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_SourceOver);
    p.drawImage(0, 0, flare);
}

QString burnInText(QString const& text, int frame, double fps, QString const& scene)
{
    int const rate = std::max(1, int(std::lround(fps)));
    int const f = std::max(0, frame);
    QString const code = QStringLiteral("%1:%2:%3:%4")
                             .arg(f / rate / 3600, 2, 10, QLatin1Char('0'))
                             .arg(f / rate / 60 % 60, 2, 10, QLatin1Char('0'))
                             .arg(f / rate % 60, 2, 10, QLatin1Char('0'))
                             .arg(f % rate, 2, 10, QLatin1Char('0'));
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('%') || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        QChar const c = text[++i];
        out += c == QLatin1Char('f') ? QString::number(frame + 1) : c == QLatin1Char('s') ? scene : c == QLatin1Char('t') ? code
             : c == QLatin1Char('%') ? QStringLiteral("%") : c == QLatin1Char('e') || c == QLatin1Char('j') ? QString() : QStringLiteral("%") + c;
    }
    return out;
}

void burnIn(QImage& image, QString const& text, QRectF const& frame, double size, QColor const& colour, bool box, int place)
{
    if (text.isEmpty() || size < 1.0 || frame.isEmpty())
        return;
    premultiplied(image);
    PixelPainter keep(image);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font = p.font();
    font.setPixelSize(std::max(1, int(std::lround(size))));
    p.setFont(font);
    QFontMetricsF const metrics(font);
    QRectF area = metrics.boundingRect(QRectF(0, 0, frame.width(), frame.height()), Qt::AlignLeft | Qt::AlignTop, text);
    double const margin = size * 0.5;
    QPointF const at = place == 1 ? QPointF(frame.right() - margin - area.width(), frame.bottom() - margin - area.height())
                       : place == 2 ? QPointF(frame.left() + margin, frame.top() + margin)
                       : place == 3 ? QPointF(frame.right() - margin - area.width(), frame.top() + margin)
                       : place == 4 ? frame.center() - QPointF(area.width() / 2.0, area.height() / 2.0)
                                    : QPointF(frame.left() + margin, frame.bottom() - margin - area.height());
    area.moveTopLeft(at);
    if (box)
        p.fillRect(area.adjusted(-size * 0.25, -size * 0.1, size * 0.25, size * 0.1), QColor(0, 0, 0, 160));
    p.setPen(colour);
    p.drawText(area, Qt::AlignLeft | Qt::AlignTop, text);
}

void turbulentNoise(QImage& image, QRectF const& frame, double pixelsPerUnit, Effect const& e, int at, double opacity)
{
    if (frame.isEmpty() || opacity <= 0.0)
        return;
    premultiplied(image);
    // Worked out on every other pixel (the noise is smooth), then laid over the frame.
    int const step = 2;
    QRect const box = frame.toAlignedRect();
    int const w = std::max(1, box.width() / step), h = std::max(1, box.height() / step);
    QImage noise(w, h, QImage::Format_ARGB32_Premultiplied);
    double const scale = std::clamp(e.value("frequency"), 0.01, 50.0) / 100.0 / std::max(1e-6, pixelsPerUnit) * step;
    int const octaves = std::clamp(int(e.value("octaves")), 1, 8);
    double const gain = e.value("gain"), lacunarity = e.value("lacunarity");
    bool const turbulent = e.value("fractal") >= 1.0;
    double const time = at * e.value("evolution");
    quint32 const seed = quint32(e.value("seed"));
    int const whole = int(std::floor(time));
    double const part = time - whole, blend = part * part * (3.0 - 2.0 * part);
    QColor const from = e.colourValue("colour1"), to = e.colourValue("colour2");
    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(noise.scanLine(y));
        for (int x = 0; x < w; ++x) {
            double sum = 0.0, amplitude = 1.0, total = 0.0, fx = x * scale, fy = y * scale;
            for (int o = 0; o < octaves; ++o) {
                quint32 const s = seed * 131u + quint32(o) * 977u;
                double n = smooth(fx, fy, s + quint32(whole)) * (1.0 - blend) + smooth(fx, fy, s + quint32(whole) + 1u) * blend;
                if (turbulent)
                    n = std::abs(2.0 * n - 1.0);
                sum += n * amplitude;
                total += amplitude;
                amplitude *= gain;
                fx *= lacunarity;
                fy *= lacunarity;
            }
            double const k = std::clamp(sum / std::max(1e-9, total), 0.0, 1.0);
            auto mix = [k](double a, double b) { return a + (b - a) * k; };
            double const a = mix(from.alphaF(), to.alphaF()) * std::clamp(opacity, 0.0, 1.0);
            auto one = [a](double v) { return std::clamp(int(std::lround(v * a * 255.0)), 0, 255); };
            row[x] = qRgba(one(mix(from.redF(), to.redF())), one(mix(from.greenF(), to.greenF())), one(mix(from.blueF(), to.blueF())), int(std::lround(a * 255.0)));
        }
    }
    // What comes in lies over the noise.
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QRectF(box), noise);
        QImage over = image;
        over.setDevicePixelRatio(1.0);
        p.drawImage(0, 0, over);
    }
    out.setDevicePixelRatio(image.devicePixelRatio());
    image = out;
}

void grid(QImage& image, QRectF const& frame, int columns, bool fitVertical, bool text, QColor const& colour, double width)
{
    if (frame.isEmpty() || columns < 1)
        return;
    premultiplied(image);
    PixelPainter keep(image);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(colour, std::max(0.5, width)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(frame);
    double const cell = (fitVertical ? frame.height() : frame.width()) / columns;
    if (cell < 2.0)
        return;
    QFont font = p.font();
    font.setPixelSize(std::max(6, int(cell * 0.3)));
    p.setFont(font);
    int k = 0;
    for (double x = frame.left(); x <= frame.right() + 0.5; x += cell, ++k) {
        p.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
        if (text && x < frame.right() - cell * 0.5)
            p.drawText(QRectF(x, frame.top(), cell, cell * 0.5), Qt::AlignCenter, QString::number(k + 1));
    }
    k = 0;
    for (double y = frame.top(); y <= frame.bottom() + 0.5; y += cell, ++k) {
        p.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        if (text && k > 0 && y < frame.bottom() - cell * 0.5)
            p.drawText(QRectF(frame.left(), y, cell * 0.5, cell), Qt::AlignCenter, QString::number(k + 1));
    }
}

void refract(QImage& image, QImage const& height, double magnitude, double smoothing, double offset)
{
    if (magnitude <= 0.0 || height.isNull())
        return;
    premultiplied(image);
    int const w = image.width(), h = image.height();
    QImage bumps = height.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    blur(bumps, smoothing);
    QVector<float> level(qsizetype(w) * h, 0.0f);
    for (int y = 0; y < h && y < bumps.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(bumps.constScanLine(y));
        for (int x = 0; x < w && x < bumps.width(); ++x)
            level[qsizetype(y) * w + x] = float(luminance(qRed(row[x]), qGreen(row[x]), qBlue(row[x])) / 255.0);
    }
    // The slope everywhere, and the steepest, so that the steepest bends by the magnitude.
    QVector<QPointF> slope(qsizetype(w) * h);
    double steepest = 1e-9;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto at = [&](int px, int py) { return double(level[qsizetype(std::clamp(py, 0, h - 1)) * w + std::clamp(px, 0, w - 1)]); };
            QPointF const s((at(x + 1, y) - at(x - 1, y)) / 2.0, (at(x, y + 1) - at(x, y - 1)) / 2.0);
            slope[qsizetype(y) * w + x] = s;
            steepest = std::max(steepest, std::hypot(s.x(), s.y()));
        }
    QImage const source = image;
    double const by = magnitude / steepest, shift = offset * magnitude;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QPointF const s = slope[qsizetype(y) * w + x];
            out[x] = bilinear(source, x + s.x() * by + shift, y + s.y() * by + shift);
        }
    }
}

QTransform shaken(Effect const& e, int at, double amount, double pixelsPerUnit, QPointF pivot)
{
    quint32 const seed = quint32(e.value("seed")) * 7919u + 17u;
    double dx = 0.0, dy = 0.0, turn = 0.0;
    auto wobble = [&](quint32 key, quint32 which) { return random01(key, which, seed) * 2.0 - 1.0; };
    if (e.type == Effect::Shake) {
        // Random places at a steady rate, glided between.
        double const t = at * std::clamp(e.value("frequency"), 0.01, 1.0);
        quint32 const key = quint32(qint64(std::floor(t)));
        double const part = t - std::floor(t), k = part * part * (3.0 - 2.0 * part);
        auto glide = [&](quint32 which) { return wobble(key, which) * (1.0 - k) + wobble(key + 1, which) * k; };
        dx = glide(1) * e.value("x");
        dy = glide(2) * e.value("y");
        turn = glide(3) * e.value("rotation");
        pivot += QPointF(e.value("pivotX"), e.value("pivotY")) * pixelsPerUnit;
    } else {
        // A jolt held for so many frames, then another.
        quint32 const key = quint32(at / std::max(1, int(e.value("hold"))));
        dx = e.flag("applyX") ? wobble(key, 1) * e.value("move") : 0.0;
        dy = e.flag("applyY") ? wobble(key, 2) * e.value("move") : 0.0;
        turn = wobble(key, 3) * e.value("rotation");
    }
    QTransform rotation;
    rotation.rotate(turn * amount);
    return QTransform::fromTranslate(-pivot.x(), -pivot.y()) * rotation
           * QTransform::fromTranslate(pivot.x() + dx * amount * pixelsPerUnit, pivot.y() + dy * amount * pixelsPerUnit);
}

}  // namespace Effects
