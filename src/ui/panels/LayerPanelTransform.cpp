// Eclipse - the Layer Properties view: how a layer may be animated (separate or locked values, angle limits, pivots).
#include "ui/panels/LayerPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "model/document/Commands.h"

void LayerPanel::buildTransformOptions()
{
    m_transformRow = new QWidget(this);
    m_transformRow->setObjectName(QStringLiteral("LayerTransformOptions"));
    QVBoxLayout* const column = new QVBoxLayout(m_transformRow);
    column->setContentsMargins(0, 0, 0, 0);
    auto check = [this, column](char const* name, QString const& text, QString const& tip) {
        QCheckBox* const box = new QCheckBox(text, m_transformRow);
        box->setObjectName(QLatin1String(name));
        box->setToolTip(tip);
        column->addWidget(box);
        return box;
    };
    m_separatePosition = check("LayerSeparatePosition", tr("Separate Position"),
                               tr("X, Y and Z each go their own way between keyframes. Off, the layer follows a path that control points can bend."));
    m_separateScale = check("LayerSeparateScale", tr("Separate Scale"),
                            tr("Width and height are scaled each on its own. Off (locked), they keep in proportion, and the layer cannot be skewed."));
    m_ignoreParentScale = check("LayerIgnoreParentScale", tr("Ignore Parent Scaling"),
                                tr("The layer moves and turns with what it is attached to, but is not scaled with it."));
    m_animatable = check("LayerAnimatable", tr("Animate Using Animation Tools"),
                         tr("Off, the Transform tool passes this layer by and moves the peg it is attached to instead."));
    QHBoxLayout* const angles = new QHBoxLayout;
    m_angleLimited = new QCheckBox(tr("Enable Min/Max Angle"), m_transformRow);
    m_angleLimited->setObjectName(QStringLiteral("LayerAngleLimited"));
    m_angleLimited->setToolTip(tr("The layer turns no further than these two angles."));
    m_minAngle = number(-3600.0, 3600.0, 1.0, 2);
    m_minAngle->setObjectName(QStringLiteral("LayerMinAngle"));
    m_minAngle->setToolTip(tr("Min Angle"));
    m_maxAngle = number(-3600.0, 3600.0, 1.0, 2);
    m_maxAngle->setObjectName(QStringLiteral("LayerMaxAngle"));
    m_maxAngle->setToolTip(tr("Max Angle"));
    angles->addWidget(m_angleLimited);
    angles->addWidget(m_minAngle, 1);
    angles->addWidget(new QLabel(tr("to"), m_transformRow));
    angles->addWidget(m_maxAngle, 1);
    column->addLayout(angles);
    m_embeddedPivot = new QComboBox(m_transformRow);
    m_embeddedPivot->setObjectName(QStringLiteral("LayerEmbeddedPivot"));
    m_embeddedPivot->addItems({tr("Don't Use Embedded Pivot"), tr("Apply Embedded Pivot on Drawing Layer"), tr("Apply Embedded Pivot on Parent Peg")});
    m_embeddedPivot->setToolTip(tr("Drawing Pivot: whether the pivots set on the drawings (Drawing Pivot tool) are used, by this layer or by the peg it is attached to."));
    column->addWidget(m_embeddedPivot);
    m_form->insertRow(m_form->rowCount() - 1, tr("Transformation"), m_transformRow);

    auto flag = [this](QCheckBox* box, QString const& on, QString const& off, void (*set)(Layer&, bool)) {
        connect(box, &QCheckBox::toggled, this, [this, on, off, set](bool checked) {
            if (!m_refreshing)
                m_document->editLayers({m_layer}, checked ? on : off, [set, checked](Layer& l) { set(l, checked); });
        });
    };
    flag(m_separatePosition, tr("Separate Position"), tr("Path Position"), [](Layer& l, bool b) {
        l.separatePosition = b;
        if (b)
            for (Keyframe& k : l.keys)
                k.controls.clear(), k.controlsZ.clear();    // (no path: nothing to bend it)
    });
    flag(m_separateScale, tr("Separate Scale"), tr("Locked Scale"), [](Layer& l, bool b) {
        l.separateScale = b;
        if (!b)
            for (Keyframe& k : l.keys)
                k.pose.scaleY = k.pose.scaleX, k.pose.skew = 0.0;
    });
    flag(m_ignoreParentScale, tr("Ignore Parent Scaling"), tr("Follow Parent Scaling"), [](Layer& l, bool b) { l.ignoreParentScale = b; });
    flag(m_animatable, tr("Animate Using Animation Tools"), tr("Don't Animate Using Animation Tools"), [](Layer& l, bool b) { l.animatable = b; });
    auto limits = [this] {
        if (m_refreshing)
            return;
        bool const on = m_angleLimited->isChecked();
        double const low = m_minAngle->value(), high = m_maxAngle->value();
        m_document->editLayers({m_layer}, on ? tr("Enable Peg Min/Max Angle") : tr("Change Peg Min Angle Value"), [on, low, high](Layer& l) {
            l.angleLimited = on;
            l.minAngle = low;
            l.maxAngle = high;
        });
    };
    connect(m_angleLimited, &QCheckBox::toggled, this, limits);
    connect(m_minAngle, &QDoubleSpinBox::valueChanged, this, limits);
    connect(m_maxAngle, &QDoubleSpinBox::valueChanged, this, limits);
    connect(m_embeddedPivot, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_refreshing)
            m_document->editLayers({m_layer}, tr("Change Drawing Pivot"), [index](Layer& l) { l.embeddedPivot = index; });
    });
}

void LayerPanel::refreshTransformOptions(Layer const& layer)
{
    bool const moves = layer.kind == Layer::DrawingLayer || layer.kind == Layer::Peg || layer.kind == Layer::Bone || layer.kind == Layer::Quadmap;
    m_form->setRowVisible(m_transformRow, moves);
    m_separatePosition->setChecked(layer.separatePosition);
    m_separateScale->setChecked(layer.separateScale);
    m_ignoreParentScale->setChecked(layer.ignoreParentScale);
    m_animatable->setChecked(layer.animatable);
    m_animatable->setVisible(layer.holdsDrawings());
    m_angleLimited->setChecked(layer.angleLimited);
    m_minAngle->setValue(layer.minAngle);
    m_maxAngle->setValue(layer.maxAngle);
    m_minAngle->setEnabled(layer.angleLimited);
    m_maxAngle->setEnabled(layer.angleLimited);
    m_embeddedPivot->setCurrentIndex(std::clamp(layer.embeddedPivot, 0, 2));
    // (A peg takes a drawing's pivot from a layer attached to it, as that layer says.)
    m_embeddedPivot->setVisible(layer.holdsDrawings());
    m_scaleY->setEnabled(layer.separateScale && !m_scaleY->property("driven").toBool());
    if (m_skew)
        m_skew->setEnabled(layer.separateScale && !m_skew->property("driven").toBool());
}
