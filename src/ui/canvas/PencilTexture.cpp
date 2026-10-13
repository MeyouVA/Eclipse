// Eclipse - pencil textures: the strips of grain, made by the program itself.
#include "ui/canvas/PencilTexture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStandardPaths>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>

#include "model/scene/Scene.h"
#include "model/scene/TextureBank.h"

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
bool g_crisp = false;       // the grain being made has a crisp edge: it frays, but does not fade

// How the grain thins out at the edge of a line: little by little, or - crisp - all there or not at all.
double edgeOf(double fade)
{
    return g_crisp ? (fade >= 0.2 ? 1.0 : 0.0) : fade;
}

bool g_paper = false;       // the grain being made lies flat, as paper: it comes round again across as well

double noiseAt(double u, double v, int cellsX, int cellsY, unsigned seed)
{
    double const x = u * cellsX, y = v * cellsY;
    int const x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - x0, fy = y - y0;
    fx = fx * fx * (3.0 - 2.0 * fx);
    fy = fy * fy * (3.0 - 2.0 * fy);
    // (Paper is two strips long and eight high before it comes round again.)
    int const roundX = g_paper ? cellsX * 2 : cellsX, roundY = cellsY * 8;
    auto at = [&](int ix, int iy) { return hashAt(((ix % roundX) + roundX) % roundX, g_paper ? ((iy % roundY) + roundY) % roundY : iy, seed); };
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
        return body * edgeOf(smooth(0.0, 0.4, middle - noiseAt(u, v, 48, 6, 5u) * 0.45));
    }
    case 2: {
        // Charcoal: soft and dark in the middle, smudged out to the edges, coarse.
        double const lumps = noiseAt(u, v, 28, 7, 6u), grain = noiseAt(u, v, 80, 20, 7u);
        double const body = 0.45 + 0.55 * smooth(0.3, 0.7, lumps * 0.55 + grain * 0.3 + speck * 0.15);
        return body * edgeOf(smooth(0.0, 0.8, middle - lumps * 0.3 - (g_crisp ? speck * 0.25 : 0.0)));
    }
    case 3: {
        // Crayon: waxy - solid where it took, bare paper showing in flecks where it skipped.
        double const paper = noiseAt(u, v, 64, 16, 8u) * 0.7 + speck * 0.3;
        double const body = paper > 0.36 ? 1.0 : 0.12 + 0.5 * paper;
        return body * edgeOf(smooth(0.0, 0.2, middle - noiseAt(u, v, 40, 4, 9u) * 0.25));
    }
    case 4: {
        // Dry brush: bristles, each a streak running with the line, some of them out of paint.
        double const bristle = noiseAt(u, v, 5, 28, 10u), wear = noiseAt(u, v, 14, 28, 11u);
        double const body = smooth(0.3, 0.55, bristle * 0.75 + wear * 0.25);
        return std::max(0.06, body) * edgeOf(smooth(0.0, 0.15, middle - (g_crisp ? wear * 0.12 : 0.0)));
    }
    default: {
        // Stipple: dots, closer together along the centre.
        int const cells = 22, rows = 6;
        double const cx = u * cells, cy = v * rows;
        int const ix = int(std::floor(cx)), iy = int(std::floor(cy));
        double best = 0.0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int const roundX = g_paper ? cells * 2 : cells, roundY = rows * 8;
                int const gx = (((ix + dx) % roundX) + roundX) % roundX;
                int const gy = g_paper ? (((iy + dy) % roundY) + roundY) % roundY : iy + dy;
                if (!g_paper && (gy < 0 || gy >= rows))
                    continue;
                double const ox = ix + dx + 0.2 + 0.6 * hashAt(gx, gy, 12u), oy = iy + dy + 0.2 + 0.6 * hashAt(gx, gy, 13u);
                double const radius = 0.22 + 0.2 * hashAt(gx, gy, 14u);
                // (The strip is four times as long as it is high, and has 22 by 6 cells: nearly square ones.)
                double const distance = std::hypot((cx - ox) * (4.0 * rows / cells), cy - oy);
                best = std::max(best, 1.0 - smooth(radius * 0.7, radius, distance));
            }
        }
        return edgeOf(best) * edgeOf(smooth(0.0, 0.12, middle));
    }
    }
}

