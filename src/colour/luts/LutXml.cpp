// Eclipse - the XML LUT formats: Academy/ASC Common LUT Format (.clf, .ctf) and ASC colour decisions (.cdl, .cc, .ccc).
#include "colour/luts/Luts.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <cmath>

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

struct Element {
    QString name;
    QXmlStreamAttributes attributes;
    QString text;
    QVector<Element> children;

    QString attr(char const* key, QString const& fallback = {}) const
    {
        for (QXmlStreamAttribute const& a : attributes)
            if (a.name().compare(QLatin1String(key), Qt::CaseInsensitive) == 0)
                return a.value().toString();
        return fallback;
    }
    double number(char const* key, double fallback) const
    {
        bool ok = false;
        double const v = attr(key).toDouble(&ok);
        return ok ? v : fallback;
    }
    Element const* child(char const* key) const
    {
        for (Element const& c : children)
            if (c.name.compare(QLatin1String(key), Qt::CaseInsensitive) == 0)
                return &c;
        return nullptr;
    }
    QVector<double> numbers() const
    {
        QVector<double> out;
        for (QString const& w : text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts))
            out << w.toDouble();
        return out;
    }
};

bool parseXml(QByteArray const& data, Element* root, QString* error)
{
    QXmlStreamReader xml(data);
    QVector<Element*> stack;
    Element top;
    stack << &top;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            Element e;
            e.name = xml.name().toString();
            e.attributes = xml.attributes();
            stack.back()->children << e;
            stack << &stack.back()->children.back();
        } else if (xml.isEndElement()) {
            stack.removeLast();
        } else if (xml.isCharacters() && stack.size() > 1) {
            stack.back()->text += xml.text();
        }
    }
    if (xml.hasError())
        return fail(error, said("The XML cannot be read (line %1): %2").arg(xml.lineNumber()).arg(xml.errorString()));
    if (top.children.isEmpty())
        return fail(error, said("The file holds no XML."));
    *root = top.children.first();
    return true;
}

double depthScale(QString const& depth)
{
    QString const d = depth.toLower();
    if (d == QLatin1String("8i"))
        return 255.0;
    if (d == QLatin1String("10i"))
        return 1023.0;
    if (d == QLatin1String("12i"))
        return 4095.0;
    if (d == QLatin1String("16i"))
        return 65535.0;
    return 1.0;
}

Op cdlOf(Element const& cc)
{
    Op op;
    op.kind = Op::Cdl;
    Element const* const sop = cc.child("SOPNode");
    auto three = [sop](char const* key, double fallback) {
        std::array<double, 3> v{fallback, fallback, fallback};
        if (Element const* const e = sop ? sop->child(key) : nullptr) {
            QVector<double> const n = e->numbers();
            for (int i = 0; i < 3 && i < n.size(); ++i)
                v[size_t(i)] = n[i];
        }
        return v;
    };
    op.a = three("Slope", 1.0);
    op.b = three("Offset", 0.0);
    op.c = three("Power", 1.0);
    Element const* sat = cc.child("SatNode");
    if (!sat)
        sat = cc.child("SATNode");
    double s = 1.0;
    if (sat)
        if (Element const* const v = sat->child("Saturation"))
            s = v->numbers().value(0, 1.0);
    op.e = {s, 0, 0};
    return op;
}

void corrections(Element const& e, QVector<Element const*>* out)
{
    if (e.name.compare(QLatin1String("ColorCorrection"), Qt::CaseInsensitive) == 0) {
        *out << &e;
        return;
    }
    for (Element const& c : e.children)
        corrections(c, out);
}

