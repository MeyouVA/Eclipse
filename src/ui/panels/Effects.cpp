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
        double const amount = effect.type == Effect::Tint ? v / 100.0 : v;
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
    Effect e;
    e.type = type;
    e.amount = type == Effect::Tint ? 0.5 : type == Effect::Shadow ? 6.0 : type == Effect::Particles ? 2.0 : 8.0;
    e.color = type == Effect::Shadow ? QColor(0, 0, 0, 150) : type == Effect::Glow ? QColor(255, 240, 160) : QColor(40, 110, 200);
    e.offset = type == Effect::Shadow ? QPointF(10, 10) : QPointF();
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
    bool const coloured = valid && (e.type == Effect::Tint || e.type == Effect::Glow || e.type == Effect::Shadow);
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
    m_matteLabel->setVisible(cutter);
    m_matte->setVisible(cutter);
    m_invert->setVisible(cutter);
    bool const particles = valid && e.type == Effect::Particles;
    m_particles->setVisible(particles);
    if (particles) {
        double const values[6] = {double(e.life), e.speed, e.spread, e.direction, e.gravity, double(e.seed)};
        for (int i = 0; i < 6; ++i)
            m_particleBox[i]->setValue(values[i]);
        m_amountLabel->setText(tr("Each Frame"));
        m_amount->setRange(0.0, 50.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (valid && e.type == Effect::Tint) {
        m_amountLabel->setText(tr("Amount"));
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
        m_matte->clear();
        m_matte->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            if (l != m_layer && scene.layers[l].holdsDrawings())
                m_matte->addItem(scene.layers[l].name, scene.layers[l].id);
        m_matte->setCurrentIndex(std::max(0, m_matte->findData(e.matte)));
        m_invert->setChecked(e.invert);
    }
    m_rebuilding = false;
}
