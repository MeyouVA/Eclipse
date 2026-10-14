#include "ui/panels/Effects.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"

namespace Effects {

// One pass of a box blur along the rows, then along the columns.  What lies outside the
// picture counts as transparent, so a blurred edge fades out.
static void boxBlur(QImage& image, int r)
{
    int const w = image.width(), h = image.height();
    if (r < 1 || w < 1 || h < 1)
        return;
    int const span = 2 * r + 1;
    QVector<QRgb> buffer(std::max(w, h));
    auto run = [&](QRgb* first, int count, qsizetype stride) {
        for (int i = 0; i < count; ++i)
            buffer[i] = *reinterpret_cast<QRgb*>(reinterpret_cast<uchar*>(first) + i * stride);
        unsigned a = 0, red = 0, g = 0, b = 0;
        auto take = [&](int i, int sign) {
            if (i < 0 || i >= count)
                return;
            QRgb const c = buffer[i];
            a += sign * qAlpha(c);
            red += sign * qRed(c);
            g += sign * qGreen(c);
            b += sign * qBlue(c);
        };
        for (int i = -r; i <= r; ++i)
            take(i, +1);
        for (int i = 0; i < count; ++i) {
            *reinterpret_cast<QRgb*>(reinterpret_cast<uchar*>(first) + i * stride) = qRgba(int(red / span), int(g / span), int(b / span), int(a / span));
            take(i - r, -1);
            take(i + r + 1, +1);
        }
    };
    for (int y = 0; y < h; ++y)
        run(reinterpret_cast<QRgb*>(image.scanLine(y)), w, sizeof(QRgb));
    qsizetype const lineBytes = image.bytesPerLine();
    for (int x = 0; x < w; ++x)
        run(reinterpret_cast<QRgb*>(image.scanLine(0)) + x, h, lineBytes);
}

void blur(QImage& image, double radius)
{
    if (radius < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Three box blurs in a row come very close to a true soft blur.
    int const r = std::max(1, int(std::lround(radius * 0.6)));
    for (int pass = 0; pass < 3; ++pass)
        boxBlur(image, std::min(r, 400));
}

void tint(QImage& image, QColor const& color, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const k = int(std::lround(amount * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // The colours are stored already multiplied by how opaque the pixel is.
            int const tr = color.red() * a / 255, tg = color.green() * a / 255, tb = color.blue() * a / 255;
            row[x] = qRgba(qRed(row[x]) + (tr - qRed(row[x])) * k / 256, qGreen(row[x]) + (tg - qGreen(row[x])) * k / 256,
                           qBlue(row[x]) + (tb - qBlue(row[x])) * k / 256, a);
        }
    }
}

void transparency(QImage& image, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const k = int(std::lround((1.0 - amount) * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (row[x] != 0)
                row[x] = qRgba(qRed(row[x]) * k / 256, qGreen(row[x]) * k / 256, qBlue(row[x]) * k / 256, qAlpha(row[x]) * k / 256);
    }
}

void directionalBlur(QImage& image, double length, double degrees)
{
    if (length < 0.5)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // Samples along the line, from one end to the other, averaged (no more than 48 of them).
    int const samples = std::clamp(int(std::ceil(length)), 2, 48);
    double const turn = degrees * 3.14159265358979323846 / 180.0;
    QPointF const along(std::cos(turn), -std::sin(turn));
    QVector<QPoint> steps;
    for (int s = 0; s < samples; ++s) {
        double const t = (double(s) / (samples - 1) - 0.5) * length;
        steps.push_back(QPoint(int(std::lround(along.x() * t)), int(std::lround(along.y() * t))));
    }
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0;
            for (QPoint const& step : std::as_const(steps)) {
                int const sx = x + step.x(), sy = y + step.y();
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    continue;       // beyond the picture: nothing (so the edge fades out)
                QRgb const p = reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
                r += qRed(p);
                g += qGreen(p);
                b += qBlue(p);
                a += qAlpha(p);
            }
            out[x] = qRgba(r / samples, g / samples, b / samples, a / samples);
        }
    }
}

void hueSaturation(QImage& image, double hue, double saturation, double lightness, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (hue == 0.0 && saturation == 0.0 && lightness == 0.0))
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    double const satBy = 1.0 + std::clamp(saturation, -100.0, 100.0) / 100.0, lightBy = std::clamp(lightness, -100.0, 100.0) / 100.0;
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // The colour as it is, not multiplied by how opaque it is; then as hue, saturation and lightness.
            double const r = qRed(row[x]) / double(a), g = qGreen(row[x]) / double(a), b = qBlue(row[x]) / double(a);
            double const most = std::max({r, g, b}), least = std::min({r, g, b});
            double l = (most + least) / 2.0, s = 0.0, hh = 0.0;
            if (most - least > 1e-9) {
                double const d = most - least;
                s = l > 0.5 ? d / (2.0 - most - least) : d / (most + least);
                hh = most == r ? (g - b) / d + (g < b ? 6.0 : 0.0) : most == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
                hh *= 60.0;
            }
            hh = std::fmod(hh + hue + 720.0, 360.0);
            s = std::clamp(s * satBy, 0.0, 1.0);
            l = std::clamp(lightBy >= 0.0 ? l + (1.0 - l) * lightBy : l * (1.0 + lightBy), 0.0, 1.0);
            auto part = [](double p, double q, double t) {
                t = t < 0.0 ? t + 1.0 : t > 1.0 ? t - 1.0 : t;
                return t < 1.0 / 6.0 ? p + (q - p) * 6.0 * t : t < 0.5 ? q : t < 2.0 / 3.0 ? p + (q - p) * (2.0 / 3.0 - t) * 6.0 : p;
            };
            double nr = l, ng = l, nb = l;
            if (s > 1e-9) {
                double const q = l < 0.5 ? l * (1.0 + s) : l + s - l * s, p = 2.0 * l - q, t = hh / 360.0;
                nr = part(p, q, t + 1.0 / 3.0);
                ng = part(p, q, t);
                nb = part(p, q, t - 1.0 / 3.0);
            }
            auto mix = [&](double was, double now) { return std::clamp(int(std::lround((was + (now - was) * amount) * a)), 0, a); };
            row[x] = qRgba(mix(r, nr), mix(g, ng), mix(b, nb), a);
        }
    }
}