// One step of a ProcessList.
bool clfOp(Element const& e, Chain* out, QString* error, QString const& folder)
{
    double const inScale = depthScale(e.attr("inBitDepth", QStringLiteral("32f")));
    double const outScale = depthScale(e.attr("outBitDepth", QStringLiteral("32f")));
    QString const kind = e.name;
    auto is = [&kind](char const* name) { return kind.compare(QLatin1String(name), Qt::CaseInsensitive) == 0; };
    if (is("Description") || is("InputDescriptor") || is("OutputDescriptor") || is("Info"))
        return true;
    if (is("Matrix")) {
        Element const* const a = e.child("Array");
        QVector<double> const v = a ? a->numbers() : QVector<double>();
        QStringList const dim = a ? a->attr("dim").split(QLatin1Char(' '), Qt::SkipEmptyParts) : QStringList();
        int const cols = dim.value(1).toInt();
        if (!a || (cols != 3 && cols != 4) || v.size() != 3 * cols)
            return fail(error, said("The Matrix of the LUT is not 3 by 3 or 3 by 4."));
        double const k = inScale / outScale;
        Matrix3 m{};
        std::array<double, 3> off{0, 0, 0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                m[size_t(r * 3 + c)] = v[r * cols + c] * k;
            if (cols == 4)
                off[size_t(r)] = v[r * cols + 3] / outScale;
        }
        *out << matrixOp(m, off);
        return true;
    }
    if (is("LUT1D") || is("InvLUT1D")) {
        if (e.attr("halfDomain") == QLatin1String("true"))
            return fail(error, said("Eclipse does not read 1D LUTs over half-float numbers."));
        Element const* const a = e.child("Array");
        QVector<double> const v = a ? a->numbers() : QVector<double>();
        QStringList const dim = a ? a->attr("dim").split(QLatin1Char(' '), Qt::SkipEmptyParts) : QStringList();
        int const n = dim.value(0).toInt(), comps = dim.value(1).toInt();
        if (n < 2 || (comps != 1 && comps != 3) || v.size() != qsizetype(n) * comps)
            return fail(error, said("A 1D LUT needs 2 entries at least, of 1 or 3 values."));
        double const scale = is("LUT1D") ? outScale : inScale;
        QVector<float> t(qsizetype(n) * 3);
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c)
                t[qsizetype(c) * n + i] = float(v[qsizetype(i) * comps + (comps == 1 ? 0 : c)] / scale);
        Op op = Luts::lut1d(t, n);
        op.inverse = is("InvLUT1D");
        *out << op;
        return true;
    }
    if (is("LUT3D")) {
        Element const* const a = e.child("Array");
        QVector<double> const v = a ? a->numbers() : QVector<double>();
        QStringList const dim = a ? a->attr("dim").split(QLatin1Char(' '), Qt::SkipEmptyParts) : QStringList();
        int const n = dim.value(0).toInt();
        if (n < 2 || v.size() != qsizetype(n) * n * n * 3)
            return fail(error, said("The 3D LUT does not hold the values its size needs."));
        QVector<float> cubeValues(qsizetype(n) * n * n * 3);
        for (int r = 0; r < n; ++r)
            for (int g = 0; g < n; ++g)
                for (int b = 0; b < n; ++b)
                    for (int c = 0; c < 3; ++c)
                        cubeValues[((qsizetype(b) * n + g) * n + r) * 3 + c] = float(v[((qsizetype(r) * n + g) * n + b) * 3 + c] / outScale);
        Op op = Luts::lut3d(cubeValues, n);
        op.tetrahedral = e.attr("interpolation").compare(QLatin1String("trilinear"), Qt::CaseInsensitive) != 0;
        *out << op;
        return true;
    }
    if (is("InvLUT3D"))
        return fail(error, said("A 3D LUT cannot be run backwards."));
    if (is("Range")) {
        auto value = [&e](char const* key, double* v) {
            Element const* const c = e.child(key);
            if (c)
                *v = c->numbers().value(0, 0.0);
            return c != nullptr;
        };
        double minIn = 0, maxIn = 1, minOut = 0, maxOut = 1;
        bool const hasMin = value("minInValue", &minIn) & value("minOutValue", &minOut);
        bool const hasMax = value("maxInValue", &maxIn) & value("maxOutValue", &maxOut);
        if (!hasMin && !hasMax)
            return true;
        Op op = rangeOp(minIn / inScale, maxIn / inScale, minOut / outScale, maxOut / outScale, e.attr("style").compare(QLatin1String("noClamp"), Qt::CaseInsensitive) != 0);
        op.style = hasMin && hasMax ? 0 : hasMin ? 1 : 2;
        *out << op;
        return true;
    }
    if (is("Log")) {
        QString const style = e.attr("style").toLower();
        Op op;
        Element const* const p = e.child("LogParams");
        if (style == QLatin1String("log10") || style == QLatin1String("antilog10") || style == QLatin1String("log2") || style == QLatin1String("antilog2")) {
            op.kind = Op::Log;
            op.base = style.endsWith(QLatin1String("10")) ? 10.0 : 2.0;
            op.inverse = style.startsWith(QLatin1String("anti"));
            *out << op;
            return true;
        }
        bool const camera = style.startsWith(QLatin1String("camera"));
        op.kind = camera ? Op::LogCamera : Op::LogAffine;
        op.base = p ? p->number("base", 2.0) : 2.0;
        op.a = {1, 1, 1};
        op.c = {1, 1, 1};
        for (Element const& params : e.children) {
            if (params.name != QLatin1String("LogParams"))
                continue;
            QString const ch = params.attr("channel");
            for (int c = 0; c < 3; ++c) {
                if (!ch.isEmpty() && ch != QStringLiteral("RGB").mid(c, 1))
                    continue;
                size_t const k = size_t(c);
                op.a[k] = params.number("logSideSlope", 1.0);
                op.b[k] = params.number("logSideOffset", 0.0);
                op.c[k] = params.number("linSideSlope", 1.0);
                op.d[k] = params.number("linSideOffset", 0.0);
                op.e[k] = params.number("linSideBreak", 0.0);
                op.f[k] = params.number("linearSlope", 0.0);
            }
        }
        op.inverse = style == QLatin1String("logtolin") || style == QLatin1String("cameralogtolin");
        if (style != QLatin1String("lintolog") && style != QLatin1String("logtolin") && !camera)
            return fail(error, said("Eclipse does not know the Log style '%1'.").arg(e.attr("style")));
        *out << op;
        return true;
    }
    if (is("Exponent")) {
        QString const style = e.attr("style");
        Op op;
        bool const mon = style.startsWith(QLatin1String("monCurve"), Qt::CaseInsensitive);
        op.kind = mon ? Op::MonCurve : Op::Exponent;
        op.a = {1, 1, 1};
        for (Element const& params : e.children) {
            if (params.name != QLatin1String("ExponentParams"))
                continue;
            QString const ch = params.attr("channel");
            for (int c = 0; c < 3; ++c) {
                if (!ch.isEmpty() && ch != QStringLiteral("RGB").mid(c, 1))
                    continue;
                op.a[size_t(c)] = params.number("exponent", 1.0);
                op.b[size_t(c)] = params.number("offset", 0.0);
            }
        }
        op.style = style.contains(QLatin1String("Mirror"), Qt::CaseInsensitive) ? 1 : style.contains(QLatin1String("PassThru"), Qt::CaseInsensitive) ? 2 : 0;
        op.inverse = style.endsWith(QLatin1String("Rev"), Qt::CaseInsensitive);
        *out << op;
        return true;
    }
    if (is("ASC_CDL")) {
        Op op = cdlOf(e);
        QString const style = e.attr("style").toLower();
        op.style = style.contains(QLatin1String("noclamp")) ? 1 : 0;
        op.inverse = style.startsWith(QLatin1String("rev"));
        *out << op;
        return true;
    }
    if (is("FixedFunction")) {
        QString const style = e.attr("style").toUpper();
        Op op;
        op.kind = Op::Fixed;
        struct Name {
            char const* forward;
            char const* back;
            FixedStyle style;
        };
        Name const names[] = {{"RGB_TO_HSV", "HSV_TO_RGB", FixedStyle::RgbToHsv}, {"XYZ_TO_XYY", "XYY_TO_XYZ", FixedStyle::XyzToXyy},
                              {"XYZ_TO_UVY", "UVY_TO_XYZ", FixedStyle::XyzToUvy}, {"XYZ_TO_LUV", "LUV_TO_XYZ", FixedStyle::XyzToLuv}};
        for (Name const& n : names)
            if (style == QLatin1String(n.forward) || style == QLatin1String(n.back)) {
                op.style = int(n.style);
                op.inverse = style == QLatin1String(n.back);
                *out << op;
                return true;
            }
        return fail(error, said("Eclipse cannot do the fixed function '%1'.").arg(e.attr("style")));
    }
    if (is("Reference")) {
        QString const path = e.attr("path");
        if (path.isEmpty())
            return fail(error, said("A Reference in the LUT names no file."));
        Chain inner;
        if (!Luts::read(QDir(folder).filePath(path), &inner, error))
            return false;
        if (e.attr("inverted") == QLatin1String("true") && !invert(inner, &inner, error))
            return false;
        *out += inner;
        return true;
    }
    return fail(error, said("Eclipse cannot do the %1 step of this LUT.").arg(kind));
}

}  // namespace

