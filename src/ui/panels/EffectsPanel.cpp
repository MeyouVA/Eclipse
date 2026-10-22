// Eclipse - the effects of the current layer: the list, and the editors of the one picked.
#include "ui/panels/EffectsPanel.h"

#include <QCheckBox>
#include <QColorDialog>
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
    connect(menu->addAction(tr("Negate")), &QAction::triggered, this, [this] { add(Effect::Negate); });
    connect(menu->addAction(tr("Median")), &QAction::triggered, this, [this] { add(Effect::Median); });
    connect(menu->addAction(tr("Ripple")), &QAction::triggered, this, [this] { add(Effect::Ripple); });
    connect(menu->addAction(tr("Tone")), &QAction::triggered, this, [this] { add(Effect::Tone); });
    connect(menu->addAction(tr("Highlight")), &QAction::triggered, this, [this] { add(Effect::Highlight); });
    connect(menu->addAction(tr("Bloom")), &QAction::triggered, this, [this] { add(Effect::Bloom); });
    connect(menu->addAction(tr("Pixelate")), &QAction::triggered, this, [this] { add(Effect::Pixelate); });
    connect(menu->addAction(tr("Turbulence")), &QAction::triggered, this, [this] { add(Effect::Turbulence); });
    connect(menu->addAction(tr("Matte Generator")), &QAction::triggered, this, [this] { add(Effect::MatteGenerator); });
    connect(menu->addAction(tr("Black & White")), &QAction::triggered, this, [this] { add(Effect::BlackWhite); });
    connect(menu->addAction(tr("Matte Resize")), &QAction::triggered, this, [this] { add(Effect::MatteResize); });
    connect(menu->addAction(tr("Matte Blur")), &QAction::triggered, this, [this] { add(Effect::MatteBlur); });
    connect(menu->addAction(tr("Gradient")), &QAction::triggered, this, [this] { add(Effect::Gradient); });
    connect(menu->addAction(tr("Channel Selector")), &QAction::triggered, this, [this] { add(Effect::ChannelSelector); });
    connect(menu->addAction(tr("Crop")), &QAction::triggered, this, [this] { add(Effect::Crop); });
    connect(menu->addAction(tr("Remove Transparency")), &QAction::triggered, this, [this] { add(Effect::RemoveTransparency); });
    connect(menu->addAction(tr("Matte Composite")), &QAction::triggered, this, [this] { add(Effect::MatteComposite); });
    connect(menu->addAction(tr("Variable Blur")), &QAction::triggered, this, [this] { add(Effect::VariableBlur); });
    connect(menu->addAction(tr("Motion Blur")), &QAction::triggered, this, [this] { add(Effect::MotionBlur); });
    connect(menu->addAction(tr("Field Chart")), &QAction::triggered, this, [this] { add(Effect::FieldChart); });
    connect(menu->addAction(tr("Colour Curves")), &QAction::triggered, this, [this] { add(Effect::ColourCurves); });
    connect(menu->addAction(tr("Focus")), &QAction::triggered, this, [this] { add(Effect::Focus); });
    connect(menu->addAction(tr("Apply Peg Transformation")), &QAction::triggered, this, [this] { add(Effect::ApplyPeg); });
    connect(menu->addAction(tr("Scale Output")), &QAction::triggered, this, [this] { add(Effect::ScaleOutput); });
    connect(menu->addAction(tr("Script")), &QAction::triggered, this, [this] { add(Effect::Script); });
    connect(menu->addAction(tr("External")), &QAction::triggered, this, [this] { add(Effect::External); });
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
    m_endColor = new QPushButton(this);
    m_endColor->setObjectName(QStringLiteral("EffectEndColour"));
    m_endColor->setFixedWidth(60);
    m_endColor->setToolTip(tr("The colour the gradient goes to."));
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
    // The effect picked (none picked: all of them) copied, and pasted onto another layer after its own.
    QToolButton* const copyButton = new QToolButton(this);
    copyButton->setObjectName(QStringLiteral("EffectsCopy"));
    copyButton->setText(tr("Copy"));
    copyButton->setToolTip(tr("Copy the selected effect (or all of them, with none selected)"));
    QToolButton* const pasteButton = new QToolButton(this);
    pasteButton->setObjectName(QStringLiteral("EffectsPaste"));
    pasteButton->setText(tr("Paste"));
    pasteButton->setToolTip(tr("Paste the effects copied onto this layer"));
    buttons->addWidget(copyButton);
    buttons->addWidget(pasteButton);
    connect(copyButton, &QToolButton::clicked, this, [this] { copyEffects(); });
    connect(pasteButton, &QToolButton::clicked, this, [this] { pasteEffects(); });
    QHBoxLayout* const amountRow = new QHBoxLayout;
    amountRow->addWidget(m_amountLabel);
    amountRow->addWidget(m_amount, 1);
    amountRow->addWidget(m_color);
    amountRow->addWidget(m_endColor);
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
    addAdjustSettings(layout);
    addMoreSettings(layout);
    addParticleSettings(layout);

    connectEditors();
    connect(m_document, &Document::changed, this, &EffectsPanel::rebuild);
    rebuild();
}

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
        QColor const chosen = QColorDialog::getColor(scene.layers[m_layer].effects[row].color, this, tr("Effect Colour"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            edit([chosen](Effect& e) { e.color = chosen; });
    });
    connect(m_endColor, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_list->currentRow();
        if (m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].effects.size())
            return;
        QColor const chosen = QColorDialog::getColor(scene.layers[m_layer].effects[row].endColor, this, tr("Gradient: To"));
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