void brightnessContrast(QImage& image, double brightness, double contrast, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0 || (brightness == 0.0 && contrast == 0.0))
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    double const c = std::clamp(contrast, -100.0, 100.0) / 100.0;
    double const factor = c >= 0.0 ? 1.0 / std::max(0.01, 1.0 - c) : 1.0 + c;     // all the way: black and white, or flat grey
    double const lift = std::clamp(brightness, -100.0, 100.0) / 100.0;
    int table[256];
    for (int v = 0; v < 256; ++v) {
        double const now = std::clamp((v / 255.0 - 0.5) * factor + 0.5 + lift, 0.0, 1.0);
        table[v] = std::clamp(int(std::lround((v / 255.0 + (now - v / 255.0) * amount) * 255.0)), 0, 255);
    }
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            auto one = [&](int premultiplied) { return table[std::clamp(premultiplied * 255 / a, 0, 255)] * a / 255; };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), a);
        }
    }
}

void grain(QImage& image, double amount, quint32 seed)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const spread = int(std::lround(amount * 160.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(row[x]);
            if (a == 0)
                continue;
            // A number for this point that depends on nothing but where it is and the seed.
            quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ seed * 83492791u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            int const shift = (int(h & 0xff) - 128) * spread / 128 * a / 255;
            auto one = [&](int v) { return std::clamp(v + shift, 0, a); };
            row[x] = qRgba(one(qRed(row[x])), one(qGreen(row[x])), one(qBlue(row[x])), a);
        }
    }
}

void radialBlur(QImage& image, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int const w = image.width(), h = image.height();
    // The middle of what is painted.
    int left = w, top = h, right = -1, bottom = -1;
    for (int y = 0; y < h; ++y) {
        QRgb const* const row = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(row[x]) != 0) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    }
    if (right < 0)
        return;
    double const cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    int const samples = 24;
    QImage const source = image;
    for (int y = 0; y < h; ++y) {
        QRgb* const out = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, al = 0;
            for (int s = 0; s < samples; ++s) {
                // Along the line from the middle through the point, so far either side of it.
                double const k = 1.0 + amount * (0.5 - double(s) / (samples - 1));
                int const sx = int(std::lround(cx + (x - cx) * k)), sy = int(std::lround(cy + (y - cy) * k));
                if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                    continue;
                QRgb const p = reinterpret_cast<QRgb const*>(source.constScanLine(sy))[sx];
                r += qRed(p);
                g += qGreen(p);
                b += qBlue(p);
                al += qAlpha(p);
            }
            out[x] = qRgba(r / samples, g / samples, b / samples, al / samples);
        }
    }
}

