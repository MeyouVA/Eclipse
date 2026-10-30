// Eclipse - reading and writing ICC profiles made of a matrix and curves (display and input profiles, versions 2 and 4).
#include "colour/icc/Icc.h"

#include "colour/luts/Luts.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>

#include <cmath>
#include <functional>

namespace {

using namespace Cms;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

bool fail(QString* error, QString const& why)
{
    if (error)
        *error = why;
    return false;
}

quint32 u32(QByteArray const& d, qsizetype at)
{
    return at + 4 <= d.size() ? qFromBigEndian<quint32>(d.constData() + at) : 0;
}

quint16 u16(QByteArray const& d, qsizetype at)
{
    return at + 2 <= d.size() ? qFromBigEndian<quint16>(d.constData() + at) : 0;
}

double s15(QByteArray const& d, qsizetype at)
{
    return qint32(u32(d, at)) / 65536.0;
}

QByteArray sig(char const* four)
{
    return QByteArray(four, 4);
}

struct Tag {
    qsizetype offset = 0;
    qsizetype size = 0;
};

using Curve1 = std::function<double(double)>;

// The curve of a TRC tag as a function of 0..1, or an error.
bool curveOf(QByteArray const& d, Tag const& t, Curve1* out, QString* error)
{
    QByteArray const type = d.mid(t.offset, 4);
    if (type == "curv") {
        quint32 const count = u32(d, t.offset + 8);
        if (t.size < 12 + qsizetype(count) * 2)
            return fail(error, said("Illegal curve tag in ICC profile."));
        if (count == 0) {
            *out = [](double x) { return x; };
            return true;
        }
        if (count == 1) {
            double const g = u16(d, t.offset + 12) / 256.0;
            *out = [g](double x) { return std::pow(std::max(x, 0.0), g); };
            return true;
        }
        QVector<double> table(count);
        for (quint32 i = 0; i < count; ++i)
            table[i] = u16(d, t.offset + 12 + qsizetype(i) * 2) / 65535.0;
        *out = [table](double x) {
            double const at = std::clamp(x, 0.0, 1.0) * (table.size() - 1);
            int const i = std::min(int(at), int(table.size()) - 2);
            return table[i] + (table[i + 1] - table[i]) * (at - i);
        };
        return true;
    }
    if (type == "para") {
        int const kind = u16(d, t.offset + 8);
        static int const counts[] = {1, 3, 4, 5, 7};
        if (kind < 0 || kind > 4)
            return fail(error, said("Illegal curve tag in ICC profile."));
        if (t.size < 12 + counts[kind] * 4)
            return fail(error, kind == 0 ? said("Expecting 1 param in parametric curve tag (type 0) of ICC profile.") : said("Illegal curve tag in ICC profile."));
        double p[7] = {1, 1, 0, 1, 0, 0, 0};
        for (int i = 0; i < counts[kind]; ++i)
            p[i] = s15(d, t.offset + 12 + i * 4);
        double const g = p[0], a = p[1], b = p[2], c = p[3], dd = p[4], e = p[5], f = p[6];
        *out = [=](double x) {
            switch (kind) {
            case 0: return std::pow(std::max(x, 0.0), g);
            case 1: return x >= -b / a ? std::pow(std::max(a * x + b, 0.0), g) : 0.0;
            case 2: return x >= -b / a ? std::pow(std::max(a * x + b, 0.0), g) + c : c;
            case 3: return x >= dd ? std::pow(std::max(a * x + b, 0.0), g) : c * x;
            default: return x >= dd ? std::pow(std::max(a * x + b, 0.0), g) + e : c * x + f;
            }
        };
        return true;
    }
    return fail(error, said("Illegal curve tag in ICC profile."));
}

QString textOf(QByteArray const& d, Tag const& t)
{
    QByteArray const type = d.mid(t.offset, 4);
    if (type == "desc") {
        quint32 const count = u32(d, t.offset + 8);
        return QString::fromLatin1(d.mid(t.offset + 12, qsizetype(count))).remove(QChar(0)).trimmed();
    }
    if (type == "text")
        return QString::fromLatin1(d.mid(t.offset + 8, t.size - 8)).remove(QChar(0)).trimmed();
    if (type == "mluc") {
        quint32 const records = u32(d, t.offset + 8);
        if (records == 0)
            return {};
        quint32 const length = u32(d, t.offset + 16 + 4);
        quint32 const at = u32(d, t.offset + 16 + 8);
        QByteArray const raw = d.mid(t.offset + at, qsizetype(length));
        QString s;
        for (qsizetype i = 0; i + 1 < raw.size(); i += 2)
            s += QChar(ushort((uchar(raw[i]) << 8) | uchar(raw[i + 1])));
        return s.remove(QChar(0)).trimmed();
    }
    return {};
}

void xy(double X, double Y, double Z, double* x, double* y)
{
    double const sum = X + Y + Z;
    *x = sum != 0 ? X / sum : 0.0;
    *y = sum != 0 ? Y / sum : 0.0;
}

// Writing.
struct Out {
    QByteArray data;
    void u32(quint32 v)
    {
        char b[4];
        qToBigEndian(v, b);
        data.append(b, 4);
    }
    void u16(quint16 v)
    {
        char b[2];
        qToBigEndian(v, b);
        data.append(b, 2);
    }
    void s15(double v) { u32(quint32(qint32(std::lround(v * 65536.0)))); }
    void pad()
    {
        while (data.size() % 4)
            data.append('\0');
    }
};

QByteArray mluc(QString const& text)
{
    Out o;
    o.data += "mluc";
    o.u32(0);
    o.u32(1);
    o.u32(12);
    o.data += "enUS";
    o.u32(quint32(text.size() * 2));
    o.u32(28);
    for (QChar c : text)
        o.u16(c.unicode());
    o.pad();
    return o.data;
}

QByteArray xyzTag(double X, double Y, double Z)
{
    Out o;
    o.data += "XYZ ";
    o.u32(0);
    o.s15(X);
    o.s15(Y);
    o.s15(Z);
    return o.data;
}

QByteArray trcTag(Curve curve)
{
    Out o;
    auto para3 = [&o](double g, double a, double b, double c, double d) {
        o.data += "para";
        o.u32(0);
        o.u16(3);
        o.u16(0);
        for (double v : {g, a, b, c, d})
            o.s15(v);
    };
    auto para0 = [&o](double g) {
        o.data += "para";
        o.u32(0);
        o.u16(0);
        o.u16(0);
        o.s15(g);
    };
    switch (curve) {
    case Curve::Linear: para0(1.0); break;
    case Curve::Gamma18: para0(1.8); break;
    case Curve::Gamma22: para0(2.2); break;
    case Curve::Gamma24: para0(2.4); break;
    case Curve::Gamma26: para0(2.6); break;
    case Curve::Srgb: para3(2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045); break;
    case Curve::Bt709: para3(1.0 / 0.45, 1.0 / 1.099, 0.099 / 1.099, 1.0 / 4.5, 0.081); break;
    case Curve::ProPhoto: para3(1.8, 1.0, 0.0, 1.0 / 16.0, 16.0 / 512.0); break;
    default: {
        // (A curve with no formula in ICC terms: a table, light scaled so the top of the code is the most.)
        int const n = 4096;
        double const top = std::max(decode(curve, 1.0), 1e-9);
        o.data += "curv";
        o.u32(0);
        o.u32(n);
        for (int i = 0; i < n; ++i)
            o.u16(quint16(std::lround(std::clamp(decode(curve, double(i) / (n - 1)) / top, 0.0, 1.0) * 65535.0)));
        break;
    }
    }
    o.pad();
    return o.data;
}

}  // namespace

