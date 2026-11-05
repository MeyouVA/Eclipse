// Eclipse - vectorising with options.
#include "model/geometry/VectorizeOptions.h"

#include "model/geometry/Morph.h"
#include "model/geometry/Vectorize.h"

#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace Vectorize {

namespace {

// ---- masks: a cell for each pixel, row by row ------------------------------------------------

struct Mask {
    int w = 0, h = 0;
    QVector<uchar> bits;
    Mask() = default;
    Mask(int width, int height) : w(width), h(height), bits(width * height, 0) {}
    uchar& at(int x, int y) { return bits[y * w + x]; }
    uchar at(int x, int y) const { return bits[y * w + x]; }
    bool any() const { return std::any_of(bits.begin(), bits.end(), [](uchar b) { return b != 0; }); }
};

// Made fatter (`grow`) or thinner by `r` pixels: a square round each pixel, along the rows then down the columns.
Mask spread(Mask const& m, int r, bool grow)
{
    if (r <= 0)
        return m;
    Mask across(m.w, m.h), out(m.w, m.h);
    for (int y = 0; y < m.h; ++y) {
        // (a running count of the set cells within r of each)
        int count = 0;
        for (int x = 0; x <= std::min(r - 1, m.w - 1); ++x)
            count += m.at(x, y) ? 1 : 0;
        for (int x = 0; x < m.w; ++x) {
            if (x + r < m.w)
                count += m.at(x + r, y) ? 1 : 0;
            if (x - r - 1 >= 0)
                count -= m.at(x - r - 1, y) ? 1 : 0;
            int const span = std::min(m.w - 1, x + r) - std::max(0, x - r) + 1;
            across.at(x, y) = grow ? count > 0 : count == span;
        }
    }
    for (int x = 0; x < m.w; ++x) {
        int count = 0;
        for (int y = 0; y <= std::min(r - 1, m.h - 1); ++y)
            count += across.at(x, y) ? 1 : 0;
        for (int y = 0; y < m.h; ++y) {
            if (y + r < m.h)
                count += across.at(x, y + r) ? 1 : 0;
            if (y - r - 1 >= 0)
                count -= across.at(x, y - r - 1) ? 1 : 0;
            int const span = std::min(m.h - 1, y + r) - std::max(0, y - r) + 1;
            out.at(x, y) = grow ? count > 0 : count == span;
        }
    }
    return out;
}

Mask closed(Mask const& m, int r) { return spread(spread(m, r, true), r, false); }
Mask opened(Mask const& m, int r) { return spread(spread(m, r, false), r, true); }

// The pieces of the cells that are `value`, joined side by side: each cell's piece (-1: not `value`), and each piece's size
// and whether it reaches the edge.
int pieces(Mask const& m, uchar value, QVector<int>& label, QVector<int>& size, QVector<bool>& edge)
{
    label = QVector<int>(m.w * m.h, -1);
    size.clear();
    edge.clear();
    QVector<int> stack;
    for (int start = 0; start < m.w * m.h; ++start) {
        if ((m.bits[start] != 0) != (value != 0) || label[start] >= 0)
            continue;
        int const id = int(size.size());
        size.push_back(0);
        edge.push_back(false);
        stack = {start};
        label[start] = id;
        while (!stack.isEmpty()) {
            int const c = stack.takeLast();
            ++size[id];
            int const x = c % m.w, y = c / m.w;
            if (x == 0 || y == 0 || x == m.w - 1 || y == m.h - 1)
                edge[id] = true;
            int const next[4] = {x > 0 ? c - 1 : -1, x + 1 < m.w ? c + 1 : -1, y > 0 ? c - m.w : -1, y + 1 < m.h ? c + m.w : -1};
            for (int n : next)
                if (n >= 0 && label[n] < 0 && (m.bits[n] != 0) == (value != 0)) {
                    label[n] = id;
                    stack.push_back(n);
                }
        }
    }
    return int(size.size());
}

// Specks smaller than `area` left out; holes smaller than `holes` filled.
void clean(Mask& m, double area, double holes)
{
    QVector<int> label, size;
    QVector<bool> edge;
    if (area > 0.0) {
        pieces(m, 1, label, size, edge);
        for (int i = 0; i < m.bits.size(); ++i)
            if (label[i] >= 0 && size[label[i]] < area)
                m.bits[i] = 0;
    }
    if (holes > 0.0) {
        pieces(m, 0, label, size, edge);
        for (int i = 0; i < m.bits.size(); ++i)
            if (label[i] >= 0 && !edge[label[i]] && size[label[i]] < holes)
                m.bits[i] = 1;
    }
}

Mask allHolesFilled(Mask m)
{
    QVector<int> label, size;
    QVector<bool> edge;
    pieces(m, 0, label, size, edge);
    for (int i = 0; i < m.bits.size(); ++i)
        if (label[i] >= 0 && !edge[label[i]])
            m.bits[i] = 1;
    return m;
}

QPolygonF smoothedRun(QPolygonF line, int passes)
{
    if (passes > 0 && line.size() >= 3) {
        // A point every pixel along it first, so that smoothing rounds the corners off a little and does not cut across.
        QPolygonF dense;
        int const n = int(line.size());
        for (int i = 0; i < n; ++i) {
            QPointF const a = line[i], b = line[(i + 1) % n];
            int const steps = std::max(1, int(std::ceil(QLineF(a, b).length())));
            for (int k = 0; k < steps; ++k)
                dense << a + (b - a) * (double(k) / steps);
        }
        line = dense;
    }
    for (int pass = 0; pass < passes && line.size() >= 3; ++pass) {
        QPolygonF out;
        int const n = int(line.size());
        for (int i = 0; i < n; ++i)
            out << (line[(i + n - 1) % n] + line[i] * 2.0 + line[(i + 1) % n]) / 4.0;
        line = out;
    }
    return line;
}

QVector<QPolygonF> outlinesOf(Mask const& m, int smooth, double fit)
{
    QVector<QPolygonF> out;
    for (QPolygonF const& raw : traceBits(m.bits, m.w, m.h, 1.0)) {
        QPolygonF line = smoothedRun(raw, smooth);
        if (fit > 0.0 && line.size() > 8) {
            QPolygonF looped = line;
            looped << line.first();
            line = simplify(looped, fit);
            if (line.size() > 1 && line.first() == line.last())
                line.removeLast();
        }
        if (line.size() >= 3)
            out.push_back(line);
    }
    return out;
}

double lightnessOf(QRgb c) { return qGray(c) / 255.0; }

// The paper and the darkest line of a picture (a little in from either end, so that a speck does not count).
void levelsOf(QImage const& image, double& black, double& white)
{
    QVector<int> counts(256, 0);
    int total = 0;
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) >= 128) {
                ++counts[qGray(row[x])];
                ++total;
            }
    }
    black = 0.0;
    white = 1.0;
    if (total == 0)
        return;
    int seen = 0, low = 0, high = 255;
    for (int g = 0; g < 256; ++g) {
        seen += counts[g];
        if (seen > total / 1000) {
            low = g;
            break;
        }
    }
    seen = 0;
    for (int g = 255; g >= 0; --g) {
        seen += counts[g];
        if (seen > total / 100) {
            high = g;
            break;
        }
    }
    if (high - low >= 32) {
        black = low / 255.0;
        white = high / 255.0;
    }
}