void colourScale(QImage& image, QColor const& scale, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    if (amount <= 0.0)
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // (Premultiplied colours scale as they are: the shares apply to the colour, not to how opaque it is.)
    int const kr = int(std::lround((1.0 + (scale.redF() - 1.0) * amount) * 256.0)), kg = int(std::lround((1.0 + (scale.greenF() - 1.0) * amount) * 256.0)),
              kb = int(std::lround((1.0 + (scale.blueF() - 1.0) * amount) * 256.0));
    for (int y = 0; y < image.height(); ++y) {
        QRgb* const row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (row[x] != 0)
                row[x] = qRgba(qRed(row[x]) * kr / 256, qGreen(row[x]) * kg / 256, qBlue(row[x]) * kb / 256, qAlpha(row[x]));
    }
}

void halo(QImage& image, QColor const& color, double radius, QPointF offset)
{
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // The shape of the picture, all in the one colour.
    QImage soft(image.size(), QImage::Format_ARGB32_Premultiplied);
    int const ca = color.alpha();
    for (int y = 0; y < image.height(); ++y) {
        QRgb const* const from = reinterpret_cast<QRgb const*>(image.constScanLine(y));
        QRgb* const to = reinterpret_cast<QRgb*>(soft.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int const a = qAlpha(from[x]) * ca / 255;
            to[x] = qRgba(color.red() * a / 255, color.green() * a / 255, color.blue() * a / 255, a);
        }
    }
    blur(soft, radius);
    QImage out(image.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.drawImage(offset, soft);
        painter.drawImage(0, 0, image);
    }
    out.setDevicePixelRatio(image.devicePixelRatio());
    image = out;
}

