// Eclipse - the colour spaces Eclipse knows without a configuration file, and the built-in transforms of OpenColorIO configurations.
#include "colour/spaces/ColourConfig.h"

#include <QCoreApplication>

#include <cmath>

namespace {

using namespace Cms;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

// A space of primaries and a curve, against the scene reference (ACES2065-1).
Space made(QString const& name, char const* description, char const* encoding, Primaries const& p, Curve curve, QStringList categories = {QStringLiteral("file-io")})
{
    Space s;
    s.name = name;
    s.description = said(description);
    s.encoding = QString::fromLatin1(encoding);
    s.categories = categories;
    s.primaries = p;
    s.curve = curve;
    s.curveKnown = true;
    s.hasFrom = true;
    s.fromReference << matrixOp(rgbToRgb(ap0Primaries(), p));
    if (curve != Curve::Linear)
        s.fromReference << curveOp(curve, true);
    s.fromReference = simplified(s.fromReference);
    return s;
}

Chain toXyzD65(Primaries const& p)
{
    return {matrixOp(rgbToXyzD65(p))};
}

// From CIE XYZ (D65) to the RGB of a display's primaries, its white adapted from D65 by Bradford.
Op fromXyzD65(Primaries const& p)
{
    return matrixOp(multiply(inverse(rgbToXyz(p)), bradford(kD65x, kD65y, p.wx, p.wy)));
}

Primaries awg3()
{
    return {0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020, kD65x, kD65y};
}

Primaries sGamut3()
{
    return {0.730, 0.280, 0.140, 0.855, 0.100, -0.050, kD65x, kD65y};
}

Primaries sGamut3Cine()
{
    return {0.766, 0.275, 0.225, 0.800, 0.089, -0.087, kD65x, kD65y};
}

// A camera log curve as OpenColorIO's LogCamera (log side slope and offset, lin side slope and offset, the break), log to light.
Op cameraLogDecode(double logSlope, double logOffset, double linSlope, double linOffset, double breakAt, double linearSlope = 0.0)
{
    Op op;
    op.kind = Op::LogCamera;
    op.base = 10.0;
    op.a = {logSlope, logSlope, logSlope};
    op.b = {logOffset, logOffset, logOffset};
    op.c = {linSlope, linSlope, linSlope};
    op.d = {linOffset, linOffset, linOffset};
    op.e = {breakAt, breakAt, breakAt};
    op.f = {linearSlope, linearSlope, linearSlope};
    op.inverse = true;
    return op;
}

}  // namespace