QVector<Effect> EffectsPanel::s_copied;

void EffectsPanel::copyEffects()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    QVector<Effect> const& all = scene.layers[m_layer].effects;
    int const row = m_list->currentRow();
    s_copied = row >= 0 && row < all.size() ? QVector<Effect>{all[row]} : all;
}

void EffectsPanel::pasteEffects()
{
    Scene const& scene = m_document->scene();
    if (s_copied.isEmpty() || m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings())
        return;
    QVector<Effect> effects = scene.layers[m_layer].effects;
    effects += s_copied;
    m_document->setEffects(m_layer, effects, tr("Paste Effects"));
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
    bool const shading = valid && (e.type == Effect::Tone || e.type == Effect::Highlight || e.type == Effect::MatteBlur || e.type == Effect::VariableBlur);
    bool const resizing = valid && e.type == Effect::MatteResize;
    bool const coloured = valid && (e.type == Effect::Tint || e.type == Effect::Glow || e.type == Effect::Shadow || e.type == Effect::ColourScale || shading
                                    || e.type == Effect::FieldChart
                                    || e.type == Effect::MatteGenerator || e.type == Effect::Gradient) && e.type != Effect::MatteBlur;
    bool const graded = valid && e.type == Effect::Gradient;
    // What the amount is on this frame: the keyframes say, for an animated effect.
    double const amount = valid ? scene.layers[m_layer].effectAmount(row, scene.layers[m_layer].poseAt(m_frame)) : 0.0;
    m_rebuilding = true;
    bool const pegged = valid && e.type == Effect::ApplyPeg;
    bool const plain = valid && (e.type == Effect::ChannelSelector || e.type == Effect::Crop || e.type == Effect::MatteComposite || e.type == Effect::Blending
                                 || e.type == Effect::Glue
                                 || pegged);
    bool const curving = valid && e.type == Effect::ColourCurves;
    m_amountLabel->setVisible(valid && !cutter && !plain);
    m_amount->setVisible(valid && !cutter && !plain);
    m_animated->setVisible(valid && !cutter && !plain);
    {
        bool const channel = valid && e.type == Effect::ChannelSelector, cropping = valid && e.type == Effect::Crop;
        m_more->setVisible(channel || cropping || curving);
        m_moreForm->setRowVisible(m_curveWhich, curving);
        m_moreForm->setRowVisible(m_curve, curving);
        bool const texted = valid && (e.type == Effect::Script || e.type == Effect::External);
        m_more->setVisible(m_more->isVisibleTo(this) || texted);
        m_moreForm->setRowVisible(m_text, texted);
        m_moreForm->setRowVisible(m_textApply, texted);
        m_moreForm->setRowVisible(m_textSays, texted);
        if (texted && m_text->toPlainText() != e.text)
            m_text->setPlainText(e.text);
        if (texted)
            m_text->setToolTip(e.type == Effect::Script
                                   ? tr("JavaScript run on the picture each frame: frame, amount, and image - blur(r), tint(\"#rrggbb\", a), "
                                        "transparency(a), negate(a), blackWhite(a), hueSaturation(h, s, l), brightnessContrast(b, c), pixelate(s), "
                                        "median(r), bloom(r), grain(a), directionalBlur(length, degrees), colourScale(\"#rrggbb\", a), "
                                        "pixel(x, y), setPixel(x, y, \"#rrggbb\"), width, height.")
                                   : tr("A command line run on the picture each frame: it reads {in} and writes {out} (PNG files); {frame} is the frame."));
        m_curve->setPoints(e.curves[std::clamp(m_curveWhich->currentIndex(), 0, 3)]);
        m_moreForm->setRowVisible(m_channel, channel);
        m_channel->setCurrentIndex(std::clamp(e.channel, 0, 3));
        double const area[4] = {e.area.x(), e.area.y(), e.area.width(), e.area.height()};
        for (int i = 0; i < 4; ++i) {
            m_moreForm->setRowVisible(m_cropBox[i], cropping);
            m_cropBox[i]->setValue(area[i]);
        }
    }
    m_animated->setChecked(valid && e.animated);
    m_color->setVisible(coloured);
    m_endColor->setVisible(graded);
    if (graded)
        m_endColor->setStyleSheet(QStringLiteral("background-color: %1;").arg(e.endColor.name()));
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
        bool const hs = valid && e.type == Effect::HueSaturation, bc = valid && e.type == Effect::BrightnessContrast;
        bool const swirls = valid && e.type == Effect::Turbulence, generator = valid && e.type == Effect::MatteGenerator;
        bool const waves = (valid && e.type == Effect::Ripple) || swirls;
        bool const focusing = valid && e.type == Effect::Focus;
        bool const along = (valid && (e.type == Effect::DirectionalBlur || e.type == Effect::Gradient)) || waves || generator || focusing;
        m_adjust->setVisible(hs || bc || along);
        // The last box: the way a Directional Blur smears, or how far apart a Ripple's waves are.
        if (QLabel* const label = qobject_cast<QLabel*>(m_adjustForm->labelForField(m_adjustBox[5])))
            label->setText(focusing ? tr("In Focus At") : generator ? tr("Inner Radius") : swirls ? tr("Swirl Size") : waves ? tr("Wave Length") : tr("Direction"));
        m_adjustBox[5]->setRange(focusing ? -10000.0 : generator ? 0.0 : waves ? 2.0 : -360.0, focusing ? 10000.0 : waves || generator ? 2000.0 : 360.0);
        m_adjustBox[5]->setSingleStep(focusing ? 10.0 : waves || generator ? 10.0 : 15.0);
        m_adjustBox[5]->setToolTip(focusing ? tr("The depth that is sharp (towards the camera, in scene units): the further a layer stands from it, the more blurred.")
                                   : generator ? tr("How far out from the drawing the matte stays solid, in scene units.")
                                   : waves   ? tr("How far apart the waves are, in scene units.")
                                             : tr("Which way the picture is smeared (degrees: 0 across, 90 up and down)."));
        bool const shownFor[6] = {hs, hs, hs, bc, bc, along};
        double const values[6] = {e.hue, e.saturation, e.lightness, e.brightness, e.contrast, e.direction};
        for (int i = 0; i < 6; ++i) {
            m_adjustForm->setRowVisible(m_adjustBox[i], shownFor[i]);
            m_adjustBox[i]->setValue(values[i]);
        }
    }
    m_matteLabel->setVisible(cutter || particles || shading || pegged);
    m_matteLabel->setText(particles ? tr("Bounce Off") : pegged ? tr("Peg") : tr("Matte"));
    m_matte->setVisible(cutter || particles || shading || pegged);
    m_matte->setToolTip(pegged ? tr("The layer (a peg, mostly) whose motion on each frame moves this layer's picture as well.")
                        : particles ? tr("A layer whose drawing the particles bounce off (keeping as much of their speed as Bounce says).")
                        : shading ? tr("The layer whose drawing says where the shade (or the light) falls on this one.")
                                  : tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert->setVisible(cutter || shading || resizing || graded);
    m_invert->setText(resizing ? tr("Shrink") : graded ? tr("Radial") : tr("Inverted"));
    m_invert->setToolTip(resizing ? tr("Make what is painted smaller instead of bigger.") : graded ? tr("From the middle out, instead of from side to side.")
                                  : tr("Keep what lies outside the matte instead."));
    if (cutter || shading || resizing || graded)
        m_invert->setChecked(e.invert);
    if (cutter || particles || shading || pegged) {
        m_matte->clear();
        m_matte->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            if (l != m_layer && (pegged || scene.layers[l].holdsDrawings()))
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
                         || e.type == Effect::Grain || e.type == Effect::RadialBlur || e.type == Effect::ColourScale || e.type == Effect::Negate
                         || e.type == Effect::BlackWhite || e.type == Effect::Gradient || e.type == Effect::RemoveTransparency)) {
        m_amountLabel->setText(e.type == Effect::Transparency ? tr("Transparency") : e.type == Effect::RemoveTransparency ? tr("Threshold") : tr("Amount"));
        m_amount->setRange(0.0, 100.0);
        m_amount->setDecimals(0);
        m_amount->setSuffix(QStringLiteral(" %"));
        m_amount->setValue(amount * 100.0);
    } else if (valid && !cutter && !plain) {
        m_amountLabel->setText(e.type == Effect::Ripple ? tr("Height") : e.type == Effect::MatteBlur ? tr("Radius") : shading ? tr("Softness")
                               : resizing ? tr("By") : e.type == Effect::Pixelate ? tr("Block Size")
                               : e.type == Effect::Turbulence ? tr("Strength") : e.type == Effect::MatteGenerator ? tr("Outer Radius")
                               : e.type == Effect::MotionBlur ? tr("Frames") : e.type == Effect::FieldChart ? tr("Line Width")
                               : e.type == Effect::Focus ? tr("Blur per 100") : e.type == Effect::ScaleOutput ? tr("Scale") : tr("Radius"));
        m_amount->setRange(0.0, 500.0);
        m_amount->setDecimals(1);
        m_amount->setSuffix(QString());
        m_amount->setValue(amount);
    } else if (cutter) {
        m_invert->setChecked(e.invert);
    }
    m_rebuilding = false;
}