QString name(Effect const& effect, Scene const& scene)
{
    switch (effect.type) {
    case Effect::Glow:
        return QObject::tr("Glow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Shadow:
        return QObject::tr("Drop Shadow  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Blur:
        return QObject::tr("Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Tint:
        return QObject::tr("Colour Tint  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Particles:
        return QObject::tr("Particles  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::Transparency:
        return QObject::tr("Transparency  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::DirectionalBlur:
        return QObject::tr("Directional Blur  %1").arg(effect.amount, 0, 'f', 1);
    case Effect::HueSaturation:
        return QObject::tr("Hue-Saturation  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::BrightnessContrast:
        return QObject::tr("Brightness-Contrast  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::Grain:
        return QObject::tr("Grain  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::RadialBlur:
        return QObject::tr("Radial Blur  %1%").arg(int(std::lround(effect.amount * 100.0)));
    case Effect::ColourScale:
        return QObject::tr("Colour-Scale  %1%").arg(int(std::lround(effect.amount * 100.0)));
    default: {
        int const matte = scene.layerIndex(effect.matte);
        return QObject::tr("Cutter  %1%2").arg(matte >= 0 ? scene.layers[matte].name : QObject::tr("(no matte)"),
                                               effect.invert ? QObject::tr("  inverted") : QString());
    }
    }
}

}

// ---- the panel ----------------------------------------------------------------------------

EffectsPanel::EffectsPanel(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("EffectsList"));
    m_list->setMaximumHeight(96);
    QToolButton* const addButton = new QToolButton(this);
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(tr("Add an effect to the layer"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* const menu = new QMenu(addButton);
    connect(menu->addAction(tr("Blur")), &QAction::triggered, this, [this] { add(Effect::Blur); });
    connect(menu->addAction(tr("Colour Tint")), &QAction::triggered, this, [this] { add(Effect::Tint); });
    connect(menu->addAction(tr("Cutter")), &QAction::triggered, this, [this] { add(Effect::Cutter); });
    connect(menu->addAction(tr("Glow")), &QAction::triggered, this, [this] { add(Effect::Glow); });
    connect(menu->addAction(tr("Drop Shadow")), &QAction::triggered, this, [this] { add(Effect::Shadow); });
    connect(menu->addAction(tr("Particles")), &QAction::triggered, this, [this] { add(Effect::Particles); });
    connect(menu->addAction(tr("Transparency")), &QAction::triggered, this, [this] { add(Effect::Transparency); });
    connect(menu->addAction(tr("Directional Blur")), &QAction::triggered, this, [this] { add(Effect::DirectionalBlur); });
    connect(menu->addAction(tr("Hue-Saturation")), &QAction::triggered, this, [this] { add(Effect::HueSaturation); });
    connect(menu->addAction(tr("Brightness-Contrast")), &QAction::triggered, this, [this] { add(Effect::BrightnessContrast); });
    connect(menu->addAction(tr("Grain")), &QAction::triggered, this, [this] { add(Effect::Grain); });
    connect(menu->addAction(tr("Radial Blur")), &QAction::triggered, this, [this] { add(Effect::RadialBlur); });
    connect(menu->addAction(tr("Colour-Scale")), &QAction::triggered, this, [this] { add(Effect::ColourScale); });
    addButton->setMenu(menu);
    QToolButton* const removeButton = new QToolButton(this);
    removeButton->setText(QStringLiteral("-"));
    removeButton->setToolTip(tr("Remove the selected effect"));
    connect(removeButton, &QToolButton::clicked, this, &EffectsPanel::removeCurrent);

    m_amountLabel = new QLabel(this);
    m_amount = new QDoubleSpinBox(this);
    m_amount->setKeyboardTracking(false);
    m_color = new QPushButton(this);
    m_color->setFixedWidth(60);
    m_matteLabel = new QLabel(tr("Matte"), this);
    m_matte = new QComboBox(this);
    m_matte->setToolTip(tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert = new QCheckBox(tr("Inverted"), this);
    m_invert->setToolTip(tr("Keep what lies outside the matte instead."));
    m_animated = new QCheckBox(tr("Animate"), this);
    m_animated->setToolTip(tr("When ticked, changing the amount sets a keyframe on the current frame, and the amount glides between keyframes."));
    m_offsetLabel = new QLabel(tr("Offset"), this);
    m_offsetX = new QDoubleSpinBox(this);
    m_offsetY = new QDoubleSpinBox(this);
    for (QDoubleSpinBox* box : {m_offsetX, m_offsetY}) {
        box->setRange(-2000.0, 2000.0);
        box->setDecimals(1);
        box->setKeyboardTracking(false);
    }

    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel(tr("Effects"), this));
    buttons->addStretch();
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    QHBoxLayout* const amountRow = new QHBoxLayout;
    amountRow->addWidget(m_amountLabel);
    amountRow->addWidget(m_amount, 1);
    amountRow->addWidget(m_color);
    amountRow->addWidget(m_animated);
    QHBoxLayout* const offsetRow = new QHBoxLayout;
    offsetRow->addWidget(m_offsetLabel);
    offsetRow->addWidget(m_offsetX, 1);
    offsetRow->addWidget(m_offsetY, 1);
    QHBoxLayout* const matteRow = new QHBoxLayout;
    matteRow->addWidget(m_matteLabel);
    matteRow->addWidget(m_matte, 1);
    matteRow->addWidget(m_invert);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_list);
    layout->addLayout(amountRow);
    layout->addLayout(offsetRow);
    layout->addLayout(matteRow);
    {
        // Hue-Saturation, Brightness-Contrast, Directional Blur: what they change, and by how much.
        m_adjust = new QWidget(this);
        m_adjust->setObjectName(QStringLiteral("AdjustSettings"));
        m_adjustForm = new QFormLayout(m_adjust);
        m_adjustForm->setContentsMargins(0, 0, 0, 0);
        struct { char const* name; char const* label; char const* tip; double low, high; } const kBoxes[6] = {
            {"AdjustHue", QT_TR_NOOP("Hue"), QT_TR_NOOP("How far round the colour wheel each colour is turned (degrees)."), -180, 180},
            {"AdjustSaturation", QT_TR_NOOP("Saturation"), QT_TR_NOOP("Colours made richer (more) or greyer (less), in percent."), -100, 100},
            {"AdjustLightness", QT_TR_NOOP("Lightness"), QT_TR_NOOP("Colours made lighter (more) or darker (less), in percent."), -100, 100},
            {"AdjustBrightness", QT_TR_NOOP("Brightness"), QT_TR_NOOP("The whole picture made brighter (more) or darker (less), in percent."), -100, 100},
            {"AdjustContrast", QT_TR_NOOP("Contrast"), QT_TR_NOOP("Lights lighter and darks darker (more), or all nearer grey (less), in percent."), -100, 100},
            {"BlurDirection", QT_TR_NOOP("Direction"), QT_TR_NOOP("Which way the picture is smeared (degrees: 0 across, 90 up and down)."), -360, 360}};
        for (int i = 0; i < 6; ++i) {
            m_adjustBox[i] = new QDoubleSpinBox(m_adjust);
            m_adjustBox[i]->setObjectName(QLatin1String(kBoxes[i].name));
            m_adjustBox[i]->setRange(kBoxes[i].low, kBoxes[i].high);
            m_adjustBox[i]->setDecimals(1);
            m_adjustBox[i]->setSingleStep(i == 5 ? 15.0 : 5.0);
            m_adjustBox[i]->setKeyboardTracking(false);
            m_adjustBox[i]->setToolTip(tr(kBoxes[i].tip));
            m_adjustForm->addRow(tr(kBoxes[i].label), m_adjustBox[i]);
            connect(m_adjustBox[i], &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                if (m_rebuilding)
                    return;
                edit([i, v](Effect& e) {
                    double* const fields[6] = {&e.hue, &e.saturation, &e.lightness, &e.brightness, &e.contrast, &e.direction};
                    *fields[i] = v;
                });
            });
        }
        layout->addWidget(m_adjust);
    }
    {
        // Particles: how long each lives, how fast and which way it leaves, how it falls.
        m_particles = new QWidget(this);
        m_particles->setObjectName(QStringLiteral("ParticleSettings"));
        QFormLayout* const form = new QFormLayout(m_particles);
        form->setContentsMargins(0, 0, 0, 0);
        struct { char const* name; char const* label; double low, high, step; int decimals; } const kBoxes[6] = {
            {"ParticleLife", QT_TR_NOOP("Life (frames)"), 1, 600, 1, 0}, {"ParticleSpeed", QT_TR_NOOP("Speed"), 0, 500, 1, 1},
            {"ParticleSpread", QT_TR_NOOP("Spread"), 0, 360, 5, 0}, {"ParticleDirection", QT_TR_NOOP("Direction"), -360, 360, 5, 0},
            {"ParticleGravity", QT_TR_NOOP("Gravity"), -50, 50, 0.1, 2}, {"ParticleSeed", QT_TR_NOOP("Scattering"), 1, 9999, 1, 0}};
        for (int i = 0; i < 6; ++i) {
            m_particleBox[i] = new QDoubleSpinBox(m_particles);
            m_particleBox[i]->setObjectName(QLatin1String(kBoxes[i].name));
            m_particleBox[i]->setRange(kBoxes[i].low, kBoxes[i].high);
            m_particleBox[i]->setSingleStep(kBoxes[i].step);
            m_particleBox[i]->setDecimals(kBoxes[i].decimals);
            m_particleBox[i]->setKeyboardTracking(false);
            form->addRow(tr(kBoxes[i].label), m_particleBox[i]);
            connect(m_particleBox[i], &QDoubleSpinBox::valueChanged, this, [this] {
                if (m_rebuilding)
                    return;
                double v[6];
                for (int k = 0; k < 6; ++k)
                    v[k] = m_particleBox[k]->value();
                edit([v](Effect& e) {
                    e.life = int(v[0]);
                    e.speed = v[1];
                    e.spread = v[2];
                    e.direction = v[3];
                    e.gravity = v[4];
                    e.seed = int(v[5]);
                });
            });
        }
        // How big at the end of its life; and whether they move with the layer.
        m_particleBox[6] = new QDoubleSpinBox(m_particles);
        m_particleBox[6]->setObjectName(QStringLiteral("ParticleEndSize"));
        m_particleBox[6]->setRange(0.0, 20.0);
        m_particleBox[6]->setSingleStep(0.1);
        m_particleBox[6]->setDecimals(2);
        m_particleBox[6]->setKeyboardTracking(false);
        m_particleBox[6]->setToolTip(tr("How big a particle is when it dies, against how big it was when it left (1: the same)."));
        form->addRow(tr("Size at the End"), m_particleBox[6]);
        connect(m_particleBox[6], &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (!m_rebuilding)
                edit([v](Effect& e) { e.endSize = v; });
        });
        // Wind across, and turning.
        struct Extra { char const* name; char const* label; char const* tip; double low, high, step; };
        static Extra const kExtras[7] = {
            {"ParticleWind", QT_TR_NOOP("Wind"), QT_TR_NOOP("Pushes the particles across by so many units a frame, each frame (less than 0: to the left)."), -20.0, 20.0, 0.1},
            {"ParticleSpin", QT_TR_NOOP("Spin"), QT_TR_NOOP("Each particle turns by up to so many degrees a frame, one way or the other."), 0.0, 360.0, 1.0},
            {"ParticleRegionWidth", QT_TR_NOOP("Region Width"), QT_TR_NOOP("They are let go from anywhere in a box this wide about the drawing (0: from the drawing itself)."), 0.0, 5000.0, 10.0},
            {"ParticleRegionHeight", QT_TR_NOOP("Region Height"), QT_TR_NOOP("They are let go from anywhere in a box this high about the drawing (0: from the drawing itself)."), 0.0, 5000.0, 10.0},
            {"ParticleFloor", QT_TR_NOOP("Floor"), QT_TR_NOOP("A floor so far below where they are let go, that they bounce off (0: none)."), 0.0, 5000.0, 10.0},
            {"ParticleBounce", QT_TR_NOOP("Bounce"), QT_TR_NOOP("How much of its speed a particle keeps when it bounces off the floor or a wall (0: none, 1: all)."), 0.0, 1.0, 0.05},
            {"ParticleWalls", QT_TR_NOOP("Walls"), QT_TR_NOOP("Walls so far to the left and to the right of where they are let go, that they bounce off (0: none)."), 0.0, 5000.0, 10.0},
        };
        for (int i = 0; i < 7; ++i) {
            QDoubleSpinBox* const box = new QDoubleSpinBox(m_particles);
            m_particleBox[7 + i] = box;
            box->setObjectName(QLatin1String(kExtras[i].name));
            box->setRange(kExtras[i].low, kExtras[i].high);
            box->setSingleStep(kExtras[i].step);
            box->setDecimals(2);
            box->setKeyboardTracking(false);
            box->setToolTip(tr(kExtras[i].tip));
            form->addRow(tr(kExtras[i].label), box);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
                if (!m_rebuilding)
                    edit([i, v](Effect& e) {
                        double* const fields[7] = {&e.wind, &e.spin, &e.regionWidth, &e.regionHeight, &e.floor, &e.bounce, &e.walls};
                        *fields[i] = v;
                    });
            });
        }
        // The shape of the region they are let go from.
        m_particleShape = new QComboBox(m_particles);
        m_particleShape->setObjectName(QStringLiteral("ParticleRegionShape"));
        m_particleShape->addItems({tr("Box"), tr("Round"), tr("Ring (its edge)"), tr("The drawing's shape")});
        form->addRow(tr("Region Shape"), m_particleShape);
        connect(m_particleShape, &QComboBox::currentIndexChanged, this, [this](int shape) {
            if (!m_rebuilding && shape >= 0)
                edit([shape](Effect& e) { e.regionShape = shape; });
        });
        // The colour they turn to as they age.
        m_particleRecolour = new QCheckBox(tr("Turn to"), m_particles);
        m_particleRecolour->setObjectName(QStringLiteral("ParticleRecolour"));
        m_particleRecolour->setToolTip(tr("On: each particle turns to the colour beside it by the end of its life."));
        m_particleColour = new QPushButton(m_particles);
        m_particleColour->setObjectName(QStringLiteral("ParticleEndColour"));
        m_particleColour->setFixedWidth(60);
        form->addRow(m_particleRecolour, m_particleColour);
        connect(m_particleRecolour, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_rebuilding)
                edit([on](Effect& e) { e.endColor = on ? (e.endColor.isValid() ? e.endColor : QColor(255, 140, 40)) : QColor(); });
        });
        connect(m_particleColour, &QPushButton::clicked, this, [this] {
            Scene const& scene = m_document->scene();
            int const row = m_list->currentRow();
            if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
                return;
            QColor const was = scene.layers[m_layer].effects[row].endColor;
            QColor const chosen = QColorDialog::getColor(was.isValid() ? was : QColor(255, 140, 40), this, tr("Particle Colour at the End"));
            if (chosen.isValid())
                edit([chosen](Effect& e) { e.endColor = chosen; });
        });
        m_particleFollow = new QCheckBox(tr("Move with the layer"), m_particles);
        m_particleFollow->setObjectName(QStringLiteral("ParticleFollow"));
        m_particleFollow->setToolTip(tr("On: the particles go where the layer goes. Off: each stays where the layer was when it was let go."));
        form->addRow(m_particleFollow);
        connect(m_particleFollow, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_rebuilding)
                edit([on](Effect& e) { e.followLayer = on; });
        });
        layout->addWidget(m_particles);
    }

    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (!m_rebuilding)
            showEditors();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_rebuilding)
            return;
        int const row = m_list->row(item);
        bool const on = item->checkState() == Qt::Checked;
        m_list->setCurrentRow(row);
        edit([on](Effect& e) { e.enabled = on; });
    });
    connect(m_amount, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_rebuilding)
            return;
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        Effect const& effect = scene.layers[m_layer].effects[row];
        bool const percent = effect.type == Effect::Tint || effect.type == Effect::Transparency || effect.type == Effect::HueSaturation
                             || effect.type == Effect::BrightnessContrast || effect.type == Effect::Grain || effect.type == Effect::RadialBlur
                             || effect.type == Effect::ColourScale;
        double const amount = percent ? v / 100.0 : v;
        if (effect.animated) {
            // On this frame only: a keyframe.
            m_document->setEffectAmount(m_layer, m_frame, row, amount);
            m_list->setCurrentRow(row);
        } else {
            edit([amount](Effect& e) { e.amount = amount; });
        }
    });
    connect(m_animated, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_rebuilding)
            edit([on](Effect& e) { e.animated = on; });
    });
    for (QDoubleSpinBox* box : {m_offsetX, m_offsetY}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            QPointF const offset(m_offsetX->value(), m_offsetY->value());
            if (!m_rebuilding)
                edit([offset](Effect& e) { e.offset = offset; });
        });
    }
    connect(m_color, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        QColor const chosen = QColorDialog::getColor(scene.layers[m_layer].effects[row].color, this, tr("Effect Colour"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.color = chosen; });
    });
    connect(m_matte, &QComboBox::activated, this, [this](int index) {
        int const id = m_matte->itemData(index).toInt();
        if (!m_rebuilding)
            edit([id](Effect& e) { e.matte = id; });
    });
    connect(m_invert, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_rebuilding)
            edit([on](Effect& e) { e.invert = on; });
    });
    connect(m_document, &Document::changed, this, &EffectsPanel::rebuild);
    rebuild();
}

