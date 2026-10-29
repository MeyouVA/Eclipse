// Eclipse - camera clipping distances in Layer Properties.
#include "ui/panels/LayerPanel.h"

#include "model/document/Commands.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>

void LayerPanel::buildCameraOptions()
{
    m_clipRow = new QWidget(this);
    QHBoxLayout* const row = new QHBoxLayout(m_clipRow);
    row->setContentsMargins(0, 0, 0, 0);
    m_nearPlane = number(0.0, 9999999.0, 1.0, 3);
    m_farPlane = number(0.0, 9999999.0, 1.0, 3);
    m_nearPlane->setObjectName(QStringLiteral("CameraNearPlane"));
    m_farPlane->setObjectName(QStringLiteral("CameraFarPlane"));
    m_nearPlane->setSpecialValueText(tr("Automatic"));
    m_farPlane->setSpecialValueText(tr("Unlimited"));
    m_nearPlane->setToolTip(tr("Distance from the camera's eye to the nearest visible plane, in scene units. Zero uses the automatic near limit."));
    m_farPlane->setToolTip(tr("Distance from the camera's eye to the furthest visible plane, in scene units. Zero leaves the far distance unlimited."));
    row->addWidget(new QLabel(tr("Near"), m_clipRow));
    row->addWidget(m_nearPlane, 1);
    row->addWidget(new QLabel(tr("Far"), m_clipRow));
    row->addWidget(m_farPlane, 1);
    int index = 0;
    QFormLayout::ItemRole role;
    m_form->getLayoutPosition(m_fovRow, &index, &role);
    m_form->insertRow(index + 1, tr("Clipping Planes"), m_clipRow);
    auto changed = [this] {
        if (m_refreshing)
            return;
        double const near = m_nearPlane->value();
        double const far = m_farPlane->value() > 0.0 ? std::max(m_farPlane->value(), near + 0.001) : 0.0;
        m_document->editLayers({m_layer}, tr("Change Camera Clipping"), [near, far](Layer& layer) {
            layer.nearPlane = near;
            layer.farPlane = far;
        });
    };
    connect(m_nearPlane, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_farPlane, &QDoubleSpinBox::valueChanged, this, changed);
    m_cameraAvailable = new QCheckBox(tr("Camera Available"), this);
    m_cameraAvailable->setObjectName(QStringLiteral("CameraAvailable"));
    m_cameraAvailable->setToolTip(tr("Off, the scene is never seen through this camera: Scene > Camera passes it by and the next available camera is used."));
    m_form->insertRow(index + 2, QString(), m_cameraAvailable);
    connect(m_cameraAvailable, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing)
            m_document->editLayers({m_layer}, on ? tr("Make Camera Available") : tr("Make Camera Unavailable"), [on](Layer& layer) { layer.cameraAvailable = on; });
    });
}

void LayerPanel::refreshCameraOptions(Layer const& layer)
{
    m_form->setRowVisible(m_clipRow, layer.kind == Layer::Camera);
    m_form->setRowVisible(m_cameraAvailable, layer.kind == Layer::Camera);
    m_cameraAvailable->setChecked(layer.cameraAvailable);
    m_nearPlane->setValue(layer.nearPlane);
    m_farPlane->setValue(layer.farPlane);
}
