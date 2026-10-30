// Eclipse - the text LUT formats: .cube, .3dl, .spi1d, .spi3d, .spimtx, .csp and Discreet's 1D .lut.
#include "colour/luts/Luts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

using namespace Cms;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

struct Lines {
    QStringList lines;
    int at = 0;
    explicit Lines(QByteArray const& data)
    {
        for (QString line : QString::fromUtf8(data).split(QLatin1Char('\n'))) {
            line.remove(QLatin1Char('\r'));
            lines << line;
        }
    }
    // The next line with something on it (comments left out); false at the end.
    bool next(QString* out, bool keepHash = false)
    {
        while (at < lines.size()) {
            QString t = lines[at++].trimmed();
            if (!keepHash) {
                int const hash = t.indexOf(QLatin1Char('#'));
                if (hash >= 0)
                    t = t.left(hash).trimmed();
            }
            if (!t.isEmpty()) {
                *out = t;
                return true;
            }
        }
        return false;
    }
};

QStringList wordsOf(QString const& line)
{
    return line.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

bool numbersOf(QString const& line, QVector<double>* out)
{
    out->clear();
    for (QString const& w : wordsOf(line)) {
        bool ok = false;
        double const v = w.toDouble(&ok);
        if (!ok)
            return false;
        *out << v;
    }
    return true;
}

bool fail(QString* error, QString const& why)
{
    if (error)
        *error = why;
    return false;
}

QString malformed(Lines const& l)
{
    return said("Malformed LUT line %1.").arg(l.at);
}

bool cube(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    int size1 = 0, size3 = 0;
    double lo[3] = {0, 0, 0}, hi[3] = {1, 1, 1};
    double lo1 = 0, hi1 = 1, lo3 = 0, hi3 = 1;
    bool range1 = false, range3 = false;
    QVector<float> values;
    QString line;
    while (l.next(&line)) {
        QStringList const w = wordsOf(line);
        QString const key = w.value(0).toUpper();
        if (key == QLatin1String("TITLE") || key == QLatin1String("LUT_IN_VIDEO_RANGE") || key == QLatin1String("LUT_OUT_VIDEO_RANGE"))
            continue;
        if (key == QLatin1String("LUT_1D_SIZE")) {
            size1 = w.value(1).toInt();
            continue;
        }
        if (key == QLatin1String("LUT_3D_SIZE")) {
            size3 = w.value(1).toInt();
            continue;
        }
        if (key == QLatin1String("DOMAIN_MIN") || key == QLatin1String("DOMAIN_MAX")) {
            if (w.size() != 4)
                return fail(error, malformed(l));
            for (int i = 0; i < 3; ++i)
                (key == QLatin1String("DOMAIN_MIN") ? lo : hi)[i] = w[i + 1].toDouble();
            continue;
        }
        if (key == QLatin1String("LUT_1D_INPUT_RANGE") || key == QLatin1String("LUT_3D_INPUT_RANGE")) {
            bool const one = key == QLatin1String("LUT_1D_INPUT_RANGE");
            (one ? lo1 : lo3) = w.value(1).toDouble();
            (one ? hi1 : hi3) = w.value(2).toDouble();
            (one ? range1 : range3) = true;
            continue;
        }
        QVector<double> v;
        if (!numbersOf(line, &v) || v.size() != 3)
            return fail(error, key.at(0).isLetter() ? said("Malformed LUT - unknown word '%1'.").arg(w.value(0)) : malformed(l));
        values << float(v[0]) << float(v[1]) << float(v[2]);
    }
    if (size1 <= 0 && size3 <= 0)
        return fail(error, said("The LUT gives no size (LUT_1D_SIZE or LUT_3D_SIZE)."));
    qsizetype const want = qsizetype(std::max(size1, 0)) * 3 + qsizetype(std::max(size3, 0)) * size3 * size3 * 3;
    if (values.size() != want)
        return fail(error, said("The LUT holds %1 values where %2 were expected.").arg(values.size() / 3).arg(want / 3));
    if (size1 > 0) {
        if (size1 < 2)
            return fail(error, said("A 1D LUT needs 2 entries at least."));
        QVector<float> t(qsizetype(size1) * 3);
        for (int i = 0; i < size1; ++i)
            for (int c = 0; c < 3; ++c)
                t[qsizetype(c) * size1 + i] = values[qsizetype(i) * 3 + c];
        Op op = Luts::lut1d(t, size1, range1 ? lo1 : 0.0, range1 ? hi1 : 1.0);
        if (!range1 && size3 <= 0)
            for (int c = 0; c < 3; ++c) {
                op.inLow[size_t(c)] = lo[c];
                op.inHigh[size_t(c)] = hi[c];
            }
        *out << op;
    }
    if (size3 > 0) {
        if (size3 < 2)
            return fail(error, said("A 3D LUT needs 2 entries a side at least."));
        Op op = Luts::lut3d(values.mid(qsizetype(std::max(size1, 0)) * 3), size3, range3 ? lo3 : 0.0, range3 ? hi3 : 1.0);
        if (!range3)
            for (int c = 0; c < 3; ++c) {
                op.inLow[size_t(c)] = lo[c];
                op.inHigh[size_t(c)] = hi[c];
            }
        *out << op;
    }
    return true;
}

// The largest a value of so many bits can be, for the smallest number of bits that holds `largest`.
double bitsFor(double largest)
{
    for (int bits : {8, 10, 12, 14, 16})
        if (largest <= std::pow(2.0, bits) - 1.0)
            return std::pow(2.0, bits) - 1.0;
    return largest > 1.0 ? largest : 1.0;
}

bool threeDl(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    QString line;
    QVector<double> shaper;
    QVector<QVector<double>> rows;
    while (l.next(&line)) {
        if (line.startsWith(QLatin1String("3DMESH"), Qt::CaseInsensitive) || line.startsWith(QLatin1String("Mesh"), Qt::CaseInsensitive)
            || line.startsWith(QLatin1String("LUT8"), Qt::CaseInsensitive) || line.startsWith(QLatin1String("gamma"), Qt::CaseInsensitive))
            continue;
        QVector<double> v;
        if (!numbersOf(line, &v))
            return fail(error, malformed(l));
        if (shaper.isEmpty() && v.size() > 3) {
            shaper = v;
            continue;
        }
        if (v.size() != 3)
            return fail(error, malformed(l));
        rows << v;
    }
    int n = int(shaper.size());
    if (n == 0)
        n = int(std::lround(std::cbrt(double(rows.size()))));
    if (n < 2 || qsizetype(n) * n * n != rows.size())
        return fail(error, said("The 3D LUT holds %1 entries; a cube of %2 a side needs %3.").arg(rows.size()).arg(n).arg(qsizetype(n) * n * n));
    double largest = 0.0;
    for (QVector<double> const& r : rows)
        largest = std::max({largest, r[0], r[1], r[2]});
    double const scale = bitsFor(largest);
    // (Blue changes fastest in the file; Eclipse keeps red fastest.)
    QVector<float> cubeValues(qsizetype(n) * n * n * 3);
    for (int r = 0; r < n; ++r)
        for (int g = 0; g < n; ++g)
            for (int b = 0; b < n; ++b) {
                QVector<double> const& v = rows[(qsizetype(r) * n + g) * n + b];
                qsizetype const at = ((qsizetype(b) * n + g) * n + r) * 3;
                for (int c = 0; c < 3; ++c)
                    cubeValues[at + c] = float(v[c] / scale);
            }
    *out << Luts::lut3d(cubeValues, n);
    return true;
}

bool spi1d(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    QString line;
    double from = 0, to = 1;
    int length = 0, components = 1;
    bool inside = false;
    QVector<QVector<double>> rows;
    while (l.next(&line)) {
        if (!inside) {
            QStringList const w = wordsOf(line);
            QString const key = w.value(0).toLower();
            if (key == QLatin1String("version"))
                continue;
            if (key == QLatin1String("from")) {
                from = w.value(1).toDouble();
                to = w.value(2).toDouble();
            } else if (key == QLatin1String("length")) {
                length = w.value(1).toInt();
            } else if (key == QLatin1String("components")) {
                components = w.value(1).toInt();
            } else if (line.startsWith(QLatin1Char('{'))) {
                inside = true;
            } else {
                return fail(error, malformed(l));
            }
            continue;
        }
        if (line.startsWith(QLatin1Char('}')))
            break;
        QVector<double> v;
        if (!numbersOf(line, &v) || v.size() != components)
            return fail(error, malformed(l));
        rows << v;
    }
    if (length < 2 || rows.size() != length || (components != 1 && components != 3))
        return fail(error, said("The 1D LUT is not complete (Length %1, %2 entries).").arg(length).arg(rows.size()));
    QVector<float> t(qsizetype(length) * 3);
    for (int i = 0; i < length; ++i)
        for (int c = 0; c < 3; ++c)
            t[qsizetype(c) * length + i] = float(rows[i][components == 1 ? 0 : c]);
    *out << Luts::lut1d(t, length, from, to);
    return true;
}

bool spi3d(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    QString line;
    if (!l.next(&line) || !line.startsWith(QLatin1String("SPILUT"), Qt::CaseInsensitive))
        return fail(error, said("The LUT is not an SPI 3D LUT (no SPILUT line)."));
    l.next(&line);
    QVector<double> dims;
    if (!l.next(&line) || !numbersOf(line, &dims) || dims.size() != 3 || dims[0] != dims[1] || dims[1] != dims[2])
        return fail(error, said("Error while reading the size of the LUT."));
    int const n = int(dims[0]);
    if (n < 2)
        return fail(error, said("A 3D LUT needs 2 entries a side at least."));
    QVector<float> cubeValues(qsizetype(n) * n * n * 3, 0.0f);
    QVector<bool> filled(qsizetype(n) * n * n, false);
    while (l.next(&line)) {
        QVector<double> v;
        if (!numbersOf(line, &v) || v.size() != 6)
            return fail(error, malformed(l));
        int const r = int(v[0]), g = int(v[1]), b = int(v[2]);
        if (r < 0 || g < 0 || b < 0 || r >= n || g >= n || b >= n)
            return fail(error, malformed(l));
        qsizetype const at = (qsizetype(b) * n + g) * n + r;
        filled[at] = true;
        for (int c = 0; c < 3; ++c)
            cubeValues[at * 3 + c] = float(v[3 + c]);
    }
    if (filled.contains(false))
        return fail(error, said("The 3D LUT does not give every entry."));
    *out << Luts::lut3d(cubeValues, n);
    return true;
}

bool spimtx(QByteArray const& data, Chain* out, QString* error)
{
    QVector<double> v;
    if (!numbersOf(QString::fromUtf8(data).simplified(), &v) || v.size() != 12)
        return fail(error, said("The matrix file holds %1 numbers; it needs 12.").arg(v.size()));
    *out << matrixOp({v[0], v[1], v[2], v[4], v[5], v[6], v[8], v[9], v[10]}, {v[3] / 65535.0, v[7] / 65535.0, v[11] / 65535.0});
    return true;
}

bool csp(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    QString line;
    if (!l.next(&line) || !line.startsWith(QLatin1String("CSPLUTV100")))
        return fail(error, said("The LUT does not appear to be a CineSpace (csp) LUT."));
    if (!l.next(&line))
        return fail(error, said("The LUT file ends too soon."));
    bool const three = line.trimmed().toUpper() == QLatin1String("3D");
    if (!three && line.trimmed().toUpper() != QLatin1String("1D"))
        return fail(error, said("The CineSpace LUT is neither 1D nor 3D."));
    if (!l.next(&line))
        return fail(error, said("The LUT file ends too soon."));
    if (line.startsWith(QLatin1String("BEGIN METADATA"))) {
        while (l.next(&line) && !line.startsWith(QLatin1String("END METADATA"))) {
        }
        if (!l.next(&line))
            return fail(error, said("The LUT file ends too soon."));
    }
    std::array<QVector<double>, 3> px, py;
    bool identity = true;
    for (int c = 0; c < 3; ++c) {
        if (c > 0 && !l.next(&line))
            return fail(error, said("The LUT file ends too soon."));
        int const count = line.toInt();
        QString ins, outs;
        if (count < 2 || !l.next(&ins) || !l.next(&outs) || !numbersOf(ins, &px[size_t(c)]) || !numbersOf(outs, &py[size_t(c)]) || px[size_t(c)].size() != count
            || py[size_t(c)].size() != count)
            return fail(error, malformed(l));
        identity = identity && count == 2 && px[size_t(c)] == QVector<double>{0, 1} && py[size_t(c)] == QVector<double>{0, 1};
    }
    if (!identity)
        *out << Luts::curveThrough(px, py);
    if (!l.next(&line))
        return fail(error, said("The LUT file ends too soon."));
    if (!three) {
        int const n = line.toInt();
        QVector<float> t(qsizetype(n) * 3);
        for (int i = 0; i < n; ++i) {
            QVector<double> v;
            if (!l.next(&line) || !numbersOf(line, &v) || v.size() != 3)
                return fail(error, said("Malformed 1D csp LUT: each line of LUT values needs 3 numbers."));
            for (int c = 0; c < 3; ++c)
                t[qsizetype(c) * n + i] = float(v[c]);
        }
        if (n < 2)
            return fail(error, said("A 1D LUT needs 2 entries at least."));
        *out << Luts::lut1d(t, n);
        return true;
    }
    QVector<double> dims;
    if (!numbersOf(line, &dims) || dims.size() != 3 || dims[0] != dims[1] || dims[1] != dims[2] || dims[0] < 2)
        return fail(error, said("Error while reading the size of the LUT."));
    int const n = int(dims[0]);
    QVector<float> cubeValues;
    cubeValues.reserve(qsizetype(n) * n * n * 3);
    while (l.next(&line) && cubeValues.size() < qsizetype(n) * n * n * 3) {
        QVector<double> v;
        if (!numbersOf(line, &v) || v.size() != 3)
            return fail(error, malformed(l));
        cubeValues << float(v[0]) << float(v[1]) << float(v[2]);
    }
    if (cubeValues.size() != qsizetype(n) * n * n * 3)
        return fail(error, said("Premature end of the LUT file."));
    *out << Luts::lut3d(cubeValues, n);
    return true;
}

bool discreet(QByteArray const& data, Chain* out, QString* error)
{
    Lines l(data);
    QString line;
    int tables = 1, length = 0;
    double outMax = 0.0;
    QVector<double> values;
    while (l.next(&line)) {
        if (line.startsWith(QLatin1String("LUT:"), Qt::CaseInsensitive)) {
            QStringList const w = wordsOf(line.mid(4));
            tables = w.value(0).toInt();
            length = w.value(1).toInt();
            if (w.size() > 2)
                outMax = std::pow(2.0, w.value(2).toInt()) - 1.0;
            continue;
        }
        QVector<double> v;
        if (!numbersOf(line, &v))
            return fail(error, said("Error parsing the Discreet LUT at line %1.").arg(l.at));
        values += v;
    }
    if (length == 0 && tables == 1)
        length = int(values.size());
    if ((tables != 1 && tables != 3 && tables != 4) || length < 2 || values.size() != qsizetype(tables) * length)
        return fail(error, said("The Discreet 1D LUT holds %1 values where %2 tables of %3 were expected.").arg(values.size()).arg(tables).arg(length));
    if (outMax <= 0.0) {
        double largest = 0.0;
        for (double v : values)
            largest = std::max(largest, v);
        outMax = std::max(double(length - 1), bitsFor(largest));
    }
    QVector<float> t(qsizetype(length) * 3);
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < length; ++i)
            t[qsizetype(c) * length + i] = float(values[qsizetype(tables == 1 ? 0 : c) * length + i] / outMax);
    *out << Luts::lut1d(t, length);
    return true;
}

}  // namespace

