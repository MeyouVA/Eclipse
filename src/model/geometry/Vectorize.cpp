#include "model/geometry/Vectorize.h"

#include <QHash>
#include <QPair>
#include <QSet>

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

static QVector<QPolygonF> traceMask(QVector<uchar>& dark, int w, int h, double minimumArea)
{
    QVector<QPolygonF> outlines;
    auto at = [&](int x, int y) -> uchar& { return dark[(y + 1) * (w + 2) + (x + 1)]; };

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
    int const paperNear = 24 * 24 * 3;
    auto ink = [&](QRgb c) { return qAlpha(c) >= 128 && (!paper.isValid() || distance(c, paper) > paperNear); };

    QHash<QRgb, int> histogram;
    for (int y = 0; y < h; ++y) {
        QRgb const* const line = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (ink(line[x]))
                ++histogram[line[x] & 0xfff8f8f8u];
    }
    if (histogram.isEmpty())
        return shapes;

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

// ---- centre lines ----------------------------------------------------------------------------

static QPolygonF fewerPoints(QPolygonF const& line, double tolerance)
{
    if (line.size() < 3)
        return line;
    QVector<bool> keep(line.size(), false);
    keep[0] = keep[line.size() - 1] = true;
    QVector<QPair<int, int>> spans = {{0, int(line.size()) - 1}};
    while (!spans.isEmpty()) {
        QPair<int, int> const span = spans.takeLast();
        QPointF const a = line[span.first], b = line[span.second];
        double const length = std::hypot(b.x() - a.x(), b.y() - a.y());
        double most = 0.0;
        int at = -1;
        for (int i = span.first + 1; i < span.second; ++i) {
            QPointF const d = line[i] - a;
            double const off = length > 1e-9 ? std::abs(d.x() * (b.y() - a.y()) - d.y() * (b.x() - a.x())) / length : std::hypot(d.x(), d.y());
            if (off > most) {
                most = off;
                at = i;
            }
        }
        if (at >= 0 && most > tolerance) {
            keep[at] = true;
            spans.push_back({span.first, at});
            spans.push_back({at, span.second});
        }
    }
    QPolygonF kept;
    for (int i = 0; i < line.size(); ++i)
        if (keep[i])
            kept << line[i];
    return kept;
}

QVector<CentreLine> centreLines(QImage const& source, int threshold, int minimumLength)
{
    if (source.isNull())
        return {};
    QImage const image = source.convertToFormat(QImage::Format_ARGB32);
    int const w = image.width(), h = image.height(), across = w + 2;
    // The picture with a clear pixel all round it, so that every pixel has its eight neighbours.
    QVector<uchar> thin(across * (h + 2), 0);
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            thin[(y + 1) * across + (x + 1)] = qAlpha(row[x]) >= 128 && qGray(row[x]) < threshold;
    }
    QVector<int> depth(thin.size(), 0);
    for (int y = 1; y <= h; ++y)
        for (int x = 1; x <= w; ++x)
            if (thin[y * across + x])
                depth[y * across + x] = std::min(depth[(y - 1) * across + x], depth[y * across + x - 1]) + 1;
    for (int y = h; y >= 1; --y)
        for (int x = w; x >= 1; --x)
            if (thin[y * across + x])
                depth[y * across + x] = std::min(depth[y * across + x], std::min(depth[(y + 1) * across + x], depth[y * across + x + 1]) + 1);

    QVector<int> gone;
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            gone.clear();
            for (int y = 1; y <= h; ++y) {
                for (int x = 1; x <= w; ++x) {
                    int const i = y * across + x;
                    if (!thin[i])
                        continue;
                    // Its neighbours, clockwise from the one above.
                    int const n[8] = {thin[i - across], thin[i - across + 1], thin[i + 1], thin[i + across + 1],
                                      thin[i + across], thin[i + across - 1], thin[i - 1], thin[i - across - 1]};
                    int count = 0, turns = 0;
                    for (int k = 0; k < 8; ++k) {
                        count += n[k];
                        turns += (!n[k] && n[(k + 1) % 8]) ? 1 : 0;
                    }
                    if (count < 2 || count > 6 || turns != 1)
                        continue;
                    if (pass == 0 ? (n[0] && n[2] && n[4]) || (n[2] && n[4] && n[6]) : (n[0] && n[2] && n[6]) || (n[0] && n[4] && n[6]))
                        continue;
                    gone.push_back(i);
                }
            }
            for (int i : std::as_const(gone))
                thin[i] = 0;
            changed = changed || !gone.isEmpty();
        }
    }

    auto neighbours = [&](int i) {
        QVector<int> found;
        static int const beside[4] = {1, 0, -1, 0}, below[4] = {0, 1, 0, -1};
        for (int k = 0; k < 4; ++k)
            if (thin[i + below[k] * across + beside[k]])
                found.push_back(i + below[k] * across + beside[k]);
        static int const cornerX[4] = {1, 1, -1, -1}, cornerY[4] = {1, -1, 1, -1};
        for (int k = 0; k < 4; ++k)
            if (thin[i + cornerY[k] * across + cornerX[k]] && !thin[i + cornerX[k]] && !thin[i + cornerY[k] * across])
                found.push_back(i + cornerY[k] * across + cornerX[k]);
        return found;
    };
    QVector<uchar> ways(thin.size(), 0);        // how many neighbours each has: two along a line, one at a loose end, more at a meeting
    for (int i = 0; i < thin.size(); ++i)
        if (thin[i])
            ways[i] = uchar(neighbours(i).size());
    QSet<quint64> walked;
    auto step = [](int a, int b) { return (quint64(std::min(a, b)) << 32) | quint64(std::max(a, b)); };
    auto follow = [&](int from, int first, bool* closed) {
        QVector<int> path = {from};
        int before = from, now = first;
        walked.insert(step(from, first));
        *closed = false;
        for (;;) {
            if (now == from) {
                *closed = true;
                break;
            }
            path.push_back(now);
            if (ways[now] != 2)
                break;          // an end, or a meeting
            QVector<int> const next = neighbours(now);
            int const on = next[0] == before ? next[1] : next[0];
            if (walked.contains(step(now, on)))
                break;
            walked.insert(step(now, on));
            before = now;
            now = on;
        }
        return path;
    };
    QVector<QPair<QVector<int>, bool>> paths;
    // From every end and every meeting, along each line that leaves it ...
    for (int i = 0; i < thin.size(); ++i) {
        if (!thin[i] || ways[i] == 2 || ways[i] == 0)
            continue;
        for (int first : neighbours(i)) {
            if (walked.contains(step(i, first)))
                continue;
            bool closed = false;
            QVector<int> const path = follow(i, first, &closed);
            paths.push_back({path, closed});
        }
    }
    // ... and the rings that have neither.
    for (int i = 0; i < thin.size(); ++i) {
        if (!thin[i] || ways[i] != 2)
            continue;
        QVector<int> const next = neighbours(i);
        if (walked.contains(step(i, next[0])) || walked.contains(step(i, next[1])))
            continue;
        bool closed = false;
        QVector<int> const path = follow(i, next[0], &closed);
        paths.push_back({path, closed});
    }

    QVector<CentreLine> lines;
    for (auto const& [path, closed] : std::as_const(paths)) {
        // A whisker - short, with a loose end - is the thinning's doing, not the drawing's.
        bool const loose = ways[path.first()] == 1 || ways[path.last()] == 1;
        if (path.size() < 2 || (!closed && loose && path.size() < minimumLength))
            continue;
        CentreLine line;
        line.closed = closed;
        double thickness = 0.0;
        for (int i : path) {
            line.points << QPointF(i % across - 1 + 0.5, i / across - 1 + 0.5);
            thickness += 2.0 * depth[i] - 0.5;
        }
        line.width = std::max(1.0, thickness / path.size());
        line.points = fewerPoints(line.points, 0.75);
        lines.push_back(line);
    }
    return lines;
}

}