void EffectsPanel::setCurrentCell(int layer, int frame)
{
    bool const layerChanged = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (layerChanged)
        rebuild();
    else
        showEditors();  // an animated amount differs from frame to frame
}

template <class Change> void EffectsPanel::edit(Change change)
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
        return;
    QVector<Effect> effects = scene.layers[m_layer].effects;
    change(effects[row]);
    m_document->setEffects(m_layer, effects, tr("Change Effect"));
    m_list->setCurrentRow(row);
}

void EffectsPanel::add(Effect::Type type)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Effect const e = Effect::made(type);
    QVector<Effect> effects = scene.layers[m_layer].effects;
    effects.push_back(e);
    m_document->setEffects(m_layer, effects, tr("Add Effect"));
    m_list->setCurrentRow(int(effects.size()) - 1);
}

void EffectsPanel::removeCurrent()
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
        return;
    m_document->removeEffect(m_layer, row);
}

void EffectsPanel::rebuild()
{
    Scene const& scene = m_document->scene();
    int const keep = m_list->currentRow();
    m_rebuilding = true;
    m_list->clear();
    bool const drawing = m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].holdsDrawings();
    setEnabled(drawing);
    if (drawing) {
        for (Effect const& e : scene.layers[m_layer].effects) {
            QListWidgetItem* const item = new QListWidgetItem(Effects::name(e, scene), m_list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
        }
        m_list->setCurrentRow(std::min(keep, m_list->count() - 1));
    }
    m_rebuilding = false;
    showEditors();
}

