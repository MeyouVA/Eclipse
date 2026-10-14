#include "ui/panels/LayerPanel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QComboBox>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

#include "model/document/Commands.h"
#include "ui/panels/Effects.h"

QDoubleSpinBox* LayerPanel::number(double min, double max, double step, int decimals)
{
    QDoubleSpinBox* const box = new QDoubleSpinBox(this);
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setDecimals(decimals);
    // Only a finished entry counts: every digit typed would otherwise be an undo step.
    box->setKeyboardTracking(false);
    return box;
}

LayerPanel::LayerPanel(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_name = new QLineEdit(this);
    m_kind = new QLabel(this);
    m_parent = new QLabel(this);
    m_key = new QLabel(this);
    m_bone = new QComboBox(this);
    m_bone->setToolTip(tr("The bone of the layer this one is attached to that it moves with."));
    m_x = number(-100000, 100000, 1.0, 2);
    m_y = number(-100000, 100000, 1.0, 2);
    m_z = number(-100000, 100000, 10.0, 2);
    m_z->setObjectName(QStringLiteral("LayerDepth"));
    m_z->setToolTip(tr("How far the layer stands towards the camera (positive: forward) or away from it (negative: back).\n"
                       "Nearer things look bigger, pass by faster when the camera moves, and cover what is behind them.\n"
                       "For a camera: where it stands - forward to see less, back to see more."));
    m_rotX = number(-36000, 36000, 5.0, 2);
    m_rotX->setObjectName(QStringLiteral("LayerRotX"));
    m_rotX->setToolTip(tr("Turns the layer in space about the line across its pivot: positive, its top leans away from the camera."));
    m_rotY = number(-36000, 36000, 5.0, 2);
    m_rotY->setObjectName(QStringLiteral("LayerRotY"));
    m_rotY->setToolTip(tr("Turns the layer in space about the line up its pivot: positive, its right side goes away from the camera."));
    m_fov = number(1, 170, 1.0, 3);
    m_fov->setObjectName(QStringLiteral("CameraFov"));
    m_fov->setSuffix(QStringLiteral(" deg"));
    m_fov->setToolTip(tr("The angle the camera sees from the top of the frame to the bottom. A wide angle stands close: depth shows strongly.\n"
                         "On a camera that has keyframes the angle is set on the current frame, and glides between keyframes."));
    m_scaleX = number(-100, 100, 0.05, 3);
    m_scaleY = number(-100, 100, 0.05, 3);
    m_angle = number(-36000, 36000, 1.0, 2);
    m_skew = number(-89, 89, 1.0, 2);
    m_skew->setObjectName(QStringLiteral("LayerSkew"));
    m_skew->setToolTip(tr("Leans the layer over: its uprights tipped by so many degrees (positive: the top goes to the right)."));
    m_pivotX = number(-100000, 100000, 1.0, 2);
    m_pivotY = number(-100000, 100000, 1.0, 2);
    m_easeIn = number(0, 100, 10.0, 0);
    m_easeOut = number(0, 100, 10.0, 0);
    m_opacity = number(0, 100, 5.0, 0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setToolTip(tr("How opaque the layer is on this frame. Keyframed like position, so a layer can fade in or out."));
    m_effects = new EffectsPanel(m_document, this);
    m_card = new QPushButton(this);
    m_card->setObjectName(QStringLiteral("CardColour"));
    m_card->setToolTip(tr("The colour of the colour-card. Click to choose another."));
    m_length = number(1, 100000, 10.0, 1);
    m_length->setObjectName(QStringLiteral("BoneLength"));
    m_length->setToolTip(tr("How long the bone is. Bones attached to it start at its tip."));
    {
        // IK constraints: the bone left out of IK, or turned only between two angles.
        m_ikRow = new QWidget(this);
        QVBoxLayout* const column = new QVBoxLayout(m_ikRow);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);
        m_ikExcluded = new QCheckBox(tr("Leave out of IK"), m_ikRow);
        m_ikExcluded->setObjectName(QStringLiteral("IkExcluded"));
        m_ikExcluded->setToolTip(tr("The IK tool does not turn this bone when a bone that hangs on it is pulled."));
        m_ikLimited = new QCheckBox(tr("Turn only between"), m_ikRow);
        m_ikLimited->setObjectName(QStringLiteral("IkLimited"));
        m_ikLimited->setToolTip(tr("The IK tool turns this bone no further than these two angles - an elbow that does not bend backwards."));
        m_ikMin = number(-360, 360, 5.0, 1);
        m_ikMin->setObjectName(QStringLiteral("IkMin"));
        m_ikMax = number(-360, 360, 5.0, 1);
        m_ikMax->setObjectName(QStringLiteral("IkMax"));
        QHBoxLayout* const angles = new QHBoxLayout;
        angles->addWidget(m_ikLimited);
        angles->addWidget(m_ikMin, 1);
        angles->addWidget(new QLabel(tr("and"), m_ikRow));
        angles->addWidget(m_ikMax, 1);
        column->addWidget(m_ikExcluded);
        column->addLayout(angles);
        auto changed = [this] {
            if (!m_refreshing)
                m_document->setIkConstraints(m_layer, m_ikExcluded->isChecked(), m_ikLimited->isChecked(), m_ikMin->value(), m_ikMax->value());
        };
        connect(m_ikExcluded, &QCheckBox::toggled, this, changed);
        connect(m_ikLimited, &QCheckBox::toggled, this, changed);
        connect(m_ikMin, &QDoubleSpinBox::valueChanged, this, changed);
        connect(m_ikMax, &QDoubleSpinBox::valueChanged, this, changed);
    }
    {
        // Art layers, from the one on top to the one underneath; `sheet` is the Drawing::Sheet of each.
        m_artRow = new QWidget(this);
        QGridLayout* const grid = new QGridLayout(m_artRow);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        struct { int sheet; char const* text; char const* name; } const kArts[] = {
            {Drawing::OverlaySheet, QT_TR_NOOP("Overlay"), "ArtShownOverlay"}, {Drawing::LineSheet, QT_TR_NOOP("Line Art"), "ArtShownLine"},
            {Drawing::ColourSheet, QT_TR_NOOP("Colour Art"), "ArtShownColour"}, {Drawing::UnderlaySheet, QT_TR_NOOP("Underlay"), "ArtShownUnderlay"}};
        int at = 0;
        for (auto const& art : kArts) {
            QCheckBox* const box = new QCheckBox(tr(art.text), m_artRow);
            box->setObjectName(QLatin1String(art.name));
            box->setToolTip(tr("Whether this art layer of the layer's drawings is part of the picture."));
            int const sheet = art.sheet;
            connect(box, &QCheckBox::toggled, this, [this, sheet](bool on) {
                if (!m_refreshing)
                    m_document->setLayerSheetShown(m_layer, sheet, on);
            });
            m_artShown[sheet] = box;
            grid->addWidget(box, at / 2, at % 2);
            ++at;
        }
    }
    m_bitmap = new QCheckBox(tr("Bitmap layer"), this);
    m_bitmap->setObjectName(QStringLiteral("LayerBitmap"));
    m_bitmap->setToolTip(tr("What is drawn on the layer's line art is painted in pixels, and the eraser rubs pixels out.\nWhat was drawn before stays as it is."));
    connect(m_bitmap, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing)
            m_document->setLayerBitmap(m_layer, on);
    });
    m_bend = new QCheckBox(tr("Bends on its bones"), this);
    m_bend->setObjectName(QStringLiteral("LayerBendOnBones"));
    m_bend->setToolTip(tr("Hanging from Game Bones, the drawing bends along them as they turn, instead of riding the last one whole.\n"
                          "Each part of it follows the bone it lies along on frame 0."));
    connect(m_bend, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing)
            m_document->editLayers({m_layer}, on ? tr("Bend on Bones") : tr("Ride on Bones"), [on](Layer& layer) { layer.bendOnBones = on; });
    });
    m_easeIn->setSuffix(QStringLiteral(" %"));
    m_easeOut->setSuffix(QStringLiteral(" %"));
    m_easeIn->setToolTip(tr("How gently the motion arrives at the keyframe on this frame."));
    m_easeOut->setToolTip(tr("How gently the motion leaves the keyframe on this frame."));

    auto pair = [this](QDoubleSpinBox* a, QDoubleSpinBox* b) {
        QHBoxLayout* const row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("X"), this));
        row->addWidget(a, 1);
        row->addWidget(new QLabel(tr("Y"), this));
        row->addWidget(b, 1);
        return row;
    };
    QFormLayout* const form = new QFormLayout(this);
    m_form = form;
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Type"), m_kind);
    form->addRow(tr("Colour"), m_card);
    form->addRow(tr("Length"), m_length);
    form->addRow(tr("IK"), m_ikRow);
    form->addRow(tr("Art Layers"), m_artRow);
    form->addRow(QString(), m_bitmap);
    form->addRow(tr("Attached to"), m_parent);
    form->addRow(tr("Rides on"), m_bone);
    form->addRow(QString(), m_bend);
    form->addRow(tr("Position"), pair(m_x, m_y));
    form->addRow(tr("Depth (Z)"), m_z);
    m_turnRow = pair(m_rotX, m_rotY);
    form->addRow(tr("Turn in Space"), m_turnRow);
    form->addRow(tr("Field of View"), m_fov);
    form->addRow(tr("Scale"), pair(m_scaleX, m_scaleY));
    form->addRow(tr("Angle"), m_angle);
    form->addRow(tr("Skew"), m_skew);
    form->addRow(tr("Pivot"), pair(m_pivotX, m_pivotY));
    QHBoxLayout* const ease = new QHBoxLayout;
    ease->addWidget(new QLabel(tr("In"), this));
    ease->addWidget(m_easeIn, 1);
    ease->addWidget(new QLabel(tr("Out"), this));
    ease->addWidget(m_easeOut, 1);
    form->addRow(tr("Opacity"), m_opacity);
    form->addRow(tr("Ease"), ease);
    form->addRow(QString(), m_key);
    form->addRow(m_effects);

    connect(m_fov, &QDoubleSpinBox::valueChanged, this, [this](double degrees) {
        if (!m_refreshing)
            m_document->setFov(m_layer, degrees, m_frame);
    });
    for (QDoubleSpinBox* box : {m_x, m_y, m_z, m_rotX, m_rotY, m_scaleX, m_scaleY, m_angle, m_skew, m_opacity})
        connect(box, &QDoubleSpinBox::valueChanged, this, &LayerPanel::applyPose);
    for (QDoubleSpinBox* box : {m_pivotX, m_pivotY})
        connect(box, &QDoubleSpinBox::valueChanged, this, &LayerPanel::applyPivot);
    for (QDoubleSpinBox* box : {m_easeIn, m_easeOut})
        connect(box, &QDoubleSpinBox::valueChanged, this, &LayerPanel::applyEase);
    connect(m_card, &QPushButton::clicked, this, [this] {
        if (m_layer < 0 || m_layer >= m_document->scene().layers.size())
            return;
        QColor const chosen = QColorDialog::getColor(m_document->scene().layers[m_layer].cardColor, this, tr("Colour-Card"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            m_document->setCardColor(m_layer, chosen);
    });
    connect(m_length, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_refreshing)
            m_document->setBoneLength(m_layer, v);
    });
    connect(m_bone, &QComboBox::activated, this, [this](int index) {
        if (!m_refreshing)
            m_document->setParentBone(m_layer, index - 1);  // the first entry is "the layer only"
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_refreshing)
            m_document->renameLayer(m_layer, m_name->text());
        refresh();  // the name may have been refused or made unique
    });
    connect(m_document, &Document::changed, this, &LayerPanel::refresh);
    refresh();
}

