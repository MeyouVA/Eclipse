#include "model/geometry/Vectorize.h"

#include <QHash>
#include <QPair>

#include <algorithm>

#include <cmath>

#include "model/geometry/FillRegion.h"

namespace Vectorize {

// Twice the area inside a closed outline, signed by the way it goes round.
static double twiceArea(QPolygonF const& p)
{
    double sum = 0.0;
    for (int i = 0; i < p.size(); ++i) {
        QPointF const a = p[i], b = p[(i + 1) % p.size()];
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return sum;
}

// The outlines of the pixels set in `dark`, which is (w + 2) by (h + 2): the picture with a
// clear pixel all round it, so that every edge has a neighbour.
static QVector<QPolygonF> traceMask(QVector<uchar>& dark, int w, int h, double minimumArea)
{
    QVector<QPolygonF> outlines;
    auto at = [&](int x, int y) -> uchar& { return dark[(y + 1) * (w + 2) + (x + 1)]; };

    // Every side of a dark pixel that faces a clear one is a piece of outline, going round
    // the dark area clockwise.  A piece is known by the corner it starts from and its direction.
    // Directions: 0 right, 1 down, 2 left, 3 up.
    static int const dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    auto key = [&](int x, int y, int direction) { return (qint64(y) * (w + 1) + x) * 4 + direction; };
    QHash<qint64, bool> pieces;     // true once followed
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!at(x, y))
                continue;
            if (!at(x, y - 1))
                pieces.insert(key(x, y, 0), false);          // along the top, going right
            if (!at(x + 1, y))
                pieces.insert(key(x + 1, y, 1), false);      // down the right side
            if (!at(x, y + 1))
                pieces.insert(key(x + 1, y + 1, 2), false);  // along the bottom, going left
            if (!at(x - 1, y))
                pieces.insert(key(x, y + 1, 3), false);      // up the left side
        }
    }

    for (auto it = pieces.begin(); it != pieces.end(); ++it) {
        if (it.value())
            continue;
        // Follow the pieces round until back at the start.
        qint64 const start = it.key();
        int direction = int(start % 4);
        int x = int((start / 4) % (w + 1)), y = int((start / 4) / (w + 1));
        QPolygonF outline;
        for (;;) {
            auto piece = pieces.find(key(x, y, direction));
            if (piece == pieces.end() || piece.value())
                break;
            piece.value() = true;
            // A point at every pixel corner passed; those on a straight run are taken out below.
            outline.push_back(QPointF(x, y));
            x += dx[direction];
            y += dy[direction];
            // At the next corner: turn right if there is a piece that way (this keeps two
            // areas that touch by a corner apart), else straight on, else left.
            int next = -1;
            for (int turn : {1, 0, 3}) {
                int const d = (direction + turn) % 4;
                auto candidate = pieces.find(key(x, y, d));
                if (candidate != pieces.end() && !candidate.value()) {
                    next = d;
                    break;
                }
            }
            if (next < 0)
                break;
            direction = next;
        }
        if (outline.size() < 4 || std::abs(twiceArea(outline)) / 2.0 < minimumArea)
            continue;
        // The steps of the pixels are taken out: the outline may stray most of a pixel.
        QPolygonF const smooth = FillRegion::simplify(outline, 0.75);
        if (smooth.size() >= 3)
            outlines.push_back(smooth);
    }
    return outlines;
}

QVector<QPolygonF> trace(QImage const& source, int threshold, double minimumArea)
{
    if (source.isNull())
        return {};
    QImage const image = source.convertToFormat(QImage::Format_ARGB32);
    int const w = image.width(), h = image.height();
    QVector<uchar> dark((w + 2) * (h + 2), 0);
    for (int y = 0; y < h; ++y) {
        QRgb const* const line = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            dark[(y + 1) * (w + 2) + (x + 1)] = qAlpha(line[x]) >= 128 && qGray(line[x]) < threshold;
    }
    return traceMask(dark, w, h, minimumArea);
}

// ---- in colour -------------------------------------------------------------------------------

namespace {
// A box of colours being cut down: the pixels in it (by their colour, with how many of each).
struct Box {
    QVector<QPair<QRgb, int>> colours;
    int count = 0;
    int widest = 0;     // the channel it is longest in: 0 red, 1 green, 2 blue
    int length = 0;     // how long it is in that channel

    static int channel(QRgb c, int which) { return which == 0 ? qRed(c) : which == 1 ? qGreen(c) : qBlue(c); }
    void measure()
    {
        count = 0;
        int low[3] = {255, 255, 255}, high[3] = {0, 0, 0};
        for (auto const& entry : colours) {
            count += entry.second;
            for (int k = 0; k < 3; ++k) {
                low[k] = std::min(low[k], channel(entry.first, k));
                high[k] = std::max(high[k], channel(entry.first, k));
            }
        }
        widest = 0;
        length = 0;
        for (int k = 0; k < 3; ++k)
            if (high[k] - low[k] > length) {
                length = high[k] - low[k];
                widest = k;
            }
    }
    QColor average() const
    {
        double r = 0, g = 0, b = 0;
        for (auto const& entry : colours) {
            r += double(qRed(entry.first)) * entry.second;
            g += double(qGreen(entry.first)) * entry.second;
            b += double(qBlue(entry.first)) * entry.second;
        }
        double const n = std::max(1, count);
        return QColor(int(std::lround(r / n)), int(std::lround(g / n)), int(std::lround(b / n)));
    }
};

int distance(QRgb a, QColor const& b)
{
    int const dr = qRed(a) - b.red(), dg = qGreen(a) - b.green(), db = qBlue(a) - b.blue();
    return dr * dr + dg * dg + db * db;
}
}  // namespace