void EffectsPanel::showEditors()
{
    Scene const& scene = m_document->scene();
    int const row = m_list->currentRow();
    bool const valid = m_layer >= 0 && m_layer < scene.layers.size() && row >= 0 && row < scene.layers[m_layer].effects.size();
    Effect const e = valid ? scene.layers[m_layer].effects[row] : Effect();
    bool const cutter = valid && e.type == Effect::Cutter;
    bool const coloured = valid && (e.type == Effect::Tint || e.type == Effect::Glow || e.type == Effect::Shadow || e.type == Effect::ColourScale);
    // What the amount is on this frame: the keyframes say, for an animated effect.
    double const amount = valid ? scene.layers[m_layer].effectAmount(row, scene.layers[m_layer].poseAt(m_frame)) : 0.0;
    m_rebuilding = true;
    m_amountLabel->setVisible(valid && !cutter);
    m_amount->setVisible(valid && !cutter);
    m_animated->setVisible(valid && !cutter);
    m_animated->setChecked(valid && e.animated);
    m_color->setVisible(coloured);
    if (coloured)
        m_color->setStyleSheet(QStringLiteral("background-color: %1;").arg(e.color.name()));
    bool const shadow = valid && e.type == Effect::Shadow;
    m_offsetLabel->setVisible(shadow);
    m_offsetX->setVisible(shadow);
    m_offsetY->setVisible(shadow);
    if (shadow) {
        m_offsetX->setValue(e.offset.x());
        m_offsetY->setValue(e.offset.y());
    }
    bool const particles = valid && e.type == Effect::Particles;
    {
        bool const hs = valid && e.type == Effect::HueSaturation, bc = valid && e.type == Effect::BrightnessContrast, along = valid && e.type == Effect::DirectionalBlur;
        m_adjust->setVisible(hs || bc || along);
        bool const shownFor[6] = {hs, hs, hs, bc, bc, along};
        double const values[6] = {e.hue, e.saturation, e.lightness, e.brightness, e.contrast, e.direction};
        for (int i = 0; i < 6; ++i) {
            m_adjustForm->setRowVisible(m_adjustBox[i], shownFor[i]);
            m_adjustBox[i]->setValue(values[i]);
        }
    }
    m_matteLabel->setVisible(cutter || particles);
    m_matteLabel->setText(particles ? tr("Bounce Off") : tr("Matte"));
    m_matte->setVisible(cutter || particles);
    m_matte->setToolTip(particles ? tr("A layer whose drawing the particles bounce off (keeping as much of their speed as Bounce says).")
                                  : tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert->setVisible(cutter);
    if (cutter || particles) {
        m_matte->clear();
        m_matte->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            if (l != m_layer && scene.layers[l].holdsDrawings())
                m_matte->addItem(scene.layers[l].name, scene.layers[l].id);
        m_matte->setCurrentIndex(std::max(0, m_matte->findData(e.matte)));
    }
    m_particles->setVisible(particles);
    if (particles) {
        double const values[6] = {double(e.life), e.speed, e.spread, e.direction, e.gravity, double(e.seed)};
        for (int i = 0; i < 6; ++i)
            m_particleBox[i]->setValue(values[i]);
        m_particleBox[6]->setValue(e.endSize);
        m_particleFollow->setChecked(e.followLayer);
        m_particleBox[7]->setValue(e.wind);
        m_particleBox[8]->setValue(e.spin);
        m_particleBox[9]->setValue(e.regionWidth);
        m_particleBox[10]->setValue(e.regionHeight);
        m_particleBox[11]->setValue(e.floor);
        m_particleBox[12]->setValue(e.bounce);
        m_particleBox[13]->setValue(e.walls);
        m_particleShape->setCurrentIndex(std::clamp(e.regionShape, 0, 3));
        m_particleRecolour->setChecked(e.endColor.isValid());
        m_particleColour->setEnabled(e.endColor.isValid());
        m_particleColour->setStyleSheet(e.endColor.isValid() ? QStringLiteral("background-color: %1;").arg(e.endColor.name()) : QString());
        m_amountLabel->setText(tr("Each Frame"));
        m_amount->setRange(0.0, 50.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (valid && (e.type == Effect::Tint || e.type == Effect::Transparency || e.type == Effect::HueSaturation || e.type == Effect::BrightnessContrast
                         || e.type == Effect::Grain || e.type == Effect::RadialBlur || e.type == Effect::ColourScale)) {
        m_amountLabel->setText(e.type == Effect::Transparency ? tr("Transparency") : tr("Amount"));
        m_amount->setRange(0.0, 100.0);
        m_amount->setDecimals(0);
        m_amount->setSuffix(QStringLiteral(" %"));
        m_amount->setValue(amount * 100.0);
    } else if (valid && !cutter) {
        m_amountLabel->setText(tr("Radius"));
        m_amount->setRange(0.0, 500.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (cutter) {
        m_invert->setChecked(e.invert);
    }
    m_rebuilding = false;
}
