// Eclipse - the main window: morphing and deformer commands.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/timeline/Timeline.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

void MainWindow::onActionCreateMorphing()
{
    Layer const& now = m_document->scene().layers[layer()];
    if (!now.holdsDrawings()) {
        statusBar()->showMessage(tr("Morphing is between two drawings: select a cell of a drawing layer."), 6000);
        return;
    }
    if (now.morphIndexAt(frame()) >= 0) {
        statusBar()->showMessage(tr("There is a morphing here already."), 5000);
        return;
    }
    if (!m_document->createMorph(layer(), frame()))
        statusBar()->showMessage(tr("Morphing needs a drawing here, another drawing later on the layer, and at least one frame between the two."), 8000);
}

void MainWindow::onActionRemoveDeformer()
{
    if (m_document->scene().layers[layer()].warp.isEmpty()) {
        statusBar()->showMessage(tr("This layer has no curve or envelope deformer."), 5000);
        return;
    }
    m_document->setWarp(layer(), {}, false, tr("Remove Deformer"));
}

void MainWindow::onActionCreateEnvelope()
{
    Layer const& now = m_document->scene().layers[layer()];
    if (!now.holdsDrawings() || now.drawingAt(frame()) == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("An envelope goes round a drawing: select a cell that shows one."), 6000);
        return;
    }
    if (!now.warp.isEmpty()) {
        statusBar()->showMessage(tr("This layer has a deformer already. Remove it first (Animation > Deformation > Remove Deformer)."), 7000);
        return;
    }
    QVector<QPointF> const points = Deform::envelopeAround(now.drawings[now.drawingAt(frame())], m_envelopePoints);
    if (points.isEmpty()) {
        statusBar()->showMessage(tr("There is too little in this drawing to put an envelope round."), 6000);
        return;
    }
    m_document->setWarp(layer(), points, true, tr("Create Envelope"));
    statusBar()->showMessage(tr("An envelope of %1 points. Pull them about with the Transform tool.").arg(points.size() / 3), 6000);
}

void MainWindow::onActionConvertDeformed()
{
    if (!m_document->convertDeformed(layer()))
        statusBar()->showMessage(tr("This layer has no curve or envelope deformer."), 5000);
}

void MainWindow::onActionResetDeformer()
{
    // Back to the shape it was laid down in, on this frame.
    Layer const& now = m_document->scene().layers[layer()];
    Pose pose = now.poseAt(frame());
    if (!Deform::isWarped(now.warp, now.warpClosed, pose.warp)) {
        statusBar()->showMessage(now.hasWarp() ? tr("The deformer is at rest on this frame already.") : tr("This layer has no curve or envelope deformer."), 5000);
        return;
    }
    pose.warp.clear();
    m_document->setKeyframe(layer(), frame(), pose, tr("Reset Deformer"));
}

void MainWindow::onActionDeleteMorphing()
{
    if (!m_document->deleteMorph(layer(), frame()))
        statusBar()->showMessage(tr("There is no morphing on this frame."), 5000);
}

void MainWindow::onActionConvertMorphing()
{
    if (!m_document->convertMorph(layer(), frame()))
        statusBar()->showMessage(tr("There is no morphing on this frame."), 5000);
}

void MainWindow::onActionMorphingEase()
{
    Layer const& now = m_document->scene().layers[layer()];
    int const index = now.morphIndexAt(frame());
    if (index < 0) {
        statusBar()->showMessage(tr("There is no morphing on this frame."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Morphing Ease"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const out = new QSpinBox(&dialog);
    out->setObjectName(QStringLiteral("MorphEaseOut"));
    out->setRange(0, 100);
    out->setSuffix(QStringLiteral(" %"));
    out->setValue(int(now.morphs[index].easeOut * 100.0 + 0.5));
    QSpinBox* const in = new QSpinBox(&dialog);
    in->setObjectName(QStringLiteral("MorphEaseIn"));
    in->setRange(0, 100);
    in->setSuffix(QStringLiteral(" %"));
    in->setValue(int(now.morphs[index].easeIn * 100.0 + 0.5));
    form->addRow(tr("Leaving the first drawing"), out);
    form->addRow(tr("Arriving at the second"), in);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted)
        m_document->setMorphEase(layer(), frame(), in->value() / 100.0, out->value() / 100.0);
}