// Which of red, green and blue (0, 1, 2) a pixel is coloured with, or -1.
int colouredWith(QRgb c, Options const& o)
{
    int const r = qRed(c), g = qGreen(c), b = qBlue(c);
    int const most = std::max({r, g, b}), least = std::min({r, g, b});
    int const channel = most == r ? 0 : most == g ? 1 : 2;
    int const second = channel == 0 ? std::max(g, b) : channel == 1 ? std::max(r, b) : std::max(r, g);
    double const saturation = most > 0 ? double(most - least) / most : 0.0;
    double const value = most / 255.0;
    if (most - second < o.threshRgb * 255.0)
        return -1;
    return saturation >= o.threshSaturation * 0.5 || value >= o.threshValue ? channel : -1;
}

QString sideName(int side)
{
    static char const* const names[] = {"top", "bottom", "left", "right"};
    return QString::fromLatin1(names[std::clamp(side, 0, 3)]);
}

int sideOf(QString const& word, bool* ok)
{
    static QStringList const names{QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right")};
    int const at = int(names.indexOf(word.toLower()));
    if (at >= 0) {
        *ok = true;
        return at;
    }
    int const number = word.toInt(ok);
    *ok = *ok && number >= 0 && number <= 3;
    return number;
}

}  // namespace

// ---- styles ----------------------------------------------------------------------------------

QStringList styleNames()
{
    return {QStringLiteral("Default Style"), QStringLiteral("(TVG)Default style"), QStringLiteral("Simple Vectorization Preset"),
            QStringLiteral("tones/hilights(B+W notriangles)"), QStringLiteral("Grayscale"), QStringLiteral("Grayscale rougher lines"),
            QStringLiteral("Grayscale soft edge"), QStringLiteral("Centre Lines"), QStringLiteral("Colour"), QStringLiteral("Blue Pencil Left Out"),
            QStringLiteral("Red, Green and Blue Lines Apart")};
}

QString styleLine(QString const& name)
{
    if (name == QLatin1String("Default Style"))
        return QStringLiteral("-no_texture -rmv_holes 7 -rmv_dirt 10 -fit_errorl 2.0 -fit_errorc 2.0");
    if (name == QLatin1String("(TVG)Default style"))
        return QStringLiteral("-no_texture -rmv_holes 7 -rmv_dirt 10 -fit_errorl 2.0 -fit_errorc 2.0 -no_break");
    if (name == QLatin1String("Simple Vectorization Preset"))
        return QStringLiteral("-thresh 0.5 -rmv_dirt 4 -fit_errorl 1.0");
    if (name == QLatin1String("tones/hilights(B+W notriangles)"))
        return QStringLiteral("-2pass -threshl 0.05 -threshc 0.05 -rmv_holesl 7 -rmv_holesc 7 -rmv_dirtl 10 -rmv_dirtc 10 -fit_errorl 2.0 -fit_errorc 2.0");
    if (name == QLatin1String("Grayscale"))
        return QStringLiteral("-2pass -threshl 0.1 -threshc 0.2 -rmv_holesl 7 -rmv_holesc 7 -rmv_dirtl 10 -rmv_dirtc 10 -expand_bitmap 0 -fit_errorc 3.0 -fit_errorl 3.0 -jag_filterc 1");
    if (name == QLatin1String("Grayscale rougher lines"))
        return QStringLiteral("-2pass -threshl 0.1 -threshc 0.2 -rmv_holesl 7 -rmv_holesc 7 -rmv_dirtl 10 -rmv_dirtc 10 -fit_errorl 0.5 -fit_errorc 3.0 -smoothl 0");
    if (name == QLatin1String("Grayscale soft edge"))
        return QStringLiteral("-2pass -threshl 0.05 -threshc 0.05 -rmv_holesl 7 -rmv_holesc 7 -rmv_dirtl 10 -rmv_dirtc 10 -fit_errorl 2.0 -fit_errorc 2.0 -expand_bitmapl 1");
    if (name == QLatin1String("Centre Lines"))
        return QStringLiteral("-pencil -rmv_dirt 10 -pressure_variation 1 0.2 1.0 1.0");
    if (name == QLatin1String("Colour"))
        return QStringLiteral("-color_vectorize -rmv_dirt 10 -fit_errorc 2.0");
    if (name == QLatin1String("Blue Pencil Left Out"))
        return QStringLiteral("-no_blue -rmv_holes 7 -rmv_dirt 10 -fit_errorl 2.0");
    if (name == QLatin1String("Red, Green and Blue Lines Apart"))
        return QStringLiteral("-rgb -rmv_holes 7 -rmv_dirt 10 -rmv_rgb_dirt 10 -fit_errorl 2.0");
    return QString();
}

Options style(QString const& name)
{
    Options o;
    parse(styleLine(name), o);
    return o;
}

// ---- the line of options -----------------------------------------------------------------------