namespace Cms::Luts {

QStringList suffixes()
{
    return {QStringLiteral("cube"), QStringLiteral("itx"), QStringLiteral("3dl"), QStringLiteral("spi1d"), QStringLiteral("spi3d"), QStringLiteral("spimtx"),
            QStringLiteral("csp"), QStringLiteral("lut"), QStringLiteral("clf"), QStringLiteral("ctf"), QStringLiteral("cdl"), QStringLiteral("cc"),
            QStringLiteral("ccc")};
}

QString filter()
{
    QStringList globs;
    for (QString const& s : suffixes())
        globs << QStringLiteral("*.") + s;
    return QCoreApplication::translate("Colour", "Look-up tables (%1)").arg(globs.join(QLatin1Char(' ')));
}

Op lut1d(QVector<float> tables, int size, double low, double high)
{
    Op op;
    op.kind = Op::Lut1D;
    op.size = size;
    op.inLow = {low, low, low};
    op.inHigh = {high, high, high};
    op.table = std::make_shared<QVector<float> const>(std::move(tables));
    return op;
}

Op lut3d(QVector<float> cubeValues, int size, double low, double high)
{
    Op op;
    op.kind = Op::Lut3D;
    op.size = size;
    op.inLow = {low, low, low};
    op.inHigh = {high, high, high};
    op.table = std::make_shared<QVector<float> const>(std::move(cubeValues));
    return op;
}

Op curveThrough(std::array<QVector<double>, 3> const& x, std::array<QVector<double>, 3> const& y, int samples)
{
    QVector<float> t(qsizetype(samples) * 3);
    Op op;
    for (size_t c = 0; c < 3; ++c) {
        QVector<double> const& xs = x[c];
        QVector<double> const& ys = y[c];
        double const lo = xs.front(), hi = xs.back();
        op.inLow[c] = lo;
        op.inHigh[c] = hi;
        int k = 0;
        for (int i = 0; i < samples; ++i) {
            double const at = lo + (hi - lo) * i / (samples - 1);
            while (k + 2 < xs.size() && xs[k + 1] < at)
                ++k;
            double const span = xs[k + 1] - xs[k];
            double const f = span != 0.0 ? std::clamp((at - xs[k]) / span, 0.0, 1.0) : 0.0;
            t[qsizetype(c) * samples + i] = float(ys[k] + (ys[k + 1] - ys[k]) * f);
        }
    }
    Op made = lut1d(t, samples);
    made.inLow = op.inLow;
    made.inHigh = op.inHigh;
    return made;
}

bool readText(QByteArray const& data, QString const& suffixIn, Chain* out, QString* error, QString const& cccId, QString const& folder)
{
    out->clear();
    QString const suffix = suffixIn.toLower();
    if (data.trimmed().isEmpty())
        return fail(error, said("The LUT file is empty."));
    if (suffix == QLatin1String("cube") || suffix == QLatin1String("itx"))
        return cube(data, out, error);
    if (suffix == QLatin1String("3dl"))
        return threeDl(data, out, error);
    if (suffix == QLatin1String("spi1d"))
        return spi1d(data, out, error);
    if (suffix == QLatin1String("spi3d"))
        return spi3d(data, out, error);
    if (suffix == QLatin1String("spimtx"))
        return spimtx(data, out, error);
    if (suffix == QLatin1String("csp"))
        return csp(data, out, error);
    if (suffix == QLatin1String("lut"))
        return discreet(data, out, error);
    if (suffix == QLatin1String("clf") || suffix == QLatin1String("ctf"))
        return readClf(data, out, error, folder);
    if (suffix == QLatin1String("cdl") || suffix == QLatin1String("cc") || suffix == QLatin1String("ccc"))
        return readCdl(data, out, error, cccId);
    // (Not a known suffix: what the file starts with.)
    QByteArray const start = data.left(64).trimmed();
    if (start.startsWith("CSPLUTV100"))
        return csp(data, out, error);
    if (start.startsWith("SPILUT"))
        return spi3d(data, out, error);
    if (start.startsWith("<"))
        return data.contains("ProcessList") ? readClf(data, out, error, folder) : readCdl(data, out, error, cccId);
    if (data.contains("LUT_3D_SIZE") || data.contains("LUT_1D_SIZE"))
        return cube(data, out, error);
    return fail(error, said("Eclipse does not know the LUT format of this file (.%1).").arg(suffix));
}

bool read(QString const& path, Chain* out, QString* error, QString const& cccId)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, said("Cannot open the LUT file %1.").arg(QDir::toNativeSeparators(path)));
    QString why;
    if (!readText(f.readAll(), QFileInfo(path).suffix(), out, &why, cccId, QFileInfo(path).absolutePath()))
        return fail(error, said("%1 (%2)").arg(why, QFileInfo(path).fileName()));
    return true;
}

QByteArray writeCube(Chain const& chain, int size, QString const& title)
{
    QByteArray text;
    QTextStream s(&text);
    s << "# Made by Eclipse\n";
    s << "TITLE \"" << QString(title).remove(QLatin1Char('"')) << "\"\n";
    s << "LUT_3D_SIZE " << size << "\n";
    s.setRealNumberNotation(QTextStream::FixedNotation);
    s.setRealNumberPrecision(6);
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                float rgb[3] = {float(r) / (size - 1), float(g) / (size - 1), float(b) / (size - 1)};
                run(chain, rgb);
                s << rgb[0] << ' ' << rgb[1] << ' ' << rgb[2] << '\n';
            }
    s.flush();
    return text;
}

}  // namespace Cms::Luts
