#include "ui/views/PerspectiveView.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <cmath>

bool PerspectiveView::setClippingPlanes(double nearDistance, double farDistance)
{
    if (!std::isfinite(nearDistance) || !std::isfinite(farDistance) || nearDistance < 0.01
        || farDistance > 100000.0 || farDistance - nearDistance < .01 - 1e-9)
        return false;
    m_nearDistance = nearDistance;
    m_farDistance = farDistance;
    update();
    return true;
}

void PerspectiveView::onActionClippingPlanes()
{
    double const previousNear = m_nearDistance, previousFar = m_farDistance;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PerspectiveClippingDialog"));
    dialog.setWindowTitle(tr("Perspective Clipping Planes"));
    QFormLayout form(&dialog);
    QLabel explanation(tr("Distances from the Perspective viewpoint, in scene units."), &dialog);
    form.addRow(&explanation);
    QDoubleSpinBox near(&dialog), far(&dialog);
    near.setObjectName(QStringLiteral("PerspectiveNearDistance"));
    far.setObjectName(QStringLiteral("PerspectiveFarDistance"));
    near.setDecimals(2); far.setDecimals(2);
    near.setRange(.01, previousFar - .01); far.setRange(previousNear + .01, 100000);
    near.setValue(previousNear); far.setValue(previousFar);
    auto apply = [&] { setClippingPlanes(near.value(), far.value()); };
    connect(&near, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { far.setMinimum(value + .01); apply(); });
    connect(&far, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) { near.setMaximum(value - .01); apply(); });
    form.addRow(tr("Near distance"), &near);
    form.addRow(tr("Far distance"), &far);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons.button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        far.setValue(100000); near.setValue(1);
    });
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted)
        setClippingPlanes(previousNear, previousFar);
}