bool parse(QString const& line, Options& o, QString* error, QStringList* ignored)
{
    QStringList const words = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    int i = 0;
    QString problem;
    auto fail = [&](QString const& why) {
        problem = why;
        return false;
    };
    auto number = [&](double& into) {
        if (i >= words.size())
            return fail(QStringLiteral("%1 needs a value").arg(words[i - 1]));
        bool ok = false;
        double const v = words[i].toDouble(&ok);
        if (!ok)
            return fail(QStringLiteral("%1: \"%2\" is not a number").arg(words[i - 1], words[i]));
        into = v;
        ++i;
        return true;
    };
    auto whole = [&](int& into) {
        double v = into;
        if (!number(v))
            return false;
        into = int(std::lround(v));
        return true;
    };
    auto optional = [&](double& into) {
        bool ok = false;
        if (i < words.size()) {
            double const v = words[i].toDouble(&ok);
            if (ok) {
                into = v;
                ++i;
            }
        }
    };
    auto side = [&](int& into) {
        if (i >= words.size())
            return fail(QStringLiteral("%1 needs a side").arg(words[i - 1]));
        bool ok = false;
        int const s = sideOf(words[i], &ok);
        if (!ok)
            return fail(QStringLiteral("%1: \"%2\" is not a side (top, bottom, left, right)").arg(words[i - 1], words[i]));
        into = s;
        ++i;
        return true;
    };
    // Options that do something only in the source program's textured or bubbled strokes, and how many values they take.
    static QHash<QString, int> const noEffect{{QStringLiteral("-color_as_texture"), 0}, {QStringLiteral("-no_texture"), 0},
                                              {QStringLiteral("-downscale_texture"), 1}, {QStringLiteral("-blur_radius"), 1},
                                              {QStringLiteral("-color_contour_smooth_passes"), 1}, {QStringLiteral("-ccsp"), 1},
                                              {QStringLiteral("-penstyle"), 3}, {QStringLiteral("-create_bubbles"), 0},
                                              {QStringLiteral("-bubble_gap"), 1}, {QStringLiteral("-bubble_length"), 1},
                                              {QStringLiteral("-min_radius"), 1}, {QStringLiteral("-max_radius"), 1},
                                              {QStringLiteral("-uniform_gap"), 0}, {QStringLiteral("-uniform_height"), 0},
                                              {QStringLiteral("-pixel"), 1}, {QStringLiteral("-end"), 1}, {QStringLiteral("-ssi"), 0},
                                              {QStringLiteral("-no_break"), 0}, {QStringLiteral("-rmv_triangles"), 1},
                                              {QStringLiteral("-color_rmv_holesl"), 1}, {QStringLiteral("-color_fill_holesl"), 1},
                                              {QStringLiteral("-copystrokes"), 0}, {QStringLiteral("-help"), 0}};
    while (i < words.size()) {
        QString const w = words[i++];
        bool ok = true;
        double v = 0.0;
        if (noEffect.contains(w)) {
            if (ignored && w != QLatin1String("-help"))
                ignored->push_back(w);
            i = std::min(int(words.size()), i + noEffect.value(w));
        } else if (w == QLatin1String("-2pass")) {
            o.twoPass = true;
        } else if (w == QLatin1String("-thresh")) {
            ok = number(o.threshLine);
            o.threshColour = o.threshLine;
        } else if (w == QLatin1String("-threshl")) {
            ok = number(o.threshLine);
        } else if (w == QLatin1String("-threshc")) {
            ok = number(o.threshColour);
        } else if (w == QLatin1String("-rmv_holes")) {
            ok = number(o.holesLine);
            o.holesColour = o.holesLine;
        } else if (w == QLatin1String("-rmv_holesl")) {
            ok = number(o.holesLine);
        } else if (w == QLatin1String("-rmv_holesc")) {
            ok = number(o.holesColour);
        } else if (w == QLatin1String("-rmv_dirt")) {
            ok = number(o.dirtLine);
            o.dirtColour = o.dirtLine;
        } else if (w == QLatin1String("-rmv_dirtl")) {
            ok = number(o.dirtLine);
        } else if (w == QLatin1String("-rmv_dirtc")) {
            ok = number(o.dirtColour);
        } else if (w == QLatin1String("-keep_dirt")) {
            o.keepDirt = true;
        } else if (w == QLatin1String("-fit_error")) {
            ok = number(o.fitLine);
            o.fitColour = o.fitLine;
        } else if (w == QLatin1String("-fit_errorl")) {
            ok = number(o.fitLine);
        } else if (w == QLatin1String("-fit_errorc")) {
            ok = number(o.fitColour);
        } else if (w == QLatin1String("-smoothl")) {
            ok = whole(o.smoothLine);
        } else if (w == QLatin1String("-smoothc")) {
            ok = whole(o.smoothColour);
        } else if (w == QLatin1String("-first_smooth") || w == QLatin1String("-first_smoothl")) {
            ok = whole(o.firstSmoothLine);
        } else if (w == QLatin1String("-first_smoothc")) {
            ok = whole(o.firstSmoothColour);
        } else if (w == QLatin1String("-jag_filter")) {
            ok = whole(o.jagLine);
            o.jagColour = o.jagLine;
        } else if (w == QLatin1String("-jag_filterl")) {
            ok = whole(o.jagLine);
        } else if (w == QLatin1String("-jag_filterc")) {
            ok = whole(o.jagColour);
        } else if (w == QLatin1String("-expand_bitmap")) {
            ok = whole(o.expandLine);
            o.expandColour = o.expandLine;
        } else if (w == QLatin1String("-expand_bitmapl")) {
            ok = whole(o.expandLine);
        } else if (w == QLatin1String("-expand_bitmapc")) {
            ok = whole(o.expandColour);
        } else if (w == QLatin1String("-rmv_hairs")) {
            ok = number(o.hairs);
            double passes = o.hairPasses;
            optional(passes);
            o.hairPasses = std::max(1, int(passes));
        } else if (w == QLatin1String("-no_red")) {
            o.noRed = true;
        } else if (w == QLatin1String("-no_green")) {
            o.noGreen = true;
        } else if (w == QLatin1String("-no_blue")) {
            o.noBlue = true;
        } else if (w == QLatin1String("-rgb")) {
            o.rgb = true;
        } else if (w == QLatin1String("-rgb_alpha")) {
            ok = whole(o.rgbAlpha);
        } else if (w == QLatin1String("-threshrgb")) {
            ok = number(o.threshRgb);
        } else if (w == QLatin1String("-threshsv")) {
            ok = number(o.threshSaturation) && number(o.threshValue);
        } else if (w == QLatin1String("-expand_bitmap_rgb")) {
            ok = whole(o.expandRgb);
        } else if (w == QLatin1String("-rmv_rgb_dirt")) {
            ok = number(o.dirtRgb);
        } else if (w == QLatin1String("-color_vectorize")) {
            o.mode = Options::Colour;
        } else if (w == QLatin1String("-pencil")) {
            o.mode = Options::CentreLines;
        } else if (w == QLatin1String("-pressure_variation")) {
            ok = whole(o.pressureStrategy) && number(o.minPressure) && number(o.maxPressure) && number(o.maxVariation);
        } else if (w == QLatin1String("-gap")) {
            ok = number(o.gap);
        } else if (w == QLatin1String("-noclosegap")) {
            o.closeGaps = false;
        } else if (w == QLatin1String("-noframe")) {
            o.frame = false;
        } else if (w == QLatin1String("-frame_fields")) {
            ok = number(o.frameFields);
        } else if (w == QLatin1String("-flatten")) {
            o.flatten = true;
        } else if (w == QLatin1String("-buildmatte")) {
            o.buildMatte = true;
        } else if (w == QLatin1String("-buildmatte_colourart")) {
            o.matteColourArt = true;
        } else if (w == QLatin1String("-margins")) {
            ok = number(v);
            o.marginTop = o.marginBottom = o.marginLeft = o.marginRight = v;
        } else if (w == QLatin1String("-top_margin")) {
            ok = number(o.marginTop);
        } else if (w == QLatin1String("-bottom_margin")) {
            ok = number(o.marginBottom);
        } else if (w == QLatin1String("-left_margin")) {
            ok = number(o.marginLeft);
        } else if (w == QLatin1String("-right_margin")) {
            ok = number(o.marginRight);
        } else if (w == QLatin1String("-pixel_margins")) {
            int p = 0;
            ok = whole(p);
            o.pixelTop = o.pixelBottom = o.pixelLeft = o.pixelRight = p;
        } else if (w == QLatin1String("-top_pixel_margin")) {
            ok = whole(o.pixelTop);
        } else if (w == QLatin1String("-bottom_pixel_margin")) {
            ok = whole(o.pixelBottom);
        } else if (w == QLatin1String("-left_pixel_margin")) {
            ok = whole(o.pixelLeft);
        } else if (w == QLatin1String("-right_pixel_margin")) {
            ok = whole(o.pixelRight);
        } else if (w == QLatin1String("-dpi")) {
            ok = number(o.dpi);
        } else if (w == QLatin1String("-downscale_input")) {
            ok = whole(o.downscale);
            o.downscale = std::max(1, o.downscale);
        } else if (w == QLatin1String("-register")) {
            o.registerPegs = true;
            double dpi = -1.0;
            ok = number(dpi) && side(o.pegSide) && number(o.looseness);
            if (dpi > 0.0)
                o.dpi = dpi;
        } else if (w == QLatin1String("-register_center_peg_holes") || w == QLatin1String("-rcph")) {
            o.centreHolesOnly = true;
        } else if (w == QLatin1String("-registration_looseness_factor") || w == QLatin1String("-rlf")) {
            ok = number(o.looseness);
        } else if (w == QLatin1String("-rdebug")) {
            o.registerDebug = true;
        } else if (w == QLatin1String("-threshr")) {
            ok = number(o.threshRegister);
        } else if (w == QLatin1String("-pegpitch")) {
            ok = number(o.pegPitch);
        } else if (w == QLatin1String("-rmargin")) {
            ok = number(o.pegMargin);
        } else if (w == QLatin1String("-peg_distance_from_center") || w == QLatin1String("-pdfc")) {
            ok = number(o.pegFromCentre);
        } else if (w == QLatin1String("-out_peg_position")) {
            ok = side(o.outPegSide);
        } else if (w == QLatin1String("-output_peg_matrix")) {
            o.outputPegMatrix = true;
        } else if (w == QLatin1String("-remove_peg_bars")) {
            o.removePegBars = true;
        } else if (w == QLatin1String("-peg_bar_size")) {
            ok = number(o.pegBarSize);
        } else if (w == QLatin1String("-field_size") || w == QLatin1String("-fs")) {
            ok = number(o.fieldSize);
        } else if (w == QLatin1String("-scanner_calibrate")) {
            ok = number(o.calibrateX) && number(o.calibrateY);
        } else if (w == QLatin1String("-file2")) {
            if (i >= words.size())
                ok = fail(QStringLiteral("-file2 needs a file name"));
            else
                o.secondFile = words[i++];
        } else {
            ok = fail(QStringLiteral("Unknown option: %1").arg(w));
        }
        if (!ok) {
            if (error)
                *error = problem;
            return false;
        }
    }
    return true;
}

