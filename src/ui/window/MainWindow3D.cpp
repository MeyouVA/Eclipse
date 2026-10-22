// Eclipse - the main window: the 3D submenu of the Animation menu, for layers that show 3D models (see
// model/geometry/Model3D.h) - their timing as a 3D animation, a flat snapshot of one, and their place brought up to date.
#include "ui/window/MainWindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QStatusBar>

#include <algorithm>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "model/geometry/Model3D.h"
#include "ui/timeline/Timeline.h"

namespace {

// The drawings of a layer that are models, in the order they are in the layer.
QVector<int> modelDrawings(Layer const& layer)
{
    QVector<int> found;
    for (int d = 0; d < layer.drawings.size(); ++d)
        if (!layer.drawings[d].model.isEmpty())
            found.push_back(d);
    return found;
}

}

bool MainWindow::refuseUnlessModel(QString const& what)
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l >= 0 && l < scene.layers.size() && !modelDrawings(scene.layers[l]).isEmpty())
        return false;
    QMessageBox::information(this, what, tr("The current layer shows no 3D model. Bring one in with File > Import > 3D Models."));
    return true;
}

void MainWindow::onActionSet3DAnimationTiming()
{
    if (refuseUnlessModel(tr("Set 3D Animation Timing")))
        return;
    int const count = int(modelDrawings(m_document->scene().layers[layer()]).size());
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("Set3DTimingDialog"));
    dialog.setWindowTitle(tr("Set 3D Animation Timing"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const start = new QSpinBox(&dialog);
    start->setObjectName(QStringLiteral("Set3DTimingStart"));
    start->setRange(1, 100000);
    start->setValue(frame() + 1);
    form->addRow(tr("Start at frame"), start);
    QSpinBox* const hold = new QSpinBox(&dialog);
    hold->setObjectName(QStringLiteral("Set3DTimingHold"));
    hold->setRange(1, 1000);
    hold->setValue(1);
    form->addRow(tr("Frames for each model"), hold);
    form->addRow(new QLabel(tr("%n model(s) in the layer, played one after the other.", nullptr, count), &dialog));
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    set3DAnimationTiming(layer(), start->value() - 1, hold->value());
}

void MainWindow::set3DAnimationTiming(int l, int first, int hold)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return;
    QVector<int> cells;
    for (int d : modelDrawings(scene.layers[l]))
        for (int k = 0; k < std::max(1, hold); ++k)
            cells.push_back(d);
    if (!cells.isEmpty())
        m_document->setCells(l, std::max(0, first), cells, tr("Set 3D Animation Timing"));
}

void MainWindow::onActionSnapshot3D()
{
    if (refuseUnlessModel(tr("Snapshot 3D")))
        return;
    Scene const& scene = m_document->scene();
    int const l = layer(), f = frame();
    Layer const& model = scene.layers[l];
    if (!model.modelAt(f)) {
        QMessageBox::information(this, tr("Snapshot 3D"), tr("The layer shows no 3D model on this frame."));
        return;
    }
    Pose const pose = scene.poseOf(l, f);
    Drawing flat = Deform::shown(model.drawings[model.drawingAt(f)], model, pose);
    flat.name = model.drawings[model.drawingAt(f)].name;
    QString const name = model.name + tr(" Snapshot");
    int const parent = model.parent;
    m_document->beginMacro(tr("Snapshot 3D"));
    int const at = m_document->importDrawings(-1, name, f, {flat}, 1);
    if (at >= 0) {
        if (parent != 0)
            m_document->setParent(at, m_document->scene().layerIndex(parent));
        Pose still = pose;
        still.rotX = still.rotY = 0.0;
        m_document->setKeyframe(at, f, still, tr("Snapshot 3D"));
    }
    m_document->endMacro();
    if (at >= 0) {
        m_timeline->setCurrentCell(at, f);
        statusBar()->showMessage(tr("Snapshot 3D: the model on frame %1, flat, on the layer %2.").arg(f + 1).arg(name), 8000);
    }
}

void MainWindow::onActionUpdate3DModelPosition()
{
    if (refuseUnlessModel(tr("Update 3D Model Position")))
        return;
    // The model files read again (they may have been changed elsewhere), and each model put back in the middle of the frame.
    Model3D::forget();
    Scene const& scene = m_document->scene();
    int const l = layer();
    QPointF const middle(scene.width / 2.0, scene.height / 2.0);
    QVector<int> const models = modelDrawings(scene.layers[l]);
    m_document->beginMacro(tr("Update 3D Model Position"));
    for (int d : models) {
        Layer const& now = m_document->scene().layers[l];
        if (now.drawings[d].modelCentre == middle)
            continue;
        int const shown = now.cells.indexOf(d);
        Drawing moved = now.drawings[d];
        moved.modelCentre = middle;
        if (shown >= 0)
            m_document->setDrawing(l, shown, moved, tr("Update 3D Model Position"));
    }
    m_document->endMacro();
    update();
}