void LayerPanel::setCurrentCell(int layer, int frame)
{
    m_layer = layer;
    m_frame = frame;
    m_effects->setCurrentCell(layer, frame);
    refresh();
}

void LayerPanel::refresh()
{
    Scene const& scene = m_document->scene();
    bool const valid = m_layer >= 0 && m_layer < scene.layers.size();
    setEnabled(valid);
    if (!valid)
        return;
    Layer const& layer = scene.layers[m_layer];
    Pose const pose = layer.poseAt(m_frame);
    m_refreshing = true;
    if (!m_name->hasFocus() || m_name->text().isEmpty())
        m_name->setText(layer.name);
    static char const* const kKinds[] = {QT_TR_NOOP("Drawing"), QT_TR_NOOP("Peg"), QT_TR_NOOP("Camera"), QT_TR_NOOP("Sound"),
                                         QT_TR_NOOP("Colour-Card"), QT_TR_NOOP("Group"), QT_TR_NOOP("Bone"), QT_TR_NOOP("Quadmap")};
    m_kind->setText(tr(kKinds[layer.kind]));
    m_form->setRowVisible(m_card, layer.kind == Layer::ColourCard);
    m_form->setRowVisible(m_length, layer.kind == Layer::Bone);
    m_form->setRowVisible(m_fov, layer.kind == Layer::Camera);
    m_fov->setValue(layer.fovAt(m_frame));
    // A camera is not turned in space (it looks straight at the scene).
    m_form->setRowVisible(m_turnRow, layer.kind != Layer::Camera);
    m_card->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px;").arg(layer.cardColor.name()));
    m_length->setValue(layer.boneLength);
    m_form->setRowVisible(m_ikRow, layer.kind == Layer::Bone);
    m_form->setRowVisible(m_artRow, layer.kind == Layer::DrawingLayer && !layer.showsSymbol());
    m_form->setRowVisible(m_bitmap, layer.kind == Layer::DrawingLayer && !layer.showsSymbol());
    m_bitmap->setChecked(layer.bitmap);
    {
        int const up = m_document->scene().parentIndex(m_layer);
        m_form->setRowVisible(m_bend, layer.holdsDrawings() && up >= 0 && m_document->scene().layers[up].kind == Layer::Bone);
        m_bend->setChecked(layer.bendOnBones);
    }
    for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
        m_artShown[sheet]->setChecked(layer.sheetShown(sheet));
    m_ikExcluded->setChecked(layer.ikExcluded);
    m_ikLimited->setChecked(layer.ikLimited);
    m_ikMin->setValue(layer.ikMin);
    m_ikMax->setValue(layer.ikMax);
    m_ikMin->setEnabled(layer.ikLimited);
    m_ikMax->setEnabled(layer.ikLimited);
    // Ease belongs to a keyframe: there is something to set only on one.
    int const key = layer.keyIndex(m_frame);
    m_easeIn->setEnabled(key >= 0);
    m_easeOut->setEnabled(key >= 0);
    m_easeIn->setValue(key >= 0 ? layer.keys[key].easeIn * 100.0 : 0.0);
    m_easeOut->setValue(key >= 0 ? layer.keys[key].easeOut * 100.0 : 0.0);
    int const parent = scene.parentIndex(m_layer);
    m_parent->setText(parent >= 0 ? scene.layers[parent].name : tr("-"));
    int const bones = parent >= 0 ? std::max(0, int(scene.layers[parent].bones.size()) - 1) : 0;
    m_bone->clear();
    m_bone->addItem(tr("The layer only"));
    for (int i = 0; i < bones; ++i)
        m_bone->addItem(tr("Bone %1").arg(i + 1));
    m_bone->setCurrentIndex(layer.parentBone >= 0 && layer.parentBone < bones ? layer.parentBone + 1 : 0);
    m_bone->setEnabled(bones > 0);
    m_x->setValue(pose.x);
    m_y->setValue(pose.y);
    m_z->setValue(pose.z);
    m_rotX->setValue(pose.rotX);
    m_rotY->setValue(pose.rotY);
    m_scaleX->setValue(pose.scaleX);
    m_scaleY->setValue(pose.scaleY);
    m_angle->setValue(pose.rotation);
    m_skew->setValue(pose.skew);
    m_opacity->setValue(pose.opacity * 100.0);
    m_pivotX->setValue(layer.pivot.x());
    m_pivotY->setValue(layer.pivot.y());
    m_key->setText(layer.keys.isEmpty() ? tr("No keyframes: the layer does not move.")
                   : layer.keyIndex(m_frame) >= 0 ? tr("Keyframe on this frame.") : tr("Between keyframes."));
    m_refreshing = false;
}