namespace Cms::Luts {

bool readClf(QByteArray const& data, Chain* out, QString* error, QString const& folder)
{
    Element root;
    if (!parseXml(data, &root, error))
        return false;
    if (root.name.compare(QLatin1String("ProcessList"), Qt::CaseInsensitive) != 0)
        return fail(error, said("The file is not a Common LUT Format process list."));
    out->clear();
    for (Element const& e : root.children)
        if (!clfOp(e, out, error, folder))
            return false;
    if (root.attr("inverse") == QLatin1String("true"))
        return invert(*out, out, error);
    return true;
}

bool readCdl(QByteArray const& data, Chain* out, QString* error, QString const& cccId)
{
    Element root;
    if (!parseXml(data, &root, error))
        return false;
    QVector<Element const*> found;
    corrections(root, &found);
    if (found.isEmpty())
        return fail(error, said("The file holds no colour correction."));
    Element const* pick = found.first();
    if (!cccId.isEmpty()) {
        pick = nullptr;
        for (Element const* e : found)
            if (e->attr("id") == cccId)
                pick = e;
        bool number = false;
        int const index = cccId.toInt(&number);
        if (!pick && number && index >= 0 && index < found.size())
            pick = found[index];
        if (!pick)
            return fail(error, said("The file has no colour correction '%1'.").arg(cccId));
    }
    out->clear();
    *out << cdlOf(*pick);
    return true;
}

}  // namespace Cms::Luts
