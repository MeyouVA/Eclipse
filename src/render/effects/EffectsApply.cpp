// Eclipse - the effects done the same way on a layer and as a node: those that need only their picture and their settings.
#include "render/effects/Effects.h"
#include "render/effects/EffectsPrivate.h"

#include "colour/images/ColourTransform.h"

#include <QHash>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include "colour/images/DeepPixels.h"

namespace Effects {

void variableBlur(QImage& image, QImage const& matteIn, double white, double black, bool keepInside)
{
    detail::premultiplied(image);
    QImage const matte = matteIn.format() == QImage::Format_ARGB32_Premultiplied ? matteIn : matteIn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage const original = image;
    if (black >= 0.5) {
        // Blurred `black` everywhere first, then towards `white` as the matte is lighter.
        QImage soft = image;
        blur(soft, black);
        QImage sharp = image;
        image = soft;
        QImage whiteSoft = sharp;
        blur(whiteSoft, white);
        for (int y = 0; y < image.height() && y < matte.height(); ++y) {
            QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
            QRgb const* const most = reinterpret_cast<QRgb const*>(whiteSoft.constScanLine(y));
            QRgb const* const by = reinterpret_cast<QRgb const*>(matte.constScanLine(y));
            for (int x = 0; x < image.width() && x < matte.width(); ++x) {
                int const k = (qRed(by[x]) * 299 + qGreen(by[x]) * 587 + qBlue(by[x]) * 114) / 1000;
                QRgb const a = line[x], b = most[x];
                auto mix = [k](int p, int q) { return p + (q - p) * k / 255; };
                line[x] = qRgba(mix(qRed(a), qRed(b)), mix(qGreen(a), qGreen(b)), mix(qBlue(a), qBlue(b)), mix(qAlpha(a), qAlpha(b)));
            }
        }
    } else {
        variableBlur(image, matte, white);
    }
    if (!keepInside)
        return;
    // No more opaque than it was: the blur stays inside the edge of what is painted.
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
        QRgb const* const was = reinterpret_cast<QRgb const*>(original.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(line[x]), most = qAlpha(was[x]);
            if (a <= most)
                continue;
            line[x] = most == 0 ? 0 : qRgba(qRed(line[x]) * most / a, qGreen(line[x]) * most / a, qBlue(line[x]) * most / a, most);
        }
    }
}

void matteBlurred(QImage& image, Effect const& e, double radius)
{
    if (e.value("blurType") >= 1.0)
        directionalBlur(image, radius * 2.0, e.value("angle"), e.value("falloff"), 0);
    else
        blur(image, radius);
    if (!e.flag("useMatteColour"))
        return;
    QColor const colour = e.colourValue("matteColour");
    double const gain = e.value("colourGain");
    bool const multiply = e.flag("multiplicative");
    detail::perPixel(image, [&](double& r, double& g, double& b, double&, int, int) {
        double const cr = colour.redF() * gain, cg = colour.greenF() * gain, cb = colour.blueF() * gain;
        r = multiply ? r * cr : cr;
        g = multiply ? g * cg : cg;
        b = multiply ? b * cb : cb;
    });
}