namespace Cms {

Config builtinConfig()
{
    Config c;
    c.title = QStringLiteral("eclipse-builtin");
    c.description = said("Eclipse's own colour spaces; the scene reference is ACES2065-1.");
    c.version = 2.1;
    Space aces;
    aces.name = QStringLiteral("ACES2065-1");
    aces.description = said("ACES2065-1 (Linear, AP0 primaries): the Academy Color Encoding System reference colour space");
    aces.encoding = QStringLiteral("scene-linear");
    aces.categories = {QStringLiteral("file-io")};
    aces.primaries = ap0Primaries();
    aces.curveKnown = true;
    c.spaces << aces;
    c.spaces << made(QStringLiteral("ACEScg"), "ACEScg (Linear, AP1 primaries): for rendering and compositing", "scene-linear", ap1Primaries(), Curve::Linear,
                     {QStringLiteral("file-io"), QStringLiteral("working-space")});
    c.spaces << made(QStringLiteral("ACEScct"), "ACEScct (log, AP1 primaries): for grading", "log", ap1Primaries(), Curve::AcesCct,
                     {QStringLiteral("file-io"), QStringLiteral("working-space")});
    c.spaces << made(QStringLiteral("ACEScc"), "ACEScc (log, AP1 primaries)", "log", ap1Primaries(), Curve::AcesCc);
    Space lin709 = made(QStringLiteral("Linear Rec.709 (sRGB)"), "Linear Rec.709 (sRGB): the primaries of sRGB, linear light", "scene-linear", rec709Primaries(),
                        Curve::Linear, {QStringLiteral("file-io"), QStringLiteral("working-space")});
    lin709.aliases = QStringList{QStringLiteral("Linear"), QStringLiteral("lin_srgb")};
    c.spaces << lin709;
    Space lin2020 = made(QStringLiteral("Linear Rec.2020"), "Rec. ITU-R BT.2020 primaries, linear light", "scene-linear", rec2020Primaries(), Curve::Linear);
    lin2020.aliases = QStringList{QStringLiteral("Rec.2020 Linear")};
    c.spaces << lin2020;
    Space linP3 = made(QStringLiteral("Linear P3-D65"), "P3 primaries with a D65 white, linear light", "scene-linear", p3d65Primaries(), Curve::Linear,
                       {QStringLiteral("file-io"), QStringLiteral("working-space")});
    linP3.aliases = QStringList{QStringLiteral("Display P3 Linear")};
    c.spaces << linP3;
    c.spaces << made(QStringLiteral("Linear Adobe RGB (1998)"), "Adobe RGB (1998) primaries, linear light", "scene-linear", adobeRgbPrimaries(), Curve::Linear);
    Space srgb = made(QStringLiteral("sRGB"), "sRGB IEC61966-2.1: most screens and picture files", "sdr-video", rec709Primaries(), Curve::Srgb,
                      {QStringLiteral("file-io"), QStringLiteral("working-space")});
    srgb.aliases = QStringList{QStringLiteral("srgb_display"), QStringLiteral("sRGB - Display")};
    c.spaces << srgb;
    c.spaces << made(QStringLiteral("Display P3"), "Display P3: P3-D65 primaries with the sRGB curve", "sdr-video", p3d65Primaries(), Curve::Srgb);
    c.spaces << made(QStringLiteral("Rec.709"), "Rec. ITU-R BT.709 (the camera curve)", "sdr-video", rec709Primaries(), Curve::Bt709);
    c.spaces << made(QStringLiteral("Rec.709 2.4"), "Rec.709 with Gamma 2.4 (ITU-R BT.1886)", "sdr-video", rec709Primaries(), Curve::Gamma24);
    c.spaces << made(QStringLiteral("Gamma 2.2 Rec.709"), "Rec.709 primaries with a pure gamma of 2.2", "sdr-video", rec709Primaries(), Curve::Gamma22);
    c.spaces << made(QStringLiteral("Rec.2020"), "Rec. ITU-R BT.2020 (the camera curve)", "sdr-video", rec2020Primaries(), Curve::Bt709);
    c.spaces << made(QStringLiteral("Rec.2020 2.4"), "Rec.2020 with Gamma 2.4 (ITU-R BT.1886)", "sdr-video", rec2020Primaries(), Curve::Gamma24);
    c.spaces << made(QStringLiteral("Adobe RGB (1998)"), "Adobe RGB (1998)", "sdr-video", adobeRgbPrimaries(), Curve::Gamma22);
    c.spaces << made(QStringLiteral("DCI-P3"), "DCI-P3: digital cinema, gamma 2.6, the DCI white", "sdr-video", p3dciPrimaries(), Curve::Gamma26);
    c.spaces << made(QStringLiteral("P3-D65"), "P3 primaries with a D65 white and gamma 2.6", "sdr-video", p3d65Primaries(), Curve::Gamma26);
    c.spaces << made(QStringLiteral("ProPhoto RGB"), "ProPhoto RGB (ROMM RGB)", "sdr-video", proPhotoPrimaries(), Curve::ProPhoto);
    c.spaces << made(QStringLiteral("Rec.2100 PQ"), "Rec.2100 with the PQ curve (ST 2084); 1.0 is 100 nits", "hdr-video", rec2020Primaries(), Curve::Pq);
    c.spaces << made(QStringLiteral("P3-D65 PQ"), "P3-D65 primaries with the PQ curve (ST 2084); 1.0 is 100 nits", "hdr-video", p3d65Primaries(), Curve::Pq);
    c.spaces << made(QStringLiteral("Rec.2100 HLG"), "Rec.2100 with the HLG curve (scene light)", "hdr-video", rec2020Primaries(), Curve::Hlg);
    Space xyz;
    xyz.name = QStringLiteral("CIE-XYZ-D65");
    xyz.aliases = QStringList{QStringLiteral("cie_xyz_d65")};
    xyz.description = said("CIE XYZ with a D65 white");
    xyz.encoding = QStringLiteral("scene-linear");
    xyz.hasFrom = true;
    xyz.fromReference = toXyzD65(ap0Primaries());
    c.spaces << xyz;
    Space raw;
    raw.name = QStringLiteral("Raw");
    raw.family = QStringLiteral("Utility");
    raw.description = said("Numbers, not colours: never converted");
    raw.isData = true;
    raw.encoding = QStringLiteral("data");
    c.spaces << raw;
    c.roles = {{QStringLiteral("aces_interchange"), QStringLiteral("ACES2065-1")},
               {QStringLiteral("cie_xyz_d65_interchange"), QStringLiteral("CIE-XYZ-D65")},
               {QStringLiteral("color_picking"), QStringLiteral("sRGB")},
               {QStringLiteral("color_timing"), QStringLiteral("ACEScct")},
               {QStringLiteral("compositing_log"), QStringLiteral("ACEScct")},
               {QStringLiteral("data"), QStringLiteral("Raw")},
               {QStringLiteral("default"), QStringLiteral("sRGB")},
               {QStringLiteral("matte_paint"), QStringLiteral("sRGB")},
               {QStringLiteral("scene_linear"), QStringLiteral("Linear Rec.709 (sRGB)")},
               {QStringLiteral("texture_paint"), QStringLiteral("sRGB")}};
    c.inactiveSpaces = QStringList{QStringLiteral("CIE-XYZ-D65"), QStringLiteral("Raw")};
    for (char const* name : {"sRGB", "Display P3", "Rec.709", "Rec.709 2.4", "Rec.2020", "Rec.2020 2.4", "Adobe RGB (1998)", "DCI-P3", "P3-D65", "Rec.2100 PQ", "P3-D65 PQ", "Rec.2100 HLG"}) {
        Display d;
        d.name = QString::fromLatin1(name);
        d.views << View{d.name, d.name, {}, {}, {}, {}};
        d.views << View{QStringLiteral("Raw"), QStringLiteral("Raw"), {}, {}, {}, {}};
        c.displays << d;
    }
    c.fileRules << FileRule{QStringLiteral("Default"), QStringLiteral("sRGB"), {}, {}, {}};
    return c;
}

bool builtinTransform(QString const& style, Chain* out, QString* error)
{
    out->clear();
    QString const s = style.trimmed();
    auto is = [&s](char const* name) { return s.compare(QLatin1String(name), Qt::CaseInsensitive) == 0; };
    if (is("IDENTITY"))
        return true;
    if (is("ACEScg_to_ACES2065-1")) {
        *out << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("ACEScct_to_ACES2065-1") || is("ACEScc_to_ACES2065-1")) {
        *out << curveOp(is("ACEScct_to_ACES2065-1") ? Curve::AcesCct : Curve::AcesCc) << matrixOp(rgbToRgb(ap1Primaries(), ap0Primaries()));
        return true;
    }
    if (is("UTILITY - ACES-AP0_to_CIE-XYZ-D65_BFD")) {
        *out = toXyzD65(ap0Primaries());
        return true;
    }
    if (is("UTILITY - ACES-AP1_to_CIE-XYZ-D65_BFD")) {
        *out = toXyzD65(ap1Primaries());
        return true;
    }
    if (is("UTILITY - ACES-AP1_to_LINEAR-REC709_BFD")) {
        *out << matrixOp(rgbToRgb(ap1Primaries(), rec709Primaries()));
        return true;
    }
    if (is("CURVE - ACEScct-LOG_to_LINEAR")) {
        *out << curveOp(Curve::AcesCct);
        return true;
    }
    if (is("CURVE - LINEAR_to_ACEScct-LOG")) {
        *out << curveOp(Curve::AcesCct, true);
        return true;
    }
    if (is("CURVE - ST-2084_to_LINEAR")) {
        *out << curveOp(Curve::Pq);
        return true;
    }
    if (is("CURVE - LINEAR_to_ST-2084")) {
        *out << curveOp(Curve::Pq, true);
        return true;
    }
    struct DisplayStyle {
        char const* name;
        Primaries p;
        Curve curve;
    };
    Primaries d60 = p3dciPrimaries();
    d60.wx = kAcesWx;
    d60.wy = kAcesWy;
    DisplayStyle const displays[] = {
        {"DISPLAY - CIE-XYZ-D65_to_sRGB", rec709Primaries(), Curve::Srgb},
        {"DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.709", rec709Primaries(), Curve::Gamma24},
        {"DISPLAY - CIE-XYZ-D65_to_REC.1886-REC.2020", rec2020Primaries(), Curve::Gamma24},
        {"DISPLAY - CIE-XYZ-D65_to_G2.2-REC.709", rec709Primaries(), Curve::Gamma22},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-DCI-BFD", p3dciPrimaries(), Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-D65", p3d65Primaries(), Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_G2.6-P3-D60-BFD", d60, Curve::Gamma26},
        {"DISPLAY - CIE-XYZ-D65_to_DisplayP3", p3d65Primaries(), Curve::Srgb},
        {"DISPLAY - CIE-XYZ-D65_to_ST2084-P3-D65", p3d65Primaries(), Curve::Pq},
        {"DISPLAY - CIE-XYZ-D65_to_REC.2100-PQ", rec2020Primaries(), Curve::Pq},
    };
    for (DisplayStyle const& d : displays)
        if (is(d.name)) {
            *out << fromXyzD65(d.p) << curveOp(d.curve, true);
            return true;
        }
    if (is("DISPLAY - CIE-XYZ-D65_to_REC.2100-HLG-1000nit")) {
        // Display light (1.0 = 100 nits) to a 1000-nit HLG signal: the system gamma of 1.2 taken back off, then the HLG curve.
        Op surround;
        surround.kind = Op::Fixed;
        surround.style = int(FixedStyle::Rec2100Surround);
        surround.a = {1.0 / 1.2, 0, 0};
        Matrix3 m = inverse(rgbToXyz(rec2020Primaries()));
        for (double& v : m)
            v *= 0.1;
        *out << matrixOp(m) << surround << curveOp(Curve::Hlg, true);
        return true;
    }
    if (is("ARRI_ALEXA-LOGC-EI800-AWG_to_ACES2065-1")) {
        *out << cameraLogDecode(0.247190, 0.385537, 5.555556, 0.052272, 0.010591, 5.367655) << matrixOp(rgbToRgb(awg3(), ap0Primaries()));
        return true;
    }
    if (is("SONY_SLOG3-SGAMUT3_to_ACES2065-1") || is("SONY_SLOG3-SGAMUT3.CINE_to_ACES2065-1")) {
        Op const decodeLog = cameraLogDecode(261.5 / 1023.0, 420.0 / 1023.0, 1.0 / 0.19, 0.01 / 0.19, 0.01125, (171.2102946929 - 95.0) / 0.01125 / 1023.0);
        *out << decodeLog << matrixOp(rgbToRgb(is("SONY_SLOG3-SGAMUT3_to_ACES2065-1") ? sGamut3() : sGamut3Cine(), ap0Primaries()));
        return true;
    }
    if (error)
        *error = said("Eclipse cannot do the built-in transform '%1'.").arg(style);
    return false;
}

}  // namespace Cms
