// Eclipse - shading what is drawn as though it stood out of the page: its normals (Normal Map) worked out from its
// shape, and those lit by a light (Light Shader).
#include "render/effects/Effects.h"
#include "render/effects/EffectsPrivate.h"

#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace Effects {

using namespace detail;

namespace {

// How high each point stands: its opacity, softened `bevel` pixels, so the edges slope and the middles are flat.
QVector<float> heights(QImage const& image, double bevel)
{
    int const w = image.width(), h = image.height();
    QVector<float> height(qsizetype(w) * h);
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            height[qsizetype(y) * w + x] = qAlpha(row[x]) / 255.0f;
    }
    int const r = std::max(1, int(std::lround(bevel / 2.0)));
    // (Three running means across and down: near enough a soft blur.)
    QVector<float> line;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            float* const row = height.data() + qsizetype(y) * w;
            line = QVector<float>(row, row + w);
            double sum = 0.0;
            for (int x = -r; x <= r; ++x)
                sum += line[std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                row[x] = float(sum / (2 * r + 1));
                sum += line[std::clamp(x + r + 1, 0, w - 1)] - line[std::clamp(x - r, 0, w - 1)];
            }
        }
        for (int x = 0; x < w; ++x) {
            line.resize(h);
            for (int y = 0; y < h; ++y)
                line[y] = height[qsizetype(y) * w + x];
            double sum = 0.0;
            for (int y = -r; y <= r; ++y)
                sum += line[std::clamp(y, 0, h - 1)];
            for (int y = 0; y < h; ++y) {
                height[qsizetype(y) * w + x] = float(sum / (2 * r + 1));
                sum += line[std::clamp(y + r + 1, 0, h - 1)] - line[std::clamp(y - r, 0, h - 1)];
            }
        }
    }
    return height;
}

// The normal at a point (x across to the right, y up the picture, z out of it), the slope `depth` times as steep.
QVector3D normalAt(QVector<float> const& height, int w, int h, int x, int y, double depth, double bevel)
{
    auto at = [&](int px, int py) { return height[qsizetype(std::clamp(py, 0, h - 1)) * w + std::clamp(px, 0, w - 1)]; };
    // (Steeper for a narrower bevel: a slope over `bevel` pixels rises the whole height.)
    double const steep = depth * std::max(1.0, bevel) / 2.0;
    double const dx = (at(x + 1, y) - at(x - 1, y)) * steep;
    double const dy = (at(x, y + 1) - at(x, y - 1)) * steep;
    return QVector3D(float(-dx), float(dy), 1.0f).normalized();
}

}  // namespace

void normalMap(QImage& image, double bevel, double depth, bool flipY)
{
    premultiplied(image);
    int const w = image.width(), h = image.height();
    QVector<float> const height = heights(image, bevel);
    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            QVector3D n = normalAt(height, w, h, x, y, depth, bevel);
            if (flipY)
                n.setY(-n.y());
            auto one = [a](float v) { return std::clamp(int(std::lround((v * 0.5 + 0.5) * a)), 0, a); };
            row[x] = qRgba(one(n.x()), one(n.y()), one(n.z()), a);
        }
    }
}

void lightShader(QImage& image, double bevel, double depth, double angle, double elevation, QColor const& light, double intensity, double ambient,
                 double specular, double shininess)
{
    premultiplied(image);
    int const w = image.width(), h = image.height();
    QVector<float> const height = heights(image, bevel);
    // Where the light comes from: round from the right (degrees, anticlockwise) and up off the page.
    double const az = angle * 3.14159265358979323846 / 180.0, el = std::clamp(elevation, 1.0, 90.0) * 3.14159265358979323846 / 180.0;
    QVector3D const towards = QVector3D(float(std::cos(el) * std::cos(az)), float(std::cos(el) * std::sin(az)), float(std::sin(el))).normalized();
    QVector3D const half = (towards + QVector3D(0, 0, 1)).normalized();
    // (A flat part keeps its colour: lit as much as the page facing the light is.)
    double const flat = std::max(0.05, double(towards.z()));
    double const flatShine = std::pow(std::max(0.0, double(half.z())), shininess);
    double const lr = light.redF(), lg = light.greenF(), lb = light.blueF();
    for (int y = 0; y < h; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            QVector3D const n = normalAt(height, w, h, x, y, depth, bevel);
            double const facing = std::max(0.0, double(QVector3D::dotProduct(n, towards))) / flat;
            double const lit = std::max(ambient, 1.0 + (facing - 1.0) * intensity);
            double const shine = specular * std::max(0.0, std::pow(std::max(0.0, double(QVector3D::dotProduct(n, half))), shininess) - flatShine);
            auto one = [&](int v, double tone) { return std::clamp(int(std::lround(v * lit * (1.0 + (tone - 1.0) * intensity) + a * shine * tone)), 0, a); };
            row[x] = qRgba(one(qRed(row[x]), lr), one(qGreen(row[x]), lg), one(qBlue(row[x]), lb), a);
        }
    }
}

}  // namespace Effects
