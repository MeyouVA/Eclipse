// Eclipse - pencil textures: the strips of grain, made by the program itself.
#include "ui/canvas/PencilTexture.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"

namespace {

int const kLong = 256, kHigh = 64;

// A number from 0 to 1 that depends only on where it is asked for.
double hashAt(int x, int y, unsigned seed)
{
    unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return double(h & 0xffffffu) / double(0xffffff);
}

// Smooth noise over the strip, `cellsX` by `cellsY` lumps; along the strip it comes round again.
bool g_paper = false;       // the grain being made lies flat, as paper: it comes round again across as well

double noiseAt(double u, double v, int cellsX, int cellsY, unsigned seed)
{
    double const x = u * cellsX, y = v * cellsY;
    int const x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    auto at = [&](int ix, int iy) { return hashAt(((ix % cellsX) + cellsX) % cellsX, g_paper ? ((iy % cellsY) + cellsY) % cellsY : iy, seed); };
    double const top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    double const bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

double smooth(double from, double to, double x)
{
    double const t = std::clamp((x - from) / (to - from), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// How opaque the grain of a kind is at a place on the strip (u along, v across, both 0 to 1).
double grainAt(int kind, double u, double v, int px, int py)
{
    double const speck = hashAt(px, py, 17u + unsigned(kind));
    // 0 at the edges of the line, 1 along its centre (paper has no edges)
    double const middle = g_paper ? 1.0 : 1.0 - std::abs(v - 0.5) * 2.0;
    switch (kind) {
    case 1: {
        // Chalk: fine dry grain, a ragged edge.
        double const grain = noiseAt(u, v, 96, 24, 3u), lumps = noiseAt(u, v, 20, 5, 4u);
        double const body = 0.25 + 0.75 * smooth(0.25, 0.6, grain * 0.6 + speck * 0.4 + (lumps - 0.5) * 0.4);
        return body * smooth(0.0, 0.45, middle + (noiseAt(u, v, 48, 6, 5u) - 0.5) * 0.7);
    }
    case 2: {
        // Charcoal: soft and dark in the middle, smudged out to the edges, coarse.
        double const lumps = noiseAt(u, v, 28, 7, 6u), grain = noiseAt(u, v, 80, 20, 7u);
        double const body = 0.45 + 0.55 * smooth(0.3, 0.7, lumps * 0.55 + grain * 0.3 + speck * 0.15);
        return body * smooth(-0.1, 0.8, middle + (lumps - 0.5) * 0.6);
    }
    case 3: {
        // Crayon: waxy - solid where it took, bare paper showing in flecks where it skipped.
        double const paper = noiseAt(u, v, 64, 16, 8u) * 0.7 + speck * 0.3;
        double const body = paper > 0.36 ? 1.0 : 0.12 + 0.5 * paper;
        return body * smooth(0.0, 0.2, middle + (noiseAt(u, v, 40, 4, 9u) - 0.5) * 0.3);
    }
    case 4: {
        // Dry brush: bristles, each a streak running with the line, some of them out of paint.
        double const bristle = noiseAt(u, v, 5, 28, 10u), wear = noiseAt(u, v, 14, 28, 11u);
        double const body = smooth(0.3, 0.55, bristle * 0.75 + wear * 0.25);
        return std::max(0.06, body) * smooth(0.0, 0.15, middle);
    }
    default: {
        // Stipple: dots, closer together along the centre.
        int const cells = 22, rows = 6;
        double const cx = u * cells, cy = v * rows;
        int const ix = int(std::floor(cx)), iy = int(std::floor(cy));
        double best = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int const gx = (((ix + dx) % cells) + cells) % cells;
                int const gy = g_paper ? (((iy + dy) % rows) + rows) % rows : iy + dy;
                if (gy < 0 || gy >= rows)
                    continue;
                double const ox = ix + dx + 0.2 + 0.6 * hashAt(gx, gy, 12u), oy = iy + dy + 0.2 + 0.6 * hashAt(gx, gy, 13u);
                double const radius = 0.22 + 0.2 * hashAt(gx, gy, 14u);
                // (The strip is four times as long as it is high, and has 22 by 6 cells: nearly square ones.)
                double const distance = std::hypot((cx - ox) * (4.0 * rows / cells), cy - oy);
                best = std::max(best, 1.0 - smooth(radius * 0.7, radius, distance));
            }
        }
        return best;
    }
    }
}

QImage makeTile(int kind, bool paper)
{
    QImage image(kLong, kHigh, QImage::Format_ARGB32_Premultiplied);
    g_paper = paper;
    for (int y = 0; y < kHigh; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < kLong; ++x) {
            int const a = int(std::lround(std::clamp(grainAt(kind, (x + 0.5) / kLong, (y + 0.5) / kHigh, x, y), 0.0, 1.0) * 255.0));
            row[x] = qRgba(a, a, a, a);     // white, as opaque as the grain is there
        }
    }
    g_paper = false;
    return image;
}

}

int PencilTexture::count()
{
    return Stroke::kTextureCount;
}

QString PencilTexture::name(int kind)
{
    static char const* const kNames[] = {QT_TRANSLATE_NOOP("PencilTexture", "Solid"), QT_TRANSLATE_NOOP("PencilTexture", "Chalk"),
                                         QT_TRANSLATE_NOOP("PencilTexture", "Charcoal"), QT_TRANSLATE_NOOP("PencilTexture", "Crayon"),
                                         QT_TRANSLATE_NOOP("PencilTexture", "Dry Brush"), QT_TRANSLATE_NOOP("PencilTexture", "Stipple")};
    return QCoreApplication::translate("PencilTexture", kNames[std::clamp(kind, 0, count())]);
}

QImage const& PencilTexture::tile(int kind)
{
    static QImage tiles[Stroke::kTextureCount];
    int const index = std::clamp(kind, 1, count()) - 1;
    if (tiles[index].isNull())
        tiles[index] = makeTile(index + 1, false);
    return tiles[index];
}

QImage const& PencilTexture::paper(int kind)
{
    static QImage tiles[Stroke::kTextureCount];
    int const index = std::clamp(kind, 1, count()) - 1;
    if (tiles[index].isNull())
        tiles[index] = makeTile(index + 1, true);
    return tiles[index];
}

QIcon PencilTexture::icon(int kind)
{
    QPixmap pixmap(72, 20);
    pixmap.fill(QColor(232, 233, 235));
    QPainter p(&pixmap);
    if (kind <= 0) {
        p.fillRect(QRect(4, 4, 64, 12), QColor(20, 20, 22));
    } else {
        // A length of the strip, in ink.
        QImage ink = tile(kind).copy(0, 0, 4 * 48, kHigh);
        QPainter tint(&ink);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(ink.rect(), QColor(20, 20, 22));
        tint.end();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QRect(4, 2, 64, 16), ink);
    }
    return QIcon(pixmap);
}