QString toLine(Options const& o)
{
    Options const d;
    QStringList out;
    auto num = [](double v) { return QString::number(v, 'g', 6); };
    if (o.mode == Options::CentreLines)
        out << QStringLiteral("-pencil");
    if (o.mode == Options::Colour)
        out << QStringLiteral("-color_vectorize");
    if (o.twoPass)
        out << QStringLiteral("-2pass");
    auto pair = [&](char const* both, char const* l, char const* c, double line, double colour, double dl, double dc) {
        if (line == colour && (line != dl || colour != dc))
            out << QString::fromLatin1(both) << num(line);
        else {
            if (line != dl)
                out << QString::fromLatin1(l) << num(line);
            if (colour != dc)
                out << QString::fromLatin1(c) << num(colour);
        }
    };
    if (o.threshLine != d.threshLine || o.threshColour != d.threshColour) {
        out << QStringLiteral("-threshl") << num(o.threshLine);
        if (o.threshColour != d.threshColour)
            out << QStringLiteral("-threshc") << num(o.threshColour);
    }
    pair("-rmv_holes", "-rmv_holesl", "-rmv_holesc", o.holesLine, o.holesColour, d.holesLine, d.holesColour);
    pair("-rmv_dirt", "-rmv_dirtl", "-rmv_dirtc", o.dirtLine, o.dirtColour, d.dirtLine, d.dirtColour);
    if (o.keepDirt)
        out << QStringLiteral("-keep_dirt");
    if (o.fitLine != d.fitLine)
        out << QStringLiteral("-fit_errorl") << num(o.fitLine);
    if (o.fitColour != d.fitColour)
        out << QStringLiteral("-fit_errorc") << num(o.fitColour);
    if (o.smoothLine != d.smoothLine)
        out << QStringLiteral("-smoothl") << num(o.smoothLine);
    if (o.smoothColour != d.smoothColour)
        out << QStringLiteral("-smoothc") << num(o.smoothColour);
    if (o.firstSmoothLine != d.firstSmoothLine)
        out << QStringLiteral("-first_smoothl") << num(o.firstSmoothLine);
    if (o.firstSmoothColour != d.firstSmoothColour)
        out << QStringLiteral("-first_smoothc") << num(o.firstSmoothColour);
    pair("-jag_filter", "-jag_filterl", "-jag_filterc", o.jagLine, o.jagColour, d.jagLine, d.jagColour);
    pair("-expand_bitmap", "-expand_bitmapl", "-expand_bitmapc", o.expandLine, o.expandColour, d.expandLine, d.expandColour);
    if (o.hairs != d.hairs || o.hairPasses != d.hairPasses)
        out << QStringLiteral("-rmv_hairs") << num(o.hairs) << num(o.hairPasses);
    if (o.noRed)
        out << QStringLiteral("-no_red");
    if (o.noGreen)
        out << QStringLiteral("-no_green");
    if (o.noBlue)
        out << QStringLiteral("-no_blue");
    if (o.rgb)
        out << QStringLiteral("-rgb");
    if (o.rgbAlpha != d.rgbAlpha)
        out << QStringLiteral("-rgb_alpha") << num(o.rgbAlpha);
    if (o.threshRgb != d.threshRgb)
        out << QStringLiteral("-threshrgb") << num(o.threshRgb);
    if (o.threshSaturation != d.threshSaturation || o.threshValue != d.threshValue)
        out << QStringLiteral("-threshsv") << num(o.threshSaturation) << num(o.threshValue);
    if (o.expandRgb != d.expandRgb)
        out << QStringLiteral("-expand_bitmap_rgb") << num(o.expandRgb);
    if (o.dirtRgb != d.dirtRgb)
        out << QStringLiteral("-rmv_rgb_dirt") << num(o.dirtRgb);
    if (o.pressureStrategy != d.pressureStrategy || o.minPressure != d.minPressure || o.maxPressure != d.maxPressure || o.maxVariation != d.maxVariation)
        out << QStringLiteral("-pressure_variation") << num(o.pressureStrategy) << num(o.minPressure) << num(o.maxPressure) << num(o.maxVariation);
    if (o.gap != d.gap)
        out << QStringLiteral("-gap") << num(o.gap);
    if (!o.closeGaps)
        out << QStringLiteral("-noclosegap");
    if (!o.frame)
        out << QStringLiteral("-noframe");
    if (o.frameFields != d.frameFields)
        out << QStringLiteral("-frame_fields") << num(o.frameFields);
    if (o.flatten)
        out << QStringLiteral("-flatten");
    if (o.buildMatte)
        out << QStringLiteral("-buildmatte");
    if (o.matteColourArt)
        out << QStringLiteral("-buildmatte_colourart");
    if (o.marginTop != d.marginTop)
        out << QStringLiteral("-top_margin") << num(o.marginTop);
    if (o.marginBottom != d.marginBottom)
        out << QStringLiteral("-bottom_margin") << num(o.marginBottom);
    if (o.marginLeft != d.marginLeft)
        out << QStringLiteral("-left_margin") << num(o.marginLeft);
    if (o.marginRight != d.marginRight)
        out << QStringLiteral("-right_margin") << num(o.marginRight);
    if (o.pixelTop != d.pixelTop)
        out << QStringLiteral("-top_pixel_margin") << num(o.pixelTop);
    if (o.pixelBottom != d.pixelBottom)
        out << QStringLiteral("-bottom_pixel_margin") << num(o.pixelBottom);
    if (o.pixelLeft != d.pixelLeft)
        out << QStringLiteral("-left_pixel_margin") << num(o.pixelLeft);
    if (o.pixelRight != d.pixelRight)
        out << QStringLiteral("-right_pixel_margin") << num(o.pixelRight);
    if (o.registerPegs)
        out << QStringLiteral("-register") << num(o.dpi) << sideName(o.pegSide) << num(o.looseness);
    else if (o.dpi != d.dpi)
        out << QStringLiteral("-dpi") << num(o.dpi);
    if (o.registerPegs && o.looseness != d.looseness)
        out << QStringLiteral("-rlf") << num(o.looseness);
    if (o.downscale != d.downscale)
        out << QStringLiteral("-downscale_input") << num(o.downscale);
    if (o.centreHolesOnly)
        out << QStringLiteral("-rcph");
    if (o.registerDebug)
        out << QStringLiteral("-rdebug");
    if (o.threshRegister != d.threshRegister)
        out << QStringLiteral("-threshr") << num(o.threshRegister);
    if (o.pegPitch != d.pegPitch)
        out << QStringLiteral("-pegpitch") << num(o.pegPitch);
    if (o.pegMargin != d.pegMargin)
        out << QStringLiteral("-rmargin") << num(o.pegMargin);
    if (o.pegFromCentre != d.pegFromCentre)
        out << QStringLiteral("-pdfc") << num(o.pegFromCentre);
    if (o.outPegSide >= 0)
        out << QStringLiteral("-out_peg_position") << sideName(o.outPegSide);
    if (o.outputPegMatrix)
        out << QStringLiteral("-output_peg_matrix");
    if (o.removePegBars)
        out << QStringLiteral("-remove_peg_bars");
    if (o.pegBarSize != d.pegBarSize)
        out << QStringLiteral("-peg_bar_size") << num(o.pegBarSize);
    if (o.fieldSize != d.fieldSize)
        out << QStringLiteral("-field_size") << num(o.fieldSize);
    if (o.calibrateX != d.calibrateX || o.calibrateY != d.calibrateY)
        out << QStringLiteral("-scanner_calibrate") << num(o.calibrateX) << num(o.calibrateY);
    if (!o.secondFile.isEmpty())
        out << QStringLiteral("-file2") << o.secondFile;
    return out.join(QLatin1Char(' '));
}

