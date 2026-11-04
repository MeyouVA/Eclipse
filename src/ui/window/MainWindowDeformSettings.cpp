// Eclipse - the main window: Deformation Settings - each bone's zone of influence, the chain's scaling and wave, how pencil
// lines follow, and whether the deformation is on at the current frame.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

void MainWindow::editDeformationSettings(int l, int bone)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size() || (scene.layers[l].bones.size() < 2 && !scene.layers[l].hasWarp())) {
        statusBar()->showMessage(tr("This layer has no bones or curve deformer: make some with the Rigging tool."), 6000);
        return;
    }
    Layer const& layer = scene.layers[l];
    int const boneCount = std::max(0, int(layer.bones.size()) - 1);
    QVector<BoneInfluence> zones = layer.influences;
    zones.resize(std::max(int(zones.size()), boneCount));

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("DeformationSettingsDialog"));
    dialog.setWindowTitle(tr("Deformation Settings: %1").arg(layer.name));
    auto number = [&](char const* name, double value, double low, double high, int decimals = 1) {
        QDoubleSpinBox* const box = new QDoubleSpinBox(&dialog);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setValue(value);
        return box;
    };

    // ---- Zone of influence (Region of Influence) of the bone chosen ----
    QGroupBox* const zoneBox = new QGroupBox(tr("Zone of Influence"), &dialog);
    QFormLayout* const zoneForm = new QFormLayout(zoneBox);
    QSpinBox* const which = new QSpinBox(&dialog);
    which->setObjectName(QStringLiteral("DeformBone"));
    which->setRange(1, std::max(1, boneCount));
    which->setValue(std::clamp(bone + 1, 1, std::max(1, boneCount)));
    QComboBox* const type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("DeformZoneType"));
    type->addItems({tr("Infinite Influence"), tr("Elliptic Influence"), tr("Zero Influence"), tr("Shaped Influence")});
    QDoubleSpinBox* const left = number("DeformLeft", 60, 0, 100000);
    QDoubleSpinBox* const right = number("DeformRight", 60, 0, 100000);
    QCheckBox* const symmetric = new QCheckBox(tr("Symmetric Ellipse of Influence"), &dialog);
    symmetric->setObjectName(QStringLiteral("DeformSymmetric"));
    QDoubleSpinBox* const begin = number("DeformBegin", 40, 0, 100000);
    QDoubleSpinBox* const end = number("DeformEnd", 40, 0, 100000);
    QDoubleSpinBox* const fade = number("DeformFade", 20, 0, 100000);
    QDoubleSpinBox* const exponent = number("DeformExponent", 1, 0.05, 20, 2);
    QCheckBox* const fadeInside = new QCheckBox(tr("Fade Inside Zones"), &dialog);
    fadeInside->setObjectName(QStringLiteral("DeformFadeInside"));
    zoneForm->addRow(tr("Bone"), which);
    zoneForm->addRow(tr("Influence Type"), type);
    zoneForm->addRow(tr("Transversal Influence Radius Left"), left);
    zoneForm->addRow(tr("Transversal Influence Radius Right"), right);
    zoneForm->addRow(symmetric);
    zoneForm->addRow(tr("Longitudinal Influence Radius Begin"), begin);
    zoneForm->addRow(tr("Longitudinal Influence Radius End"), end);
    zoneForm->addRow(tr("Influence Fade Radius"), fade);
    zoneForm->addRow(tr("Influence Fade Exponent"), exponent);
    zoneForm->addRow(fadeInside);
    zoneBox->setEnabled(boneCount > 0);
    int shown = -1;
    auto keep = [&] {
        if (shown < 0 || shown >= zones.size())
            return;
        BoneInfluence& z = zones[shown];
        z.type = type->currentIndex();
        z.left = left->value();
        z.right = right->value();
        z.symmetric = symmetric->isChecked();
        z.begin = begin->value();
        z.end = end->value();
        z.fade = fade->value();
        z.exponent = exponent->value();
        z.fadeInside = fadeInside->isChecked();
    };
    auto showBone = [&](int b) {
        keep();
        shown = b;
        BoneInfluence const z = b >= 0 && b < zones.size() ? zones[b] : BoneInfluence();
        type->setCurrentIndex(z.type);
        left->setValue(z.left);
        right->setValue(z.right);
        symmetric->setChecked(z.symmetric);
        begin->setValue(z.begin);
        end->setValue(z.end);
        fade->setValue(z.fade);
        exponent->setValue(z.exponent);
        fadeInside->setChecked(z.fadeInside);
    };
    auto enableZone = [&] {
        bool const sized = type->currentIndex() == BoneInfluence::Elliptic;
        for (QWidget* w : std::initializer_list<QWidget*>{left, begin, end, symmetric})
            w->setEnabled(sized);
        right->setEnabled(sized && !symmetric->isChecked());
        fade->setEnabled(sized || type->currentIndex() == BoneInfluence::Shaped);
        exponent->setEnabled(fade->isEnabled());
    };
    showBone(which->value() - 1);
    enableZone();
    connect(which, &QSpinBox::valueChanged, &dialog, [&](int v) {
        showBone(v - 1);
        enableZone();
    });
    connect(type, &QComboBox::currentIndexChanged, &dialog, [&] { enableZone(); });
    connect(symmetric, &QCheckBox::toggled, &dialog, [&] { enableZone(); });

    // ---- Deformation Scale and Wave ----
    ChainScale const& was = layer.chainScale;
    QGroupBox* const scaleBox = new QGroupBox(tr("Deformation Scale"), &dialog);
    QFormLayout* const scaleForm = new QFormLayout(scaleBox);
    QDoubleSpinBox* const leftStart = number("DeformScaleLeftStart", was.leftStart, 0, 100, 2);
    QDoubleSpinBox* const leftEnd = number("DeformScaleLeftEnd", was.leftEnd, 0, 100, 2);
    QDoubleSpinBox* const rightStart = number("DeformScaleRightStart", was.rightStart, 0, 100, 2);
    QDoubleSpinBox* const rightEnd = number("DeformScaleRightEnd", was.rightEnd, 0, 100, 2);
    QCheckBox* const uniform = new QCheckBox(tr("Uniform Scale (both sides as the left)"), &dialog);
    uniform->setObjectName(QStringLiteral("DeformUniform"));
    uniform->setChecked(was.uniform);
    scaleForm->addRow(tr("Left Start Scale"), leftStart);
    scaleForm->addRow(tr("Left End Scale"), leftEnd);
    scaleForm->addRow(tr("Right Start Scale"), rightStart);
    scaleForm->addRow(tr("Right End Scale"), rightEnd);
    scaleForm->addRow(uniform);
    QCheckBox* const curved = new QCheckBox(tr("Along a curve (handles)"), &dialog);
    curved->setObjectName(QStringLiteral("DeformScaleCurved"));
    curved->setChecked(was.curved);
    QDoubleSpinBox* const lshp = number("DeformLeftStartHandlePos", was.leftStartHandlePos, 0, 1, 3);
    QDoubleSpinBox* const lshs = number("DeformLeftStartHandleScale", was.leftStartHandleScale, 0, 100, 2);
    QDoubleSpinBox* const lehp = number("DeformLeftEndHandlePos", was.leftEndHandlePos, 0, 1, 3);
    QDoubleSpinBox* const lehs = number("DeformLeftEndHandleScale", was.leftEndHandleScale, 0, 100, 2);
    QDoubleSpinBox* const rshp = number("DeformRightStartHandlePos", was.rightStartHandlePos, 0, 1, 3);
    QDoubleSpinBox* const rshs = number("DeformRightStartHandleScale", was.rightStartHandleScale, 0, 100, 2);
    QDoubleSpinBox* const rehp = number("DeformRightEndHandlePos", was.rightEndHandlePos, 0, 1, 3);
    QDoubleSpinBox* const rehs = number("DeformRightEndHandleScale", was.rightEndHandleScale, 0, 100, 2);
    QDoubleSpinBox* const odl = number("DeformOffsetDeformerLeft", was.offsetDeformerLeft, -100000, 100000);
    QDoubleSpinBox* const odr = number("DeformOffsetDeformerRight", was.offsetDeformerRight, -100000, 100000);
    QDoubleSpinBox* const osl = number("DeformOffsetScalingLeft", was.offsetScalingLeft, -100000, 100000);
    QDoubleSpinBox* const osr = number("DeformOffsetScalingRight", was.offsetScalingRight, -100000, 100000);
    scaleForm->addRow(curved);
    scaleForm->addRow(tr("Left Start Handle Position"), lshp);
    scaleForm->addRow(tr("Left Start Handle Scale"), lshs);
    scaleForm->addRow(tr("Left End Handle Position"), lehp);
    scaleForm->addRow(tr("Left End Handle Scale"), lehs);
    scaleForm->addRow(tr("Right Start Handle Position"), rshp);
    scaleForm->addRow(tr("Right Start Handle Scale"), rshs);
    scaleForm->addRow(tr("Right End Handle Position"), rehp);
    scaleForm->addRow(tr("Right End Handle Scale"), rehs);
    scaleForm->addRow(tr("Left Offset Deformer"), odl);
    scaleForm->addRow(tr("Right Offset Deformer"), odr);
    scaleForm->addRow(tr("Left Offset Scaling"), osl);
    scaleForm->addRow(tr("Right Offset Scaling"), osr);
    QGroupBox* const waveBox = new QGroupBox(tr("Deformation Wave"), &dialog);
    QFormLayout* const waveForm = new QFormLayout(waveBox);
    QDoubleSpinBox* const waveLeft = number("DeformWaveLeft", was.waveLeft, -10, 10, 2);
    QDoubleSpinBox* const periodLeft = number("DeformPeriodLeft", was.periodLeft, 1, 100000);
    QDoubleSpinBox* const offsetLeft = number("DeformOffsetLeft", was.offsetLeft, -100000, 100000);
    QCheckBox* const same = new QCheckBox(tr("Same as Left"), &dialog);
    same->setObjectName(QStringLiteral("DeformWaveSame"));
    same->setChecked(was.waveSameAsLeft);
    QDoubleSpinBox* const waveRight = number("DeformWaveRight", was.waveRight, -10, 10, 2);
    QDoubleSpinBox* const periodRight = number("DeformPeriodRight", was.periodRight, 1, 100000);
    QDoubleSpinBox* const offsetRight = number("DeformOffsetRight", was.offsetRight, -100000, 100000);
    waveForm->addRow(tr("Left Amplitude"), waveLeft);
    waveForm->addRow(tr("Left Period"), periodLeft);
    waveForm->addRow(tr("Left Start"), offsetLeft);
    QDoubleSpinBox* const spanLeft = number("DeformSpanLeft", was.spanLeft, 0, 100000);
    QDoubleSpinBox* const fadeInLeft = number("DeformFadeInLeft", was.fadeInLeft, 0, 100000);
    QDoubleSpinBox* const fadeOutLeft = number("DeformFadeOutLeft", was.fadeOutLeft, 0, 100000);
    waveForm->addRow(tr("Left Span"), spanLeft);
    waveForm->addRow(tr("Left Fade In"), fadeInLeft);
    waveForm->addRow(tr("Left Fade Out"), fadeOutLeft);
    waveForm->addRow(same);
    waveForm->addRow(tr("Right Amplitude"), waveRight);
    waveForm->addRow(tr("Right Period"), periodRight);
    waveForm->addRow(tr("Right Start"), offsetRight);
    QDoubleSpinBox* const spanRight = number("DeformSpanRight", was.spanRight, 0, 100000);
    QDoubleSpinBox* const fadeInRight = number("DeformFadeInRight", was.fadeInRight, 0, 100000);
    QDoubleSpinBox* const fadeOutRight = number("DeformFadeOutRight", was.fadeOutRight, 0, 100000);
    waveForm->addRow(tr("Right Span"), spanRight);
    waveForm->addRow(tr("Right Fade In"), fadeInRight);
    waveForm->addRow(tr("Right Fade Out"), fadeOutRight);
    auto enableSides = [&] {
        rightStart->setEnabled(!uniform->isChecked());
        rightEnd->setEnabled(!uniform->isChecked());
        for (QWidget* w : std::initializer_list<QWidget*>{waveRight, periodRight, offsetRight, spanRight, fadeInRight, fadeOutRight})
            w->setEnabled(!same->isChecked());
        for (QWidget* w : std::initializer_list<QWidget*>{lshp, lshs, lehp, lehs, rshp, rshs, rehp, rehs})
            w->setEnabled(curved->isChecked());
        for (QWidget* w : std::initializer_list<QWidget*>{rshp, rshs, rehp, rehs, odr, osr})
            w->setEnabled(w->isEnabled() && !uniform->isChecked());
    };
    enableSides();
    connect(uniform, &QCheckBox::toggled, &dialog, [&] { enableSides(); });
    connect(same, &QCheckBox::toggled, &dialog, [&] { enableSides(); });
    connect(curved, &QCheckBox::toggled, &dialog, [&] { enableSides(); });

    // ---- Pencil lines ----
    QGroupBox* const lineBox = new QGroupBox(tr("Line Thickness: Deformation"), &dialog);
    QFormLayout* const lineForm = new QFormLayout(lineBox);
    QComboBox* const quality = new QComboBox(&dialog);
    quality->setObjectName(QStringLiteral("DeformLineQuality"));
    quality->addItems({tr("Low"), tr("Medium"), tr("High"), tr("Very High")});
    quality->setCurrentIndex(layer.lineDeformQuality);
    QCheckBox* const preserve = new QCheckBox(tr("Preserve Thickness"), &dialog);
    preserve->setObjectName(QStringLiteral("DeformPreserveThickness"));
    preserve->setChecked(layer.linePreserveThickness);
    preserve->setToolTip(tr("On: pencil lines stay as thick as drawn. Off: they grow and shrink with the drawing where it is stretched."));
    QSpinBox* const smooth = new QSpinBox(&dialog);
    smooth->setObjectName(QStringLiteral("DeformLineSmooth"));
    smooth->setRange(0, 10);
    smooth->setValue(layer.lineDeformSmooth);
    QDoubleSpinBox* const fit = number("DeformFitError", layer.lineDeformFitError, 0, 50, 2);
    lineForm->addRow(tr("Pencil Line Deformation Quality"), quality);
    lineForm->addRow(preserve);
    lineForm->addRow(tr("Smooth"), smooth);
    lineForm->addRow(tr("Fit Error"), fit);

    // ---- On or off here ----
    QCheckBox* const enabled = new QCheckBox(tr("Enable Deformation (from this frame)"), &dialog);
    enabled->setObjectName(QStringLiteral("DeformEnabled"));
    double const amountWas = layer.poseAt(frame()).deform;
    enabled->setChecked(amountWas > 0.0);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QHBoxLayout* const columns = new QHBoxLayout;
    QVBoxLayout* const first = new QVBoxLayout;
    first->addWidget(zoneBox);
    first->addWidget(lineBox);
    first->addStretch(1);
    QVBoxLayout* const second = new QVBoxLayout;
    second->addWidget(scaleBox);
    second->addWidget(waveBox);
    second->addStretch(1);
    columns->addLayout(first);
    columns->addLayout(second);
    QWidget* const page = new QWidget;
    page->setLayout(columns);
    QScrollArea* const scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(scroll, 1);
    layout->addWidget(enabled);
    layout->addWidget(buttons);
    dialog.resize(900, 700);
    if (dialog.exec() != QDialog::Accepted)
        return;
    keep();
    // Zones past the last bone, and trailing ones left as they come, are not kept.
    zones.resize(boneCount);
    while (!zones.isEmpty() && zones.last() == BoneInfluence())
        zones.removeLast();
    ChainScale scale;
    scale.leftStart = leftStart->value();
    scale.leftEnd = leftEnd->value();
    scale.rightStart = rightStart->value();
    scale.rightEnd = rightEnd->value();
    scale.uniform = uniform->isChecked();
    scale.waveLeft = waveLeft->value();
    scale.periodLeft = periodLeft->value();
    scale.offsetLeft = offsetLeft->value();
    scale.waveSameAsLeft = same->isChecked();
    scale.waveRight = waveRight->value();
    scale.periodRight = periodRight->value();
    scale.offsetRight = offsetRight->value();
    scale.spanLeft = spanLeft->value();
    scale.fadeInLeft = fadeInLeft->value();
    scale.fadeOutLeft = fadeOutLeft->value();
    scale.spanRight = spanRight->value();
    scale.fadeInRight = fadeInRight->value();
    scale.fadeOutRight = fadeOutRight->value();
    scale.curved = curved->isChecked();
    scale.leftStartHandlePos = lshp->value();
    scale.leftStartHandleScale = lshs->value();
    scale.leftEndHandlePos = lehp->value();
    scale.leftEndHandleScale = lehs->value();
    scale.rightStartHandlePos = rshp->value();
    scale.rightStartHandleScale = rshs->value();
    scale.rightEndHandlePos = rehp->value();
    scale.rightEndHandleScale = rehs->value();
    scale.offsetDeformerLeft = odl->value();
    scale.offsetDeformerRight = odr->value();
    scale.offsetScalingLeft = osl->value();
    scale.offsetScalingRight = osr->value();
    int const q = quality->currentIndex();
    bool const keepThick = preserve->isChecked();
    int const passes = smooth->value();
    double const fitError = fit->value();
    m_document->beginMacro(tr("Deformation Settings"));
    m_document->editLayers({l}, tr("Deformation Settings"), [&](Layer& edited) {
        edited.influences = zones;
        edited.chainScale = scale;
        edited.lineDeformQuality = q;
        edited.linePreserveThickness = keepThick;
        edited.lineDeformSmooth = passes;
        edited.lineDeformFitError = fitError;
    });
    if (enabled->isChecked() != (amountWas > 0.0))
        m_document->setDeformEnabled(l, frame(), enabled->isChecked() ? 1.0 : 0.0);
    m_document->endMacro();
}