void LayerPanel::applyPose()
{
    Scene const& scene = m_document->scene();
    if (m_refreshing || m_layer < 0 || m_layer >= scene.layers.size())
        return;
    // A scale of nothing could not be dragged back with the transform tool.
    auto keep = [](double v) { return std::abs(v) < 0.001 ? (v < 0 ? -0.001 : 0.001) : v; };
    // Only the numbers that were changed are taken from the boxes: they round what they
    // show, and the others must not pick that rounding up.
    Pose const now = scene.layers[m_layer].poseAt(m_frame);
    Pose pose = now;
    auto take = [](double& field, double shown, double step) {
        if (std::abs(shown - field) > step)
            field = shown;
    };
    take(pose.x, m_x->value(), 0.006);
    take(pose.y, m_y->value(), 0.006);
    take(pose.z, m_z->value(), 0.006);
    take(pose.rotX, m_rotX->value(), 0.006);
    take(pose.rotY, m_rotY->value(), 0.006);
    take(pose.scaleX, keep(m_scaleX->value()), 0.0006);
    take(pose.scaleY, keep(m_scaleY->value()), 0.0006);
    take(pose.rotation, m_angle->value(), 0.006);
    take(pose.skew, m_skew->value(), 0.006);
    take(pose.opacity, m_opacity->value() / 100.0, 0.006);
    if (pose == now)
        return;
    if (m_animate)
        m_document->setKeyframe(m_layer, m_frame, pose, tr("Change Layer Properties"));
    else
        m_document->shiftPoses(m_layer, m_frame, now, pose, tr("Change Layer Properties"));
}

void LayerPanel::applyEase()
{
    if (!m_refreshing)
        m_document->setKeyEase(m_layer, m_frame, m_easeIn->value() / 100.0, m_easeOut->value() / 100.0);
}

void LayerPanel::applyPivot()
{
    if (m_refreshing || m_layer < 0 || m_layer >= m_document->scene().layers.size())
        return;
    m_document->setPivot(m_layer, QPointF(m_pivotX->value(), m_pivotY->value()));
}