QString help()
{
    return QStringLiteral(
        "-2pass  line art and colour art, each with its own settings (l and c)\n"
        "-thresh / -threshl / -threshc <0..1>  how dark line art is (of white); colour art: darker than 1 - this of white\n"
        "-rmv_holes[l|c] <area>  holes smaller than this many pixels filled\n"
        "-rmv_dirt[l|c] <area>  specks smaller than this left out;  -keep_dirt  none left out\n"
        "-fit_error[l|c] <pixels>  how far the outlines may stray to have fewer points\n"
        "-smoothl / -smoothc <passes>;  -first_smooth[l|c] <passes>  smoothing\n"
        "-jag_filter[l|c] <pixels>;  -expand_bitmap[l|c] <pixels>;  -rmv_hairs <pixels> [passes]\n"
        "-no_red / -no_green / -no_blue  lines of that colour left out\n"
        "-rgb  red, green and blue lines each a shape of their own colour;  -rgb_alpha, -threshrgb, -threshsv <s> <v>, -expand_bitmap_rgb, -rmv_rgb_dirt\n"
        "-color_vectorize  in colour;  -pencil  centre lines;  -pressure_variation <0|1|2> <min> <max> <variation>\n"
        "-gap <pixels>  gaps closed for the colour art;  -noclosegap\n"
        "-noframe;  -frame_fields <fields>;  -flatten;  -buildmatte;  -buildmatte_colourart\n"
        "-margins / -top_margin / -bottom_margin / -left_margin / -right_margin <inches>;  -pixel_margins ... <pixels>\n"
        "-dpi <dpi>;  -downscale_input <factor>;  -field_size / -fs <fields>;  -scanner_calibrate <x> <y>;  -file2 <colour art file>\n"
        "-register <dpi> <top|bottom|left|right> <looseness>  optical registration by the peg holes;  -rcph;  -rlf <factor>;  -threshr <0..1>\n"
        "-pegpitch <inches>;  -rmargin <inches>;  -pdfc <inches>;  -out_peg_position <side>;  -output_peg_matrix;  -rdebug\n"
        "-remove_peg_bars;  -peg_bar_size <inches>\n"
        "-help  this");
}

