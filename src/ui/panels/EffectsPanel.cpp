// Eclipse - the effects of the current layer: the panel made, effects added, copied, pasted, removed, the list and the editors shown.
#include "ui/panels/EffectsPanel.h"
#include "ui/panels/EffectsPanelEdit.h"
#include "ui/panels/EffectParamsEditor.h"

#include <QCoreApplication>

#include <QCheckBox>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QDir>
#include <QLineEdit>
#include "ui/panels/ColourSpaceBox.h"
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
    {
        // Particles made ready: Fire (rising, short-lived, turning red and small) and Rain (falling fast across a wide band, in the wind).
        QMenu* const presets = menu->addMenu(tr("Particle Presets"));
        connect(presets->addAction(tr("Fire")), &QAction::triggered, this, [this] {
            Effect e = Effect::made(Effect::Particles);
            e.life = 18;
            e.speed = 4.0;
            e.spread = 35.0;
            e.direction = 90.0;
            e.gravity = -0.15;
            e.endSize = 0.25;
            e.endColor = QColor(200, 40, 10);
            e.regionWidth = 60.0;
            e.regionHeight = 12.0;
            addEffect(e);
        });
        connect(presets->addAction(tr("Rain")), &QAction::triggered, this, [this] {
            Effect e = Effect::made(Effect::Particles);
            e.life = 30;
            e.speed = 18.0;
            e.spread = 4.0;
            e.direction = 270.0;
            e.gravity = 0.6;
            e.endSize = 1.0;
            e.endColor = QColor(150, 170, 200);
            e.wind = 1.5;
            e.regionWidth = 1600.0;
            e.regionHeight = 10.0;
            addEffect(e);
        });
    }
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
    connect(menu->addAction(tr("Apply Camera")), &QAction::triggered, this, [this] { add(Effect::ApplyCamera); });
    connect(menu->addAction(tr("Chroma Keying")), &QAction::triggered, this, [this] { add(Effect::ChromaKey); });
    connect(menu->addAction(tr("Luminance Threshold")), &QAction::triggered, this, [this] { add(Effect::LuminanceThreshold); });
    // The newer kinds, by what they do.
    struct Group {
        char const* name;
        QVector<Effect::Type> kinds;
    };
    QVector<Group> const groups = {
        {QT_TR_NOOP("More Blurs"), {Effect::GaussianBlur, Effect::BoxBlur, Effect::ZoomBlur, Effect::BokehBlur, Effect::AntiFlicker, Effect::UnsharpMask}},
        {QT_TR_NOOP("More Colour Corrections"), {Effect::ColourFade, Effect::ChannelSwap, Effect::ColourLevels, Effect::ColourScreen, Effect::Contrast, Effect::Gamma,
                                                 Effect::ColourBanding, Effect::Dither, Effect::IncreaseOpacity, Effect::ColourSpace, Effect::LookUpTable}},
        {QT_TR_NOOP("Light"), {Effect::Shine, Effect::LensFlare}},
        {QT_TR_NOOP("Generate"), {Effect::TurbulentNoise, Effect::Grid, Effect::BurnIn, Effect::ShapeCurve}},
        {QT_TR_NOOP("Move"), {Effect::Shake, Effect::Quake}},
        {QT_TR_NOOP("Timing"), {Effect::HoldTiming, Effect::HoldTransformationTiming, Effect::Retime, Effect::RetimeTransformation, Effect::Loop, Effect::LoopTransformation}}};
    for (Group const& group : groups) {
        QMenu* const sub = menu->addMenu(tr(group.name));
        for (Effect::Type kind : group.kinds)
            connect(sub->addAction(Effects::kindName(kind)), &QAction::triggered, this, [this, kind] { add(kind); });
    }
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
    m_truck = new QCheckBox(tr("Truck Factor"), this);
    m_truck->setObjectName(QStringLiteral("EffectTruckFactor"));
    m_truck->setToolTip(tr("The blur grows as the layer comes nearer the camera and shrinks as it goes further away, as the layer itself does."));
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
    amountRow->addWidget(m_truck);
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
    m_params = new EffectParamsEditor(this);
    layout->addWidget(m_params);
    connect(m_params, &EffectParamsEditor::edited, this, [this](QString const& key, double value) {
        if (!m_rebuilding)
            edit([key, value](Effect& e) { e.setValue(key, value); });
    });

    connectEditors();
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


void EffectsPanel::add(Effect::Type type)
{
    addEffect(Effect::made(type));
}