namespace Cms::Icc {

bool read(QByteArray const& d, Profile* out, QString* error)
{
    if (d.size() < 132 || qsizetype(u32(d, 0)) > d.size())
        return fail(error, said("ICC profile too short."));
    if (d.mid(36, 4) != "acsp")
        return fail(error, said("This is not an ICC profile."));
    Profile p;
    p.version = uchar(d[8]);
    QByteArray const space = d.mid(16, 4);
    p.grey = space == "GRAY";
    if (space != "RGB " && !p.grey)
        return fail(error, said("Eclipse reads RGB and grey ICC profiles; this one is for %1.").arg(QString::fromLatin1(space).trimmed()));
    if (d.mid(20, 4) != "XYZ ")
        return fail(error, said("Eclipse reads ICC profiles with an XYZ connection space."));
    double const illX = s15(d, 68), illY = s15(d, 72), illZ = s15(d, 76);
    double pcsX = kD50x, pcsY = kD50y;
    if (illY > 0)
        xy(illX, illY, illZ, &pcsX, &pcsY);
    quint32 const count = u32(d, 128);
    QHash<QByteArray, Tag> tags;
    for (quint32 i = 0; i < count; ++i) {
        qsizetype const at = 132 + qsizetype(i) * 12;
        if (at + 12 > d.size())
            return fail(error, said("ICC profile too short."));
        Tag t{qsizetype(u32(d, at + 4)), qsizetype(u32(d, at + 8))};
        if (t.offset + t.size > d.size() || t.size < 8)
            return fail(error, said("ICC profile tag outside profile."));
        tags.insert(d.mid(at, 4), t);
    }
    if (tags.contains(sig("desc")))
        p.description = textOf(d, tags.value(sig("desc")));
    if (tags.contains(sig("cprt")))
        p.copyright = textOf(d, tags.value(sig("cprt")));
    auto xyzOf = [&](char const* name, double v[3]) {
        if (!tags.contains(sig(name)))
            return false;
        Tag const t = tags.value(sig(name));
        if (d.mid(t.offset, 4) != "XYZ " || t.size < 20)
            return false;
        for (int i = 0; i < 3; ++i)
            v[i] = s15(d, t.offset + 8 + i * 4);
        return true;
    };
    double white[3] = {illX, illY, illZ};
    xyzOf("wtpt", white);
    if (tags.contains(sig("chad"))) {
        Tag const t = tags.value(sig("chad"));
        if (d.mid(t.offset, 4) == "sf32" && t.size >= 44)
            for (int i = 0; i < 9; ++i)
                p.adaptation[size_t(i)] = s15(d, t.offset + 8 + i * 4);
    }
    std::array<Curve1, 3> curves;
    if (p.grey) {
        if (!tags.contains(sig("kTRC")))
            return fail(error, said("The grey ICC profile has no curve (kTRC)."));
        Curve1 k;
        if (!curveOf(d, tags.value(sig("kTRC")), &k, error))
            return false;
        curves = {k, k, k};
        Matrix3 const toD50 = multiply(bradford(kD65x, kD65y, pcsX, pcsY), rgbToXyz(rec709Primaries()));
        p.toXyzD50 = toD50;
    } else {
        double r[3], g[3], b[3];
        if (!xyzOf("rXYZ", r) || !xyzOf("gXYZ", g) || !xyzOf("bXYZ", b)) {
            if (tags.contains(sig("A2B0")) || tags.contains(sig("B2A0")))
                return fail(error, said("Eclipse reads ICC profiles made of a matrix and curves; this one is made of look-up tables."));
            return fail(error, said("Illegal matrix tag in ICC profile."));
        }
        p.toXyzD50 = {r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
        char const* const names[3] = {"rTRC", "gTRC", "bTRC"};
        for (int c = 0; c < 3; ++c) {
            if (!tags.contains(sig(names[c])))
                return fail(error, said("Curves with no values in ICC profile."));
            if (!curveOf(d, tags.value(sig(names[c])), &curves[size_t(c)], error))
                return false;
        }
    }
    // The curves as one 1D LUT of three tables.
    int const n = 4096;
    QVector<float> table(qsizetype(n) * 3);
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < n; ++i)
            table[qsizetype(c) * n + i] = float(curves[size_t(c)](double(i) / (n - 1)));
    bool straight = true;
    for (int i = 0; i < n && straight; ++i)
        for (int c = 0; c < 3; ++c)
            straight = straight && std::abs(table[qsizetype(c) * n + i] - float(i) / (n - 1)) < 1e-6f;
    if (!straight)
        p.decode << Luts::lut1d(table, n);
    // The device's own primaries and white: the matrix taken back from D50 to its white.
    Matrix3 native = p.toXyzD50;
    if (p.adaptation != identity3()) {
        native = multiply(inverse(p.adaptation), p.toXyzD50);
    } else {
        double wx = pcsX, wy = pcsY;
        xy(white[0], white[1], white[2], &wx, &wy);
        native = multiply(bradford(pcsX, pcsY, wx, wy), p.toXyzD50);
    }
    xy(native[0], native[3], native[6], &p.primaries.rx, &p.primaries.ry);
    xy(native[1], native[4], native[7], &p.primaries.gx, &p.primaries.gy);
    xy(native[2], native[5], native[8], &p.primaries.bx, &p.primaries.by);
    xy(native[0] + native[1] + native[2], native[3] + native[4] + native[5], native[6] + native[7] + native[8], &p.whiteX, &p.whiteY);
    p.primaries.wx = p.whiteX;
    p.primaries.wy = p.whiteY;
    *out = p;
    return true;
}

bool spaceOf(Profile const& p, Config const& config, Space* out, QString* error)
{
    Space const* xyz = config.space(QStringLiteral("cie_xyz_d65_interchange"));
    if (!xyz)
        xyz = config.space(QStringLiteral("CIE-XYZ-D65"));
    if (!xyz)
        return fail(error, said("The colour configuration has no CIE XYZ D65 interchange role, which ICC profiles need."));
    Chain chain = p.decode;
    // To the connection space (D50), then to D65 by Bradford, as screens are measured.
    Matrix3 toD65 = multiply(bradford(kD50x, kD50y, kD65x, kD65y), p.toXyzD50);
    if (p.adaptation != identity3())
        toD65 = multiply(bradford(p.whiteX, p.whiteY, kD65x, kD65y), multiply(inverse(p.adaptation), p.toXyzD50));
    chain << matrixOp(toD65);
    Chain back;
    if (xyz->hasTo) {
        back = xyz->toReference;
    } else if (xyz->hasFrom && !invert(xyz->fromReference, &back, error)) {
        return false;
    }
    chain += back;
    Space s;
    s.name = p.description.isEmpty() ? said("ICC profile") : p.description;
    s.family = QStringLiteral("ICC");
    s.description = p.copyright.isEmpty() ? s.name : s.name + QLatin1String(" - ") + p.copyright;
    s.encoding = QStringLiteral("sdr-video");
    s.categories = {QStringLiteral("file-io")};
    s.hasTo = true;
    s.toReference = simplified(chain);
    s.primaries = p.primaries;
    *out = s;
    return true;
}

bool spaceOfFile(QString const& path, Config const& config, Space* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, said("Cannot open the ICC profile %1.").arg(QDir::toNativeSeparators(path)));
    Profile p;
    if (!read(f.readAll(), &p, error) || !spaceOf(p, config, out, error))
        return false;
    if (p.description.isEmpty())
        out->name = QFileInfo(path).completeBaseName();
    out->file = QFileInfo(path).absoluteFilePath();
    return true;
}