// ---- optical registration ----------------------------------------------------------------------

bool registration(QImage const& source, Options const& o, QTransform& move, QString* why, QString* debug)
{
    move = QTransform();
    QImage const image = source.convertToFormat(QImage::Format_ARGB32);
    double const dpi = o.dpi > 0.0 ? o.dpi : (image.dotsPerMeterX() > 0 ? image.dotsPerMeterX() * 0.0254 : 72.0);
    int const w = image.width(), h = image.height();
    // The strip along the peg side, looked at as if the pegs were along the top.
    bool const across = o.pegSide <= 1;
    int const length = across ? w : h, depth = std::min(across ? h : w, int(std::lround(o.pegMargin * dpi)));
    if (depth < 4 || length < 16) {
        if (why)
            *why = QStringLiteral("The picture is too small to hold peg holes.");
        return false;
    }
    Mask strip(length, depth);
    for (int d = 0; d < depth; ++d)
        for (int a = 0; a < length; ++a) {
            int const x = o.pegSide == 0 ? a : o.pegSide == 1 ? a : o.pegSide == 2 ? d : w - 1 - d;
            int const y = o.pegSide == 0 ? d : o.pegSide == 1 ? h - 1 - d : a;
            QRgb const c = image.pixel(x, y);
            strip.at(a, d) = qAlpha(c) >= 128 && lightnessOf(c) < o.threshRegister;
        }
    QVector<int> label, size;
    QVector<bool> edge;
    int const count = pieces(strip, 1, label, size, edge);
    struct Hole {
        QPointF centre;
        double area = 0.0;
        bool round = false;
    };
    QVector<QRectF> boxes(count);
    QVector<QPointF> sums(count);
    for (int i = 0; i < label.size(); ++i)
        if (label[i] >= 0) {
            QPointF const p(i % length + 0.5, i / length + 0.5);
            sums[label[i]] += p;
            QRectF& b = boxes[label[i]];
            b = b.isNull() ? QRectF(p - QPointF(0.5, 0.5), QSizeF(1, 1)) : b.united(QRectF(p - QPointF(0.5, 0.5), QSizeF(1, 1)));
        }
    double const holeRadius = 0.125 * dpi;  // a quarter inch across
    double const expected = std::numbers::pi * holeRadius * holeRadius;
    double const loose = std::max(1.0, o.looseness);
    QVector<Hole> holes;
    QStringList lines;
    for (int k = 0; k < count; ++k) {
        double const area = size[k];
        if (area < expected / (2.0 * loose) || area > expected * 4.0 * loose)
            continue;
        QRectF const b = boxes[k];
        double const aspect = std::max(b.width(), b.height()) / std::max(1.0, std::min(b.width(), b.height()));
        double const filled = area / std::max(1.0, b.width() * b.height());
        Hole hole{sums[k] / area, area, aspect < 1.0 + 0.2 * loose && filled > 0.6 && area < expected * 2.0 * loose};
        holes.push_back(hole);
        lines << QStringLiteral("hole at %1,%2 area %3 %4").arg(hole.centre.x(), 0, 'f', 1).arg(hole.centre.y(), 0, 'f', 1).arg(area).arg(hole.round ? "round" : "oblong");
    }
    // The round hole nearest the middle of the strip.
    int centre = -1;
    for (int k = 0; k < holes.size(); ++k)
        if (holes[k].round && (centre < 0 || std::abs(holes[k].centre.x() - length / 2.0) < std::abs(holes[centre].centre.x() - length / 2.0)))
            centre = k;
    if (centre < 0) {
        if (debug)
            *debug = lines.join(QLatin1Char('\n'));
        if (why)
            *why = QStringLiteral("Unable to optically register this drawing.");
        return false;
    }
    // The holes either side: as far from the middle one as the pegs are (more or less, as loosely as asked).
    QPointF const c = holes[centre].centre;
    double const reach = o.pegFromCentre * dpi, slack = 0.25 * dpi * loose;
    QVector<QPointF> sides;
    for (int k = 0; k < holes.size(); ++k) {
        if (k == centre || (o.centreHolesOnly && !holes[k].round))
            continue;
        double const d = QLineF(c, holes[k].centre).length();
        if (o.centreHolesOnly ? d > holeRadius * 4.0 : std::abs(d - reach) <= slack)
            sides.push_back(holes[k].centre);
    }
    double angle = 0.0;     // how far the holes are turned off level (degrees, in the strip)
    if (sides.size() >= 2) {
        std::sort(sides.begin(), sides.end(), [](QPointF a, QPointF b) { return a.x() < b.x(); });
        angle = std::atan2(sides.last().y() - sides.first().y(), sides.last().x() - sides.first().x()) * 180.0 / std::numbers::pi;
    } else if (sides.size() == 1) {
        QPointF const d = sides.first().x() > c.x() ? sides.first() - c : c - sides.first();
        angle = std::atan2(d.y(), d.x()) * 180.0 / std::numbers::pi;
    }
    lines << QStringLiteral("middle hole at %1,%2, %3 hole(s) either side, turned %4 degrees").arg(c.x(), 0, 'f', 1).arg(c.y(), 0, 'f', 1).arg(sides.size()).arg(angle, 0, 'f', 3);
    // Back into the picture: the strip's place, and which way it runs.
    auto toImage = [&](QPointF s) {
        switch (o.pegSide) {
        case 1: return QPointF(s.x(), h - s.y());
        case 2: return QPointF(s.y(), s.x());
        case 3: return QPointF(w - s.y(), s.x());
        default: return s;
        }
    };
    QPointF const hole = toImage(c);
    double const imageAngle = o.pegSide == 1 || o.pegSide == 2 ? -angle : angle;
    // Where the middle hole is to be: in the middle of the side, half the strip in.
    QPointF const target = toImage(QPointF(length / 2.0, o.pegMargin * dpi / 2.0));
    move = QTransform::fromTranslate(-hole.x(), -hole.y()) * QTransform().rotate(-imageAngle) * QTransform::fromTranslate(target.x(), target.y());
    // The pegs on another side: the whole picture turned about its middle.
    if (o.outPegSide >= 0 && o.outPegSide != o.pegSide) {
        static int const turns[4][4] = {{0, 180, -90, 90}, {180, 0, 90, -90}, {90, -90, 0, 180}, {-90, 90, 180, 0}};
        double const turn = turns[o.pegSide][o.outPegSide];
        move = move * QTransform::fromTranslate(-w / 2.0, -h / 2.0) * QTransform().rotate(turn) * QTransform::fromTranslate(w / 2.0, h / 2.0);
    }
    if (debug)
        *debug = lines.join(QLatin1Char('\n'));
    return true;
}