bool MainWindow::editPointKinematicOutput(int l, bool make)
{
    // The trackers (in the parent's coordinates at rest) and how they are sampled: for a new output under layer `l`, or for
    // the output `l` itself.
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return false;
    int const parent = make ? l : scene.parentIndex(l);
    if (parent < 0)
        return false;
    QVector<QPointF> trackers = make ? QVector<QPointF>() : scene.layers[l].trackers;
    int sampling = make ? int(Layer::OnePoint) : scene.layers[l].trackerSampling;
    if (trackers.isEmpty()) {
        // To begin with: the middle of the drawing shown, and two more beside and below it.
        Layer const& from = scene.layers[parent];
        int const index = from.drawingAt(frame());
        QRectF const box = index != Layer::kNoDrawing ? from.drawings[index].bounds() : QRectF(0, 0, scene.width, scene.height);
        trackers = {box.center(), box.center() + QPointF(std::max(20.0, box.width() / 4.0), 0.0),
                    box.center() + QPointF(0.0, std::max(20.0, box.height() / 4.0))};
    }
    while (trackers.size() < 3)
        trackers.push_back(trackers.last() + QPointF(20.0, 20.0));
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PointKinematicOutputDialog"));
    dialog.setWindowTitle(tr("Point Kinematic Output"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("PointKinematicSampling"));
    type->addItems({tr("One-Point Sampling"), tr("Two-Point Sampling"), tr("Two-Point Sampling with Scaling"), tr("Three-Point Sampling")});
    type->setCurrentIndex(sampling);
    form->addRow(tr("Sampling Type"), type);
    QStringList const names = {tr("Main Position Tracker"), tr("Tracker 2"), tr("Tracker 3")};
    QVector<QDoubleSpinBox*> xs, ys;
    for (int i = 0; i < 3; ++i) {
        QDoubleSpinBox* const x = new QDoubleSpinBox(&dialog);
        QDoubleSpinBox* const y = new QDoubleSpinBox(&dialog);
        for (QDoubleSpinBox* box : {x, y}) {
            box->setRange(-100000, 100000);
            box->setDecimals(1);
        }
        x->setObjectName(QStringLiteral("PointKinematicX%1").arg(i));
        y->setObjectName(QStringLiteral("PointKinematicY%1").arg(i));
        x->setValue(trackers[i].x());
        y->setValue(trackers[i].y());
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(x);
        row->addWidget(y);
        form->addRow(names[i], row);
        xs << x;
        ys << y;
    }
    auto enable = [&] {
        int const used = type->currentIndex() == Layer::OnePoint ? 1 : type->currentIndex() == Layer::ThreePoint ? 3 : 2;
        for (int i = 0; i < 3; ++i) {
            xs[i]->setEnabled(i < used);
            ys[i]->setEnabled(i < used);
        }
    };
    enable();
    connect(type, &QComboBox::currentIndexChanged, &dialog, [&] { enable(); });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    int const used = type->currentIndex() == Layer::OnePoint ? 1 : type->currentIndex() == Layer::ThreePoint ? 3 : 2;
    QVector<QPointF> chosen;
    for (int i = 0; i < used; ++i)
        chosen.push_back(QPointF(xs[i]->value(), ys[i]->value()));
    if (make) {
        int const made = m_document->addPointKinematicOutput(parent, chosen, type->currentIndex());
        if (made >= 0)
            m_timeline->setCurrentCell(made, frame());
        return made >= 0;
    }
    m_document->setTrackers(l, chosen, type->currentIndex());
    return true;
}

void MainWindow::onActionAddPointKinematicOutput()
{
    Layer const& now = m_document->scene().layers[layer()];
    if (!now.trackers.isEmpty()) {
        editPointKinematicOutput(layer(), false);    // a Point Kinematic Output chosen: its trackers
        return;
    }
    if (now.bones.size() < 2 && !now.hasWarp()) {
        statusBar()->showMessage(tr("A Point Kinematic Output follows points of a deformed drawing: select a layer with bones or a curve deformer."), 7000);
        return;
    }
    editPointKinematicOutput(layer(), true);
}