void EffectsPanel::addEffect(Effect const& made)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    // (A peg draws nothing for an effect to work on.)
    if (scene.layers[m_layer].kind == Layer::Peg) {
        QMessageBox::warning(this, tr("Add Effect"), tr("Effects cannot be inserted directly underneath pegs."));
        return;
    }
    Effect const& e = made;
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
                                    || e.type == Effect::MatteGenerator || e.type == Effect::Gradient || e.type == Effect::ChromaKey) && e.type != Effect::MatteBlur;
    bool const graded = valid && e.type == Effect::Gradient;
    // What the amount is on this frame: the keyframes say, for an animated effect.
    double const amount = valid ? scene.layers[m_layer].effectAmount(row, scene.layers[m_layer].poseAt(m_frame)) : 0.0;
    m_rebuilding = true;
    bool const viewed = valid && e.type == Effect::ApplyCamera;
    bool const pegged = valid && (e.type == Effect::ApplyPeg || viewed);
    bool const plain = valid && (e.type == Effect::ChannelSelector || e.type == Effect::Crop || e.type == Effect::MatteComposite || e.type == Effect::Blending
                                 || e.type == Effect::Glue || e.type == Effect::ColourSpace
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
        bool const texted = valid && (e.type == Effect::Script || e.type == Effect::External || e.type == Effect::BurnIn);
        m_more->setVisible(m_more->isVisibleTo(this) || texted);
        m_moreForm->setRowVisible(m_text, texted);
        m_moreForm->setRowVisible(m_textApply, texted);
        bool const spaced = valid && e.type == Effect::ColourSpace, looked = valid && e.type == Effect::LookUpTable;
        m_more->setVisible(m_more->isVisibleTo(this) || spaced || looked);
        m_moreForm->setRowVisible(m_textSays, texted || looked);
        m_moreForm->setRowVisible(m_fromSpace, spaced);
        m_moreForm->setRowVisible(m_toSpace, spaced);
        m_moreForm->setRowVisible(m_lutRow, looked);
        if (spaced) {
            ColourSpaceBox::fill(m_fromSpace, e.fromSpace, tr("Scene Working Colour Space"));
            ColourSpaceBox::fill(m_toSpace, e.toSpace, tr("Scene Working Colour Space"));
        }
        if (looked && m_lutFile->text() != QDir::toNativeSeparators(e.file))
            m_lutFile->setText(QDir::toNativeSeparators(e.file));
        if (!texted && !looked)
            m_textSays->clear();
        if (texted && m_text->toPlainText() != e.text)
            m_text->setPlainText(e.text);
        if (texted)
            m_text->setToolTip(e.type == Effect::BurnIn ? tr("The text shown, then Apply: %f the frame, %s the scene's name, %t the time code, %% a percent sign.")
                               : e.type == Effect::Script
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
    m_truck->setVisible(valid && Effect::trucks(e.type));
    m_truck->setChecked(valid && e.truckFactor);
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
    m_matteLabel->setText(particles ? tr("Bounce Off") : viewed ? tr("Camera") : pegged ? tr("Peg") : tr("Matte"));
    m_matte->setVisible(cutter || particles || shading || pegged);
    m_matte->setToolTip(viewed ? tr("The camera this layer's picture is seen through, instead of the scene's.")
                        : pegged ? tr("The layer (a peg, mostly) whose motion on each frame moves this layer's picture as well.")
                        : particles ? tr("A layer whose drawing the particles bounce off (keeping as much of their speed as Bounce says).")
                        : shading ? tr("The layer whose drawing says where the shade (or the light) falls on this one.")
                                  : tr("The layer whose drawing cuts this one: only what lies inside it stays."));
    m_invert->setVisible(cutter || shading || resizing || graded || viewed || (valid && (e.type == Effect::ChromaKey || e.type == Effect::LuminanceThreshold)));
    m_invert->setText(viewed ? tr("Apply Camera Viewpoint") : resizing ? tr("Shrink") : graded ? tr("Radial") : tr("Inverted"));
    m_invert->setToolTip(resizing ? tr("Make what is painted smaller instead of bigger.") : graded ? tr("From the middle out, instead of from side to side.")
                                  : tr("Keep what lies outside the matte instead."));
    if (cutter || shading || resizing || graded)
        m_invert->setChecked(e.invert);
    if (viewed) {
        m_invert->setChecked(e.cameraViewpoint);
        m_invert->setToolTip(tr("Seen through the camera. Off, the picture moves with the camera instead, as with a peg."));
    }
    if (cutter || particles || shading || pegged) {
        m_matte->clear();
        m_matte->addItem(tr("(none)"), 0);
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
            if (l != m_layer && (viewed ? scene.layers[l].kind == Layer::Camera : pegged || scene.layers[l].holdsDrawings()))
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
                         || e.type == Effect::BlackWhite || e.type == Effect::Gradient || e.type == Effect::RemoveTransparency || e.type == Effect::ChromaKey
                         || e.type == Effect::LuminanceThreshold)) {
        m_amountLabel->setText(e.type == Effect::Transparency ? tr("Transparency")
                               : e.type == Effect::RemoveTransparency || e.type == Effect::ChromaKey || e.type == Effect::LuminanceThreshold ? tr("Threshold")
                               : tr("Amount"));
        m_amount->setRange(0.0, 100.0);
        m_amount->setDecimals(0);
        m_amount->setSuffix(QStringLiteral(" %"));
        m_amount->setValue(amount * 100.0);
    } else if (valid && Effect::amountOf(e.type).shown >= 0) {
        // A newer kind: its amount as its table says, or none.
        Effect::Amount const a = Effect::amountOf(e.type);
        m_amountLabel->setVisible(a.shown > 0);
        m_amount->setVisible(a.shown > 0);
        m_animated->setVisible(a.shown > 0);
        m_amountLabel->setText(QCoreApplication::translate("Effect", a.label ? a.label : "Amount"));
        m_amount->setRange(0.0, a.shown == 1 ? 100.0 : a.most);
        m_amount->setDecimals(a.shown == 1 ? 0 : 1);
        m_amount->setSuffix(a.shown == 1 ? QStringLiteral(" %") : QString());
        m_amount->setValue(a.shown == 1 ? amount * 100.0 : amount);
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
    m_params->setVisible(valid && !Effect::params(e.type).isEmpty());
    if (valid)
        m_params->setEffect(e);
    m_rebuilding = false;
}