QImage makeTile(int kind, bool paper, bool crisp = false)
{
    g_crisp = crisp;
    int const wide = paper ? kLong * 2 : kLong, high = paper ? kHigh * 8 : kHigh;
    QImage image(wide, high, QImage::Format_ARGB32_Premultiplied);
    g_paper = paper;
    for (int y = 0; y < high; ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < wide; ++x) {
            int const a = int(std::lround(std::clamp(grainAt(kind, (x + 0.5) / kLong, (y + 0.5) / kHigh, x, y), 0.0, 1.0) * 255.0));
            row[x] = qRgba(a, a, a, a);     // white, as opaque as the grain is there
        }
    }
    g_paper = false;
    g_crisp = false;
    return image;
}

// The strip of a texture of your own, as it is laid along a line: its grain, thinned out towards the two edges.
QImage stripOf(QImage const& grain, double edge, bool crisp = false)
{
    QImage strip(grain.width(), grain.height(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < grain.height(); ++y) {
        uchar const* const from = grain.constScanLine(y);
        QRgb* const to = reinterpret_cast<QRgb*>(strip.scanLine(y));
        double const middle = 1.0 - std::abs((y + 0.5) / grain.height() - 0.5) * 2.0;
        for (int x = 0; x < grain.width(); ++x) {
            double const ink = from[x] / 255.0;
            // (The thin places give way first, so the edge frays as the grain does.)
            double fade = edge <= 0.0 ? 1.0 : smooth(0.0, 0.1 + 0.3 * edge, middle - edge * 0.45 * (1.0 - ink));
            if (crisp)
                fade = fade >= 0.2 ? 1.0 : 0.0;
            int const alpha = int(std::lround(ink * fade * 255.0));
            to[x] = qRgba(alpha, alpha, alpha, alpha);
        }
    }
    return strip;
}

QImage flatOf(QImage const& grain)
{
    QImage flat(grain.width(), grain.height(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < grain.height(); ++y) {
        uchar const* const from = grain.constScanLine(y);
        QRgb* const to = reinterpret_cast<QRgb*>(flat.scanLine(y));
        for (int x = 0; x < grain.width(); ++x)
            to[x] = qRgba(from[x], from[x], from[x], from[x]);
    }
    return flat;
}

QHash<int, QImage>& ownStrips()
{
    static QHash<int, QImage> strips;
    return strips;
}

QHash<int, QImage>& ownCrispStrips()
{
    static QHash<int, QImage> strips;
    return strips;
}

QHash<int, QImage>& ownPapers()
{
    static QHash<int, QImage> papers;
    return papers;
}

QImage const& none()
{
    static QImage const empty;
    return empty;
}

}

int PencilTexture::count()
{
    return Stroke::kTextureCount;
}

QString PencilTexture::libraryDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/textures");
}

void PencilTexture::loadLibrary()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    QDir const folder(libraryDirectory());
    for (QString const& file : folder.entryList({QStringLiteral("*.png")}, QDir::Files)) {
        QImage const picture(folder.filePath(file));
        if (picture.isNull())
            continue;
        bool ok = false;
        double const edge = picture.text(QStringLiteral("edge")).toDouble(&ok);
        TextureBank::add(picture.text(QStringLiteral("name")), picture, ok ? edge : 0.5);
    }
}

int PencilTexture::keep(QString const& name, QImage const& grain, double edge)
{
    loadLibrary();
    int const id = TextureBank::add(name, grain, edge);
    if (id == 0)
        return 0;
    QDir().mkpath(libraryDirectory());
    exportTo(id, libraryDirectory() + QStringLiteral("/%1.png").arg(id));
    return id;
}

bool PencilTexture::exportTo(int id, QString const& path)
{
    TextureBank::Entry const entry = TextureBank::entry(id);
    if (entry.id == 0)
        return false;
    QImage file = entry.grain;
    file.setText(QStringLiteral("eclipse"), QStringLiteral("texture"));
    file.setText(QStringLiteral("name"), entry.name);
    file.setText(QStringLiteral("edge"), QString::number(entry.edge));
    return file.save(path, "PNG");
}

int PencilTexture::importFile(QString const& path)
{
    QImage const picture(path);
    if (picture.isNull() || picture.text(QStringLiteral("eclipse")) != QStringLiteral("texture"))
        return 0;
    bool ok = false;
    double const edge = picture.text(QStringLiteral("edge")).toDouble(&ok);
    return keep(picture.text(QStringLiteral("name")), picture, ok ? edge : 0.5);
}

