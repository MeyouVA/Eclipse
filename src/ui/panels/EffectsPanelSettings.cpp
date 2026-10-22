// Eclipse - the effects panel: the settings of the kinds of effect that have more, and the editors wired up.
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/EffectsPanelEdit.h"

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QDir>
#include <QJSEngine>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QTemporaryDir>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include "model/document/Commands.h"

#include <algorithm>
#include <cmath>

void EffectsPanel::addAdjustSettings(QVBoxLayout* layout)
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

void EffectsPanel::addMoreSettings(QVBoxLayout* layout)
{
    // Channel Selector: which channel; Crop: the area kept.
    m_more = new QWidget(this);
    m_more->setObjectName(QStringLiteral("MoreEffectSettings"));
    m_moreForm = new QFormLayout(m_more);
    m_moreForm->setContentsMargins(0, 0, 0, 0);
    m_channel = new QComboBox(m_more);
    m_channel->setObjectName(QStringLiteral("EffectChannel"));
    m_channel->addItems({tr("Red"), tr("Green"), tr("Blue"), tr("Opacity")});
    m_moreForm->addRow(tr("Channel"), m_channel);
    connect(m_channel, &QComboBox::currentIndexChanged, this, [this](int channel) {
        if (!m_rebuilding)
            edit([channel](Effect& e) { e.channel = channel; });
    });
    static char const* const cropNames[4] = {QT_TR_NOOP("Left"), QT_TR_NOOP("Top"), QT_TR_NOOP("Width"), QT_TR_NOOP("Height")};
    for (int i = 0; i < 4; ++i) {
        m_cropBox[i] = new QDoubleSpinBox(m_more);
        m_cropBox[i]->setObjectName(QStringLiteral("EffectCrop%1").arg(i));
        m_cropBox[i]->setRange(i < 2 ? -10000.0 : 0.0, 10000.0);
        m_cropBox[i]->setDecimals(1);
        m_cropBox[i]->setKeyboardTracking(false);
        m_moreForm->addRow(tr(cropNames[i]), m_cropBox[i]);
        connect(m_cropBox[i], &QDoubleSpinBox::valueChanged, this, [this] {
            if (m_rebuilding)
                return;
            QRectF const area(m_cropBox[0]->value(), m_cropBox[1]->value(), m_cropBox[2]->value(), m_cropBox[3]->value());
            edit([area](Effect& e) { e.area = area; });
        });
    }
    // Colour Curves: the curve picked, to drag.
    m_curveWhich = new QComboBox(m_more);
    m_curveWhich->setObjectName(QStringLiteral("EffectCurveWhich"));
    m_curveWhich->addItems({tr("RGB"), tr("Red"), tr("Green"), tr("Blue")});
    m_moreForm->addRow(tr("Curve"), m_curveWhich);
    m_curve = new ColourCurveEdit(m_more);
    m_moreForm->addRow(m_curve);
    connect(m_curveWhich, &QComboBox::currentIndexChanged, this, [this] { showEditors(); });
    // Script and External: the script or the command line, and Apply to use it.
    m_text = new QPlainTextEdit(m_more);
    m_text->setObjectName(QStringLiteral("EffectText"));
    m_text->setMinimumHeight(90);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_moreForm->addRow(m_text);
    m_textApply = new QPushButton(tr("Apply"), m_more);
    m_textApply->setObjectName(QStringLiteral("EffectTextApply"));
    m_moreForm->addRow(m_textApply);
    m_textSays = new QLabel(m_more);
    m_textSays->setObjectName(QStringLiteral("EffectTextSays"));
    m_textSays->setWordWrap(true);
    m_moreForm->addRow(m_textSays);
    connect(m_textApply, &QPushButton::clicked, this, [this] {
        QString const text = m_text->toPlainText();
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        bool const script = m_layer >= 0 && m_layer < scene.layers.size() && row >= 0 && row < scene.layers[m_layer].effects.size()
                            && scene.layers[m_layer].effects[row].type == Effect::Script;
        // A script tried out first on a small picture: what is wrong with it, said here.
        QString says;
        if (script) {
            QImage trial(8, 8, QImage::Format_ARGB32_Premultiplied);
            trial.fill(Qt::white);
            says = Effects::runScript(trial, text, m_frame, 1.0, 1.0);
        }
        edit([text](Effect& e) { e.text = text; });
        m_textSays->setText(says);
    });
    connect(m_curve, &ColourCurveEdit::edited, this, [this](QVector<QPointF> const& points) {
        int const which = std::clamp(m_curveWhich->currentIndex(), 0, 3);
        // (A straight line from corner to corner is no curve at all.)
        bool const straight = points.size() == 2 && points[0] == QPointF(0, 0) && points[1] == QPointF(1, 1);
        edit([which, points, straight](Effect& e) { e.curves[which] = straight ? QVector<QPointF>() : points; });
    });
    layout->addWidget(m_more);
}

void EffectsPanel::addParticleSettings(QVBoxLayout* layout)
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
        QColor const chosen = ColourEditor::getColour(was.isValid() ? was : QColor(255, 140, 40), this, tr("Particle Colour at the End"));
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

void EffectsPanel::connectEditors()
{
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
                             || effect.type == Effect::ColourScale || effect.type == Effect::Negate || effect.type == Effect::BlackWhite
                             || effect.type == Effect::Gradient;
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
        QColor const chosen = ColourEditor::getColour(scene.layers[m_layer].effects[row].color, this, tr("Effect Colour"));
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.color = chosen; });
    });
    connect(m_endColor, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        QColor const chosen = ColourEditor::getColour(scene.layers[m_layer].effects[row].endColor, this, tr("Gradient: To"));
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.endColor = chosen; });
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
}