QByteArray write(Primaries const& primaries, Curve curve, QString const& description)
{
    if (!primaries.valid())
        return {};
    constexpr double d50X = 0.9642, d50Y = 1.0, d50Z = 0.8249;
    double pcsX = 0, pcsY = 0;
    xy(d50X, d50Y, d50Z, &pcsX, &pcsY);
    Matrix3 const chad = bradford(primaries.wx, primaries.wy, pcsX, pcsY);
    Matrix3 const m = multiply(chad, rgbToXyz(primaries));
    QByteArray const trc = trcTag(curve);
    Out chadTag;
    chadTag.data += "sf32";
    chadTag.u32(0);
    for (double v : chad)
        chadTag.s15(v);
    QVector<QPair<QByteArray, QByteArray>> tags = {
        {"desc", mluc(description)},
        {"cprt", mluc(QStringLiteral("No copyright, use freely"))},
        {"wtpt", xyzTag(d50X, d50Y, d50Z)},
        {"chad", chadTag.data},
        {"rXYZ", xyzTag(m[0], m[3], m[6])},
        {"gXYZ", xyzTag(m[1], m[4], m[7])},
        {"bXYZ", xyzTag(m[2], m[5], m[8])},
        {"rTRC", trc},
        {"gTRC", trc},
        {"bTRC", trc},
    };
    int const tableEnd = 132 + int(tags.size()) * 12;
    Out body;
    QVector<QPair<quint32, quint32>> places;
    qsizetype trcAt = -1;
    for (auto const& t : tags) {
        // (The three curves share one copy of their data.)
        if (t.second == trc && trcAt >= 0) {
            places << qMakePair(quint32(trcAt), quint32(trc.size()));
            continue;
        }
        qsizetype const at = tableEnd + body.data.size();
        if (t.second == trc)
            trcAt = at;
        places << qMakePair(quint32(at), quint32(t.second.size()));
        body.data += t.second;
        body.pad();
    }
    Out o;
    quint32 const size = quint32(tableEnd + body.data.size());
    o.u32(size);
    o.u32(0);
    o.u32(0x04300000);
    o.data += "mntr";
    o.data += "RGB ";
    o.data += "XYZ ";
    QDateTime const now = QDateTime::currentDateTimeUtc();
    for (int v : {now.date().year(), now.date().month(), now.date().day(), now.time().hour(), now.time().minute(), now.time().second()})
        o.u16(quint16(v));
    o.data += "acsp";
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.s15(d50X);
    o.s15(d50Y);
    o.s15(d50Z);
    o.u32(0);
    o.data.append(QByteArray(16 + 28, '\0'));
    o.u32(quint32(tags.size()));
    for (int i = 0; i < tags.size(); ++i) {
        o.data += tags[i].first;
        o.u32(places[i].first);
        o.u32(places[i].second);
    }
    o.data += body.data;
    return o.data;
}

QByteArray profileOf(QString const& spaceName)
{
    std::shared_ptr<Config const> const c = config();
    Space const* s = c->space(spaceName);
    if (!s)
        return {};
    if (!s->file.isEmpty()) {
        QFile f(s->file);
        if (f.open(QIODevice::ReadOnly))
            return f.readAll();
    }
    if (s->primaries.valid() && s->curveKnown)
        return write(s->primaries, s->curve, s->name);
    Config const builtin = builtinConfig();
    Space const* const same = builtin.space(s->name);
    if (same && same->primaries.valid() && same->curveKnown)
        return write(same->primaries, same->curve, same->name);
    return {};
}

}  // namespace Cms::Icc