QImage sequenceFrame(QString const& first, int frame)
{
    static QMutex lock;
    static QHash<QString, QImage> pictures;
    static QHash<QString, int> lengths;
    QMutexLocker const locked(&lock);
    QFileInfo const info(first);
    QRegularExpressionMatch const digits = QRegularExpression(QStringLiteral("(\\d+)$")).match(info.completeBaseName());
    auto numbered = [&](int n) {
        if (!digits.hasMatch())
            return first;
        QString const base = info.completeBaseName().left(digits.capturedStart(1));
        QString const number = QStringLiteral("%1").arg(n, int(digits.capturedLength(1)), 10, QLatin1Char('0'));
        return info.dir().filePath(base + number + (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix()));
    };
    int const start = digits.hasMatch() ? digits.captured(1).toInt() : 0;
    if (!lengths.contains(first)) {
        int n = 1;
        while (digits.hasMatch() && n < 10000 && QFileInfo::exists(numbered(start + n)))
            ++n;
        lengths.insert(first, n);
    }
    QString const file = numbered(start + std::max(0, frame) % lengths.value(first));
    if (!pictures.contains(file)) {
        if (pictures.size() > 64)
            pictures.clear();
        pictures.insert(file, QImage(file).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    return pictures.value(file);
}

void overrideColours(Drawing& drawing, Effect const& e, int frame)
{
    QHash<int, QColor> instead;
    for (QPair<int, QColor> const& o : e.overrides)
        instead.insert(o.first, o.second);
    bool const only = e.value("onlyListed") >= 1.0;
    // Render Selected Colours (not their bitmaps): the pictures of the drawing are left out as well.
    if (e.value("onlyListed") >= 2.0)
        drawing.pictures.clear();
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
        if (only) {
            drawing.strokesOf(sheet).removeIf([&](Stroke const& s) { return !instead.contains(s.swatch) && !e.overrideImages.contains(s.swatch); });
            drawing.fillsOf(sheet).removeIf([&](Fill const& f) { return !instead.contains(f.swatch) && !e.overrideImages.contains(f.swatch); });
        }
        // The areas of a colour given a picture sequence: that picture, cut to their shape, at the bottom of their art layer.
        drawing.fillsOf(sheet).removeIf([&](Fill const& f) {
            auto const found = e.overrideImages.constFind(f.swatch);
            if (found == e.overrideImages.cend() || f.outlines.isEmpty())
                return false;
            QRectF bounds;
            for (QPolygonF const& outline : f.outlines)
                bounds |= outline.boundingRect();
            if (bounds.isEmpty())
                return false;
            Picture picture;
            picture.image = sequenceFrame(*found, frame);
            picture.painted = true;
            picture.sheet = sheet;
            picture.size = QSize(std::max(1, int(std::ceil(bounds.width()))), std::max(1, int(std::ceil(bounds.height()))));
            picture.placement = QTransform::fromTranslate(bounds.left(), bounds.top());
            for (QPolygonF const& outline : f.outlines)
                picture.clip.push_back(outline.translated(-bounds.topLeft()));
            drawing.pictures.push_back(picture);
            return true;
        });
        for (Stroke& s : drawing.strokesOf(sheet))
            if (auto const found = instead.constFind(s.swatch); found != instead.cend()) {
                s.swatch = Scene::kNoSwatch;
                s.color = *found;
            }
        for (Fill& f : drawing.fillsOf(sheet))
            if (auto const found = instead.constFind(f.swatch); found != instead.cend()) {
                f.swatch = Scene::kNoSwatch;
                f.color = *found;
            }
    }
}

namespace {

// `amount` of the way from what was there to what the effect made.
void mixed(QImage& image, QImage const& before, double amount)
{
    // (The picture keeps the depth it had before the effect.)
    QImage was = before;
    Deep::prepare(was);
    if (image.format() != was.format())
        image = image.convertToFormat(was.format());
    double const k = std::clamp(amount, 0.0, 1.0);
    for (int y = 0; y < image.height() && y < was.height(); ++y) {
        Deep::Line const line(image, y);
        Deep::ConstLine const old(was, y);
        for (int x = 0; x < image.width() && x < was.width(); ++x) {
            Deep::Pixel const a = old[x], b = line[x];
            line.set(x, {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k});
        }
    }
}

}  // namespace

bool apply(QImage& image, Effect const& e, Context const& c)
{
    double const amount = c.amount, ppu = c.pixelsPerUnit;
    QPointF const middle = c.frame.center();
    auto place = [&](char const* x, char const* y) { return middle + QPointF(e.value(x), e.value(y)) * ppu; };
    quint32 const frameSeed = quint32(c.at) * 7919u;
    switch (e.type) {
    case Effect::DirectionalBlur:
        directionalBlur(image, amount * ppu, e.direction, e.value("falloff"), int(e.value("trail")));
        if (e.flag("extraBlur"))
            blur(image, std::max(1.0, amount * ppu / 8.0));
        return true;
    case Effect::RadialBlur:
        radialBlur(image, std::min(amount, 1.0), e.value("quality"));
        return true;
    case Effect::Grain:
        grain(image, amount, quint32(e.seed) * 7919u + (e.flag("random") ? quint32(c.at) : 0u), e.value("smooth"));
        return true;
    case Effect::HueSaturation:
        if (e.flag("colorize"))
            colorize(image, e.hue, e.saturation, e.lightness, amount);
        else
            hueSaturation(image, e.hue, e.saturation, e.lightness, amount);
        return true;
    case Effect::Negate:
        negate(image, amount, e.flag("colour"), e.flag("alpha"), e.flag("clamp"));
        return true;
    case Effect::BlackWhite:
        blackWhite(image, amount);
        if (e.flag("matteOutput"))
            greyMatte(image);
        return true;
    case Effect::ColourScale:
        colourScale(image, e.color, amount);
        colourScaleMore(image, e.value("alpha"), e.value("hue"), e.value("saturation"), e.value("value"));
        return true;
    case Effect::FieldChart:
        fieldChart(image, c.frame, e.color, amount * image.devicePixelRatio(), int(e.value("fields")), e.flag("opaque"));
        return true;
    case Effect::LuminanceThreshold:
        if (e.value("gamma") != 1.0)
            gamma(image, e.value("gamma"), 1.0, 1.0, 1.0, 1.0, 1.0);
        luminanceThreshold(image, amount, e.softness, e.clipBlack, e.clipWhite, e.invert);
        if (e.flag("greyMatte"))
            matteComposite(image);
        return true;
    case Effect::GaussianBlur: {
        // Three box passes come very close to a true soft blur.
        double const r = amount * ppu * 0.6;
        boxBlur(image, r * e.value("horizontal"), r * e.value("vertical"), 3, e.flag("repeatEdges"));
        return true;
    }
    case Effect::BoxBlur:
        boxBlur(image, amount * ppu * e.value("horizontal"), amount * ppu * e.value("vertical"), int(e.value("iterations")), e.flag("repeatEdges"));
        return true;
    case Effect::ZoomBlur:
        zoomBlur(image, place("x", "y"), amount, int(e.value("kind")), e.value("quality"));
        return true;
    case Effect::BokehBlur:
        bokehBlur(image, amount * ppu, int(e.value("shape")), e.value("brightness"), e.flag("repeatEdges"));
        return true;
    case Effect::AntiFlicker:
        boxBlur(image, 0.0, amount * ppu * 0.6, 3, true);
        return true;
    case Effect::ColourFade:
        colourFade(image, e.colourValue("target"), amount, int(e.value("space")), int(e.value("hue")));
        return true;
    case Effect::ChannelSwap: {
        int const from[4] = {int(e.value("red")), int(e.value("green")), int(e.value("blue")), int(e.value("alpha"))};
        channelSwap(image, from);
        return true;
    }
    case Effect::ColourLevels:
        levels(image, int(e.value("channel")), e.value("inBlack"), e.value("inWhite"), e.value("gamma"), e.value("outBlack"), e.value("outWhite"), amount);
        return true;
    case Effect::ColourScreen: {
        double const range[6] = {e.value("redMin"), e.value("redMax"), e.value("greenMin"), e.value("greenMax"), e.value("blueMin"), e.value("blueMax")};
        colourScreen(image, range, e.flag("outside"));
        return true;
    }
    case Effect::Contrast:
        contrast(image, e.value("midPoint"), e.value("dark"), e.value("bright"), amount);
        return true;
    case Effect::Gamma:
        gamma(image, e.value("rgb"), e.value("red"), e.value("green"), e.value("blue"), e.value("alpha"), amount);
        return true;
    case Effect::ColourBanding: {
        double const thresholds[4] = {e.value("threshold1"), e.value("threshold2"), e.value("threshold3"), e.value("threshold4")};
        QColor const colours[4] = {e.colourValue("colour1"), e.colourValue("colour2"), e.colourValue("colour3"), e.colourValue("colour4")};
        colourBanding(image, thresholds, colours);
        return true;
    }
    case Effect::Dither:
        dither(image, amount, quint32(e.value("seed")) * 104729u + (e.flag("random") ? frameSeed : 0u));
        return true;
    case Effect::Shine: {
        QColor colour;
        if (!e.flag("useSource"))
            colour = e.colourValue("colour");
        shine(image, place("x", "y"), amount * ppu, e.value("threshold"), e.value("falloff"), colour, e.value("gain"), e.flag("composite"));
        return true;
    }
    case Effect::ShapeCurve: {
        // A curve from Position 1 to Position 2, led by the handles; closed, back to its start and filled.
        QPointF const from = place("x1", "y1"), to = place("x2", "y2");
        QPainterPath curve(from);
        curve.cubicTo(from + QPointF(e.value("lx"), e.value("ly")) * ppu, to + QPointF(e.value("rx"), e.value("ry")) * ppu, to);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        QColor const colour = e.colourValue("colour");
        if (e.flag("close")) {
            curve.closeSubpath();
            p.fillPath(curve, colour);
        }
        QPen pen(colour, std::max(0.1, amount * ppu));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.strokePath(curve, pen);
        return true;
    }
    case Effect::LensFlare:
        lensFlare(image, place("x", "y"), c.frame, int(e.value("kind")), e.colourValue("colour"), amount, e.flag("screen"), e.value("blur"));
        return true;
    case Effect::BurnIn:
        burnIn(image, burnInText(e.text, c.at + int(e.value("offset")), c.fps, sceneName()), c.frame, amount * ppu, e.colourValue("colour"), e.flag("box"),
               int(e.value("place")));
        return true;
    case Effect::TurbulentNoise:
        turbulentNoise(image, c.frame, ppu, e, c.at, amount);
        return true;
    case Effect::Grid:
        grid(image, c.frame, int(e.value("columns")), e.flag("fitVertical"), e.flag("text"), e.colourValue("colour"), amount * image.devicePixelRatio());
        return true;
    case Effect::Refract:
        refract(image, c.second, amount * ppu, e.value("smoothing") * ppu, e.value("offset"));
        return true;
    case Effect::Shake:
    case Effect::Quake:
        transformImage(image, shaken(e, c.at, amount, ppu, middle));
        return true;
    case Effect::UnsharpMask:
        unsharpMask(image, amount * ppu, e.value("strength"), e.value("threshold"));
        return true;
    case Effect::IncreaseOpacity:
        increaseOpacity(image, amount);
        return true;
    case Effect::ColourSpace: {
        // (No name: the scene's working space; without colour management there is nothing to convert from or to.)
        QString const from = e.fromSpace.isEmpty() ? c.workingSpace : e.fromSpace;
        QString const to = e.toSpace.isEmpty() ? c.workingSpace : e.toSpace;
        if (!from.isEmpty() && !to.isEmpty())
            Cms::between(from, to).apply(&image);
        return true;
    }
    case Effect::LookUpTable: {
        if (e.file.isEmpty())
            return true;
        Cms::Transform const lut = Cms::ofLut(e.file);
        if (lut.isIdentity())
            return true;
        QImage const before = image;
        lut.apply(&image);
        if (amount < 1.0)
            mixed(image, before, amount);
        return true;
    }
    case Effect::HoldTiming:
    case Effect::HoldTransformationTiming:
    case Effect::Retime:
    case Effect::RetimeTransformation:
    case Effect::Loop:
    case Effect::LoopTransformation:
        // (Timing: which frame is shown is chosen where the layer is painted; the picture itself is left as it is.)
        return true;
    default:
        return false;
    }
}

}  // namespace Effects