void PencilTexture::rename(int id, QString const& name)
{
    loadLibrary();
    if (!TextureBank::contains(id) || name.trimmed().isEmpty())
        return;
    TextureBank::rename(id, name);
    // (In the folder too, when it is kept there.)
    QString const file = libraryDirectory() + QStringLiteral("/%1.png").arg(id);
    if (QFile::exists(file))
        exportTo(id, file);
}

void PencilTexture::discard(int id)
{
    loadLibrary();
    TextureBank::remove(id);
    ownStrips().remove(id);
    ownCrispStrips().remove(id);
    ownPapers().remove(id);
    QFile::remove(libraryDirectory() + QStringLiteral("/%1.png").arg(id));
}

bool PencilTexture::known(int kind)
{
    if (kind >= 0 && kind <= count())
        return true;
    if (TextureBank::contains(kind))
        return true;
    loadLibrary();
    return TextureBank::contains(kind);
}

QVector<int> PencilTexture::kinds()
{
    loadLibrary();
    QVector<int> all;
    for (int kind = 0; kind <= count(); ++kind)
        all.push_back(kind);
    for (TextureBank::Entry const& entry : TextureBank::all())
        all.push_back(entry.id);
    return all;
}

QString PencilTexture::name(int kind)
{
    if (kind >= TextureBank::kFirst)
        return TextureBank::entry(kind).name;
    static char const* const kNames[] = {QT_TRANSLATE_NOOP("PencilTexture", "Solid"), QT_TRANSLATE_NOOP("PencilTexture", "Chalk"),
                                         QT_TRANSLATE_NOOP("PencilTexture", "Charcoal"), QT_TRANSLATE_NOOP("PencilTexture", "Crayon"),
                                         QT_TRANSLATE_NOOP("PencilTexture", "Dry Brush"), QT_TRANSLATE_NOOP("PencilTexture", "Stipple")};
    return QCoreApplication::translate("PencilTexture", kNames[std::clamp(kind, 0, count())]);
}

QImage const& PencilTexture::tile(int kind, bool crisp)
{
    if (kind >= TextureBank::kFirst) {
        QHash<int, QImage>& strips = crisp ? ownCrispStrips() : ownStrips();
        auto found = strips.constFind(kind);
        if (found != strips.constEnd())
            return *found;
        TextureBank::Entry const entry = TextureBank::entry(kind);
        if (entry.id == 0)
            return none();
        return *strips.insert(kind, stripOf(entry.grain, entry.edge, crisp));
    }
    static QImage tiles[2][Stroke::kTextureCount];
    int const index = std::clamp(kind, 1, count()) - 1;
    QImage& tile = tiles[crisp ? 1 : 0][index];
    if (tile.isNull())
        tile = makeTile(index + 1, false, crisp);
    return tile;
}

QImage const& PencilTexture::paper(int kind)
{
    if (kind >= TextureBank::kFirst) {
        auto found = ownPapers().constFind(kind);
        if (found != ownPapers().constEnd())
            return *found;
        TextureBank::Entry const entry = TextureBank::entry(kind);
        if (entry.id == 0)
            return none();
        return *ownPapers().insert(kind, flatOf(entry.grain));
    }
    static QImage tiles[Stroke::kTextureCount];
    int const index = std::clamp(kind, 1, count()) - 1;
    if (tiles[index].isNull())
        tiles[index] = makeTile(index + 1, true);
    return tiles[index];
}

QImage PencilTexture::preview(QImage const& grain, double edge, QSize size)
{
    QImage picture(size, QImage::Format_ARGB32_Premultiplied);
    picture.fill(QColor(232, 233, 235));
    QImage const normal = TextureBank::normalised(grain);
    if (normal.isNull())
        return picture;
    // A line of it, in ink, as thick as the picture allows; and under it a patch lying flat.
    auto inked = [](QImage image) {
        QPainter tint(&image);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(image.rect(), QColor(20, 20, 22));
        tint.end();
        return image;
    };
    QPainter p(&picture);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    int const lineHigh = std::max(8, size.height() * 2 / 5), margin = 6;
    double const scale = double(lineHigh) / normal.height();
    QBrush line(inked(stripOf(normal, edge)));
    line.setTransform(QTransform::fromTranslate(margin, margin).scale(scale, scale));
    p.fillRect(QRect(margin, margin, size.width() - 2 * margin, lineHigh), line);
    QBrush flat(inked(flatOf(normal)));
    flat.setTransform(QTransform::fromTranslate(margin, 2 * margin + lineHigh).scale(scale, scale));
    p.fillRect(QRect(margin, 2 * margin + lineHigh, size.width() - 2 * margin, size.height() - 3 * margin - lineHigh), flat);
    return picture;
}