QVector<ColourShape> traceColours(QImage const& source, int maxColours, QColor const& paper, double minimumArea)
{
    QVector<ColourShape> shapes;
    if (source.isNull())
        return shapes;
    maxColours = std::clamp(maxColours, 1, 64);
    QImage const image = source.convertToFormat(QImage::Format_ARGB32);
    int const w = image.width(), h = image.height();
    // Ink is what is opaque and not the paper.  A little away from the paper still counts
    // as paper: the soft edge of a scan, the grain of the sheet.
    int const paperNear = 24 * 24 * 3;
    auto ink = [&](QRgb c) { return qAlpha(c) >= 128 && (!paper.isValid() || distance(c, paper) > paperNear); };

    // The colours of the ink, with how many pixels have each (colours are rounded a little,
    // so that a photograph does not have a million of them).
    QHash<QRgb, int> histogram;
    for (int y = 0; y < h; ++y) {
        QRgb const* const line = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (ink(line[x]))
                ++histogram[line[x] & 0xfff8f8f8u];
    }
    if (histogram.isEmpty())
        return shapes;

    // Median cut: the box that is longest is cut in two where half its pixels are, until
    // there are as many boxes as colours wanted.
    QVector<Box> boxes(1);
    for (auto it = histogram.begin(); it != histogram.end(); ++it)
        boxes[0].colours.push_back({it.key(), it.value()});
    boxes[0].measure();
    while (boxes.size() < maxColours) {
        int cut = -1;
        for (int i = 0; i < boxes.size(); ++i)
            if (boxes[i].colours.size() > 1 && boxes[i].length > 12 && (cut < 0 || boxes[i].length > boxes[cut].length))
                cut = i;
        if (cut < 0)
            break;      // every box is one colour, near enough
        Box& box = boxes[cut];
        int const k = box.widest;
        std::sort(box.colours.begin(), box.colours.end(), [k](QPair<QRgb, int> const& a, QPair<QRgb, int> const& b) {
            return Box::channel(a.first, k) < Box::channel(b.first, k);
        });
        int seen = 0, at = 0;
        while (at < box.colours.size() - 1 && seen + box.colours[at].second <= box.count / 2) {
            seen += box.colours[at].second;
            ++at;
        }
        at = std::clamp(at, 1, int(box.colours.size()) - 1);
        Box upper;
        upper.colours = box.colours.mid(at);
        box.colours.resize(at);
        box.measure();
        upper.measure();
        boxes.push_back(upper);
    }
    QVector<QColor> palette;
    for (Box const& box : boxes)
        palette.push_back(box.average());

    // Which colour each pixel of ink is nearest to.
    QVector<qint8> label(w * h, -1);
    QVector<int> pixels(palette.size(), 0);
    QHash<QRgb, qint8> nearest;
    for (int y = 0; y < h; ++y) {
        QRgb const* const line = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!ink(line[x]))
                continue;
            QRgb const rounded = line[x] & 0xfff8f8f8u;
            auto found = nearest.find(rounded);
            if (found == nearest.end()) {
                int best = 0;
                for (int i = 1; i < palette.size(); ++i)
                    if (distance(rounded, palette[i]) < distance(rounded, palette[best]))
                        best = i;
                found = nearest.insert(rounded, qint8(best));
            }
            label[y * w + x] = found.value();
            ++pixels[found.value()];
        }
    }

    // The area of each colour, largest first; each grown by a pixel so that it reaches
    // under the areas beside it.
    QVector<int> order;
    for (int i = 0; i < palette.size(); ++i)
        if (pixels[i] > 0 && pixels[i] >= minimumArea)      // a colour that is only a speck is left out before it is grown
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return pixels[a] > pixels[b]; });
    for (int index : std::as_const(order)) {
        QVector<uchar> mask((w + 2) * (h + 2), 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool mine = label[y * w + x] == index;
                // Grown only over other ink, never out onto the paper: the edge of the picture stays where it is.
                if (!mine && label[y * w + x] >= 0)
                    mine = (x > 0 && label[y * w + x - 1] == index) || (x + 1 < w && label[y * w + x + 1] == index)
                           || (y > 0 && label[(y - 1) * w + x] == index) || (y + 1 < h && label[(y + 1) * w + x] == index);
                mask[(y + 1) * (w + 2) + (x + 1)] = mine;
            }
        }
        ColourShape shape;
        shape.colour = palette[index];
        shape.outlines = traceMask(mask, w, h, minimumArea);
        if (!shape.outlines.isEmpty())
            shapes.push_back(shape);
    }
    return shapes;
}

}