// ---- the whole of it -----------------------------------------------------------------------------

Result run(QImage const& source, Options const& o, QColor const& colour, int swatch)
{
    Result result;
    if (source.isNull()) {
        result.ok = false;
        result.notes << QStringLiteral("Could not read input drawing.");
        return result;
    }
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    double dpi = o.dpi > 0.0 ? o.dpi : (image.dotsPerMeterX() > 0 ? image.dotsPerMeterX() * 0.0254 : 72.0);
    if (o.downscale > 1) {
        image = image.scaled(std::max(1, image.width() / o.downscale), std::max(1, image.height() / o.downscale), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        dpi /= o.downscale;
    }
    Options scaled = o;
    scaled.dpi = dpi;
    // Registered by the peg holes.
    if (o.registerPegs) {
        QTransform move;
        QString why, debug;
        if (registration(image, scaled, move, &why, &debug)) {
            QImage moved(image.size(), QImage::Format_ARGB32);
            moved.fill(Qt::white);
            QPainter p(&moved);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setTransform(move);
            p.drawImage(0, 0, image);
            p.end();
            image = moved;
            if (o.outputPegMatrix)
                result.notes << QStringLiteral("Peg matrix: %1 %2 %3 %4 %5 %6")
                                    .arg(move.m11()).arg(move.m12()).arg(move.m21()).arg(move.m22()).arg(move.dx()).arg(move.dy());
        } else {
            result.notes << QStringLiteral("Optical Registration Failed: ") + why;
        }
        if (o.registerDebug && !debug.isEmpty())
            result.notes << debug.split(QLatin1Char('\n'));
    }
    int const w = image.width(), h = image.height();
    // Margins and the peg bar strip cleared to paper.
    {
        QPainter p(&image);
        auto clear = [&](QRectF r) { p.fillRect(r, Qt::white); };
        int const top = int(std::lround(o.marginTop * dpi)) + o.pixelTop, bottom = int(std::lround(o.marginBottom * dpi)) + o.pixelBottom;
        int const left = int(std::lround(o.marginLeft * dpi)) + o.pixelLeft, right = int(std::lround(o.marginRight * dpi)) + o.pixelRight;
        clear(QRectF(0, 0, w, top));
        clear(QRectF(0, h - bottom, w, bottom));
        clear(QRectF(0, 0, left, h));
        clear(QRectF(w - right, 0, right, h));
        if (o.removePegBars) {
            int const bar = int(std::lround(o.pegBarSize * dpi));
            clear(o.pegSide == 0 ? QRectF(0, 0, w, bar) : o.pegSide == 1 ? QRectF(0, h - bar, w, bar) : o.pegSide == 2 ? QRectF(0, 0, bar, h) : QRectF(w - bar, 0, bar, h));
        }
    }
    QTransform const calibrate = QTransform::fromScale(o.calibrateX, o.calibrateY);
    result.placed = calibrate;
    Drawing& d = result.drawing;

    if (o.mode == Options::Colour) {
        // In colour: a painted shape for each colour's areas.
        for (ColourShape const& area : traceColours(image, 16, QColor(Qt::white), std::max(1.0, o.dirtColour))) {
            Fill f;
            f.color = area.colour;
            for (QPolygonF const& outline : area.outlines) {
                QPolygonF line = smoothedRun(outline, o.smoothColour);
                if (o.fitColour > 0.0 && line.size() > 8)
                    line = simplify(line, o.fitColour);
                if (line.size() >= 3)
                    f.outlines.push_back(calibrate.map(line));
            }
            if (!f.outlines.isEmpty())
                d.fillsOf(Drawing::LineSheet).push_back(f);
        }
        if (o.flatten)
            Morphing::flatten(d);
        return result;
    }

    // The tones as levels between the darkest line and the paper.
    double black = 0.0, white = 1.0;
    levelsOf(image, black, white);
    auto level = [&](QRgb c) { return std::clamp((lightnessOf(c) - black) / std::max(0.05, white - black), 0.0, 1.0); };
    Mask line(w, h), tone(w, h), rgb[3] = {Mask(w, h), Mask(w, h), Mask(w, h)};
    bool const leaveOut[3] = {o.noRed, o.noGreen, o.noBlue};
    QImage second;
    if (!o.secondFile.isEmpty()) {
        second = QImage(o.secondFile).convertToFormat(QImage::Format_ARGB32);
        if (second.isNull())
            result.notes << QStringLiteral("Cannot load file %1, unsupported type.").arg(o.secondFile);
        else if (second.size() != image.size())
            second = second.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb const c = row[x];
            if (qAlpha(c) < 128)
                continue;
            int const channel = (o.rgb || o.noRed || o.noGreen || o.noBlue) ? colouredWith(c, o) : -1;
            if (channel >= 0 && (o.rgb || leaveOut[channel])) {
                if (o.rgb && !leaveOut[channel])
                    rgb[channel].at(x, y) = 1;
                continue;       // not black line art, nor tone
            }
            double const l = level(c);
            if (l < o.threshLine)
                line.at(x, y) = 1;
            else if (o.twoPass && second.isNull() && l < 1.0 - o.threshColour)
                tone.at(x, y) = 1;
        }
    }
    if (!second.isNull()) {
        double sb = 0.0, sw = 1.0;
        levelsOf(second, sb, sw);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                QRgb const c = second.pixel(x, y);
                double const l = std::clamp((lightnessOf(c) - sb) / std::max(0.05, sw - sb), 0.0, 1.0);
                tone.at(x, y) = qAlpha(c) >= 128 && l < 1.0 - o.threshColour && !line.at(x, y);
            }
    }
    auto prepare = [&](Mask& m, int expand, int jag, double dirt, double holes) {
        m = spread(m, expand, true);
        if (jag > 0)
            m = opened(closed(m, jag), jag);
        if (o.hairs > 0.0)
            for (int pass = 0; pass < o.hairPasses; ++pass)
                m = opened(m, std::max(1, int(std::ceil(o.hairs / 2.0))));
        clean(m, o.keepDirt ? 0.0 : dirt, holes);
    };
    prepare(line, o.expandLine, o.jagLine, o.dirtLine, o.holesLine);

    if (o.mode == Options::CentreLines) {
        // Pencil lines down the middle of the line art.
        QImage bits(w, h, QImage::Format_ARGB32);
        bits.fill(Qt::white);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (line.at(x, y))
                    bits.setPixel(x, y, qRgb(0, 0, 0));
        QVector<CentreLine> const traced = centreLines(bits, 128);
        double thickest = 1.0;
        for (CentreLine const& t : traced)
            thickest = std::max(thickest, t.width);
        for (CentreLine const& t : traced) {
            Stroke s;
            s.swatch = swatch;
            s.color = colour;
            s.closed = t.closed;
            QPolygonF points = smoothedRun(t.points, 0);
            if (o.fitLine > 0.0 && points.size() > 3)
                points = simplify(points, o.fitLine);
            // Pressure Variation: the line's thickness as pressure (1: as it is; 2: eased), kept within its bounds.
            float pressure = 1.0f;
            if (o.pressureStrategy > 0) {
                double share = t.width / thickest;
                if (o.pressureStrategy == 2)
                    share = std::sqrt(share);
                share = 1.0 - std::min(o.maxVariation, 1.0 - share);
                pressure = float(std::clamp(o.minPressure + (o.maxPressure - o.minPressure) * share, 0.0, 1.0));
                s.width = float(std::max(1.0, thickest));
            } else {
                s.width = float(std::max(1.0, t.width));
            }
            for (QPointF const& p : points)
                s.points.push_back({calibrate.map(p), pressure});
            if (s.points.size() >= 2)
                d.strokesOf(Drawing::LineSheet).push_back(s);
        }
    } else if (line.any()) {
        Fill f;
        f.swatch = swatch;
        f.color = colour;
        for (QPolygonF const& outline : outlinesOf(line, o.smoothLine + o.firstSmoothLine, o.fitLine))
            f.outlines.push_back(calibrate.map(outline));
        if (!f.outlines.isEmpty())
            d.fillsOf(Drawing::LineSheet).push_back(f);
    }

    // Red, green and blue lines: a shape of each colour.
    for (int k = 0; k < 3; ++k) {
        if (!rgb[k].any())
            continue;
        rgb[k] = spread(rgb[k], o.expandRgb, true);
        clean(rgb[k], o.keepDirt ? 0.0 : o.dirtRgb, o.holesLine);
        Fill f;
        f.color = QColor(k == 0 ? 255 : 0, k == 1 ? 255 : 0, k == 2 ? 255 : 0, std::clamp(o.rgbAlpha, 0, 255));
        for (QPolygonF const& outline : outlinesOf(rgb[k], o.smoothLine, o.fitLine))
            f.outlines.push_back(calibrate.map(outline));
        if (!f.outlines.isEmpty())
            d.fillsOf(Drawing::LineSheet).push_back(f);
    }

    // The colour art: the tones, each piece a shape of its own colour, the gaps of the line art closed round them.
    if (o.twoPass) {
        prepare(tone, o.expandColour, o.jagColour, o.dirtColour, o.holesColour);
        if (o.closeGaps && o.gap > 0.0) {
            Mask both = tone;
            for (int i = 0; i < both.bits.size(); ++i)
                both.bits[i] = tone.bits[i] || line.bits[i];
            both = closed(both, std::max(1, int(std::lround(o.gap / 2.0))));
            for (int i = 0; i < tone.bits.size(); ++i)
                tone.bits[i] = both.bits[i] && !line.bits[i] ? 1 : tone.bits[i];
        }
        QVector<int> label, size;
        QVector<bool> edge;
        int const count = pieces(tone, 1, label, size, edge);
        QVector<qint64> r(count, 0), g(count, 0), b(count, 0);
        for (int i = 0; i < label.size(); ++i)
            if (label[i] >= 0) {
                QRgb const c = (second.isNull() ? image : second).pixel(i % w, i / w);
                r[label[i]] += qRed(c);
                g[label[i]] += qGreen(c);
                b[label[i]] += qBlue(c);
            }
        // (The biggest pieces each a shape; past so many, the rest together.)
        QVector<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int c) { return size[a] > size[c]; });
        int const kMost = 200;
        QVector<int> slot(count, -1);
        for (int k = 0; k < count; ++k)
            slot[order[k]] = std::min(k, kMost);
        int const slotCount = std::min(count, kMost + 1);
        QVector<Mask> parts(slotCount, Mask(w, h));
        QVector<qint64> sr(slotCount, 0), sg(slotCount, 0), sb(slotCount, 0), sn(slotCount, 0);
        for (int i = 0; i < label.size(); ++i)
            if (label[i] >= 0)
                parts[slot[label[i]]].bits[i] = 1;
        for (int k = 0; k < count; ++k) {
            int const s = slot[k];
            sr[s] += r[k];
            sg[s] += g[k];
            sb[s] += b[k];
            sn[s] += size[k];
        }
        for (int s = 0; s < slotCount; ++s) {
            Fill f;
            f.color = sn[s] > 0 ? QColor(int(sr[s] / sn[s]), int(sg[s] / sn[s]), int(sb[s] / sn[s])) : QColor(Qt::gray);
            for (QPolygonF const& outline : outlinesOf(parts[s], o.smoothColour + o.firstSmoothColour, o.fitColour))
                f.outlines.push_back(calibrate.map(outline));
            if (!f.outlines.isEmpty())
                d.fillsOf(Drawing::ColourSheet).push_back(f);
        }
        if (o.frame) {
            // A frame round it all (an invisible line), for the paper round the drawing to be painted too.
            QRectF box(0, 0, w, h);
            if (o.frameFields > 0.0) {
                double const across = o.frameFields / 12.0 * w;
                box = QRectF(w / 2.0 - across / 2.0, h / 2.0 - across * 0.375, across, across * 0.75);
            }
            Stroke frame;
            frame.closed = true;
            frame.width = 0.0f;
            frame.color = QColor(0, 0, 0, 0);
            for (QPointF const& p : {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()})
                frame.points.push_back({calibrate.map(p), 1.0f});
            d.strokesOf(Drawing::ColourSheet).push_back(frame);
        }
    }

    // Mattes: the shape of the whole drawing (or of its colour art), filled in, underneath it.
    if (o.buildMatte || o.matteColourArt) {
        Mask matte(w, h);
        for (int i = 0; i < matte.bits.size(); ++i)
            matte.bits[i] = o.matteColourArt ? tone.bits[i] : (line.bits[i] || tone.bits[i] || rgb[0].bits[i] || rgb[1].bits[i] || rgb[2].bits[i]);
        matte = allHolesFilled(closed(matte, 3));
        Fill f;
        f.color = Qt::white;
        for (QPolygonF const& outline : outlinesOf(matte, 1, 2.0))
            f.outlines.push_back(calibrate.map(outline));
        if (!f.outlines.isEmpty())
            d.fillsOf(Drawing::UnderlaySheet).push_back(f);
    }
    if (o.flatten)
        Morphing::flatten(d);
    return result;
}

}  // namespace Vectorize