QImage PencilTexture::grainFromPicture(QImage const& picture, bool lightIsInk)
{
    if (picture.isNull())
        return QImage();
    QImage const colours = picture.convertToFormat(QImage::Format_ARGB32);
    QImage grain(colours.size(), QImage::Format_Grayscale8);
    int most = 0;
    for (int y = 0; y < colours.height(); ++y) {
        QRgb const* const from = reinterpret_cast<QRgb const*>(colours.constScanLine(y));
        uchar* const to = grain.scanLine(y);
        for (int x = 0; x < colours.width(); ++x) {
            // Ink where the picture is dark (or light, when told so) - and none where it is clear.
            int const light = qGray(from[x]);
            int const ink = (lightIsInk ? light : 255 - light) * qAlpha(from[x]) / 255;
            to[x] = uchar(ink);
            most = std::max(most, ink);
        }
    }
    // The darkest of it is solid ink.
    if (most > 0 && most < 255)
        for (int y = 0; y < grain.height(); ++y) {
            uchar* const row = grain.scanLine(y);
            for (int x = 0; x < grain.width(); ++x)
                row[x] = uchar(row[x] * 255 / most);
        }
    return TextureBank::normalised(grain);
}

QImage PencilTexture::makeGrain(int size, int density, int streaks, unsigned seed)
{
    size = std::clamp(size, 1, 100);
    density = std::clamp(density, 0, 100);
    streaks = std::clamp(streaks, 0, 100);
    // How many lumps across the strip, and along it (fewer along, the more it is drawn out into streaks).
    int const across = std::max(2, int(std::lround(34.0 - 31.0 * std::sqrt((size - 1) / 99.0))));
    int const along = std::max(1, int(std::lround(across * 4.0 / (1.0 + streaks * 0.12))));
    // (Noise that comes round again both ways.)
    auto lumps = [seed](double u, double v, int cellsX, int cellsY, unsigned which) {
        double const x = u * cellsX, y = v * cellsY;
        int const x0 = int(std::floor(x)), y0 = int(std::floor(y));
        double fx = x - x0, fy = y - y0;
        fx = fx * fx * (3.0 - 2.0 * fx);
        fy = fy * fy * (3.0 - 2.0 * fy);
        auto at = [&](int ix, int iy) { return hashAt(((ix % cellsX) + cellsX) % cellsX, ((iy % cellsY) + cellsY) % cellsY, seed * 31u + which); };
        double const top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
        double const bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
        return top + (bottom - top) * fy;
    };
    QImage grain(kLong, kHigh, QImage::Format_Grayscale8);
    double const level = 0.82 - 0.64 * density / 100.0;       // noise above this is ink
    for (int y = 0; y < kHigh; ++y) {
        uchar* const row = grain.scanLine(y);
        for (int x = 0; x < kLong; ++x) {
            double const u = (x + 0.5) / kLong, v = (y + 0.5) / kHigh;
            double const value = 0.6 * lumps(u, v, along, across, 1u) + 0.3 * lumps(u, v, along * 2, across * 2, 2u) + 0.1 * hashAt(x, y, seed + 3u);
            row[x] = uchar(std::lround(smooth(level - 0.12, level + 0.12, value) * 255.0));
        }
    }
    return grain;
}

QIcon PencilTexture::icon(int kind)
{
    QPixmap pixmap(72, 20);
    pixmap.fill(QColor(232, 233, 235));
    QPainter p(&pixmap);
    if (kind <= 0 || tile(kind).isNull()) {
        p.fillRect(QRect(4, 4, 64, 12), QColor(20, 20, 22));
    } else {
        // A length of the strip, in ink.
        QImage ink = tile(kind);
        QPainter tint(&ink);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(ink.rect(), QColor(20, 20, 22));
        tint.end();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        QBrush along(ink);
        along.setTransform(QTransform::fromTranslate(4, 2).scale(16.0 / ink.height(), 16.0 / ink.height()));
        p.fillRect(QRect(4, 2, 64, 16), along);
    }
    return QIcon(pixmap);
}
