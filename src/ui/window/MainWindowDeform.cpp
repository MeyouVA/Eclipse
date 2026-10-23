// Eclipse - the main window: morphing and deformer commands.
#include "ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/timeline/Timeline.h"

#include "ui/window/MainWindowPrivate.h"
#include "ui/window/MorphVelocityGraph.h"
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
    Scene const& scene = m_document->scene();
    Layer const& now = scene.layers[layer()];
    if (now.kind == Layer::Peg) {
        // On a peg: one envelope round all that hangs from it, which reshapes all of it.
        if (!now.warp.isEmpty()) {
            statusBar()->showMessage(tr("This peg has a deformer already. Remove it first (Animation > Deformation > Remove Deformer)."), 7000);
            return;
        }
        Drawing all;
        QTransform const there = scene.worldTransform(layer(), frame());
        for (int l = 0; l < scene.layers.size() && there.isInvertible(); ++l) {
            bool under = false;
            for (int up = scene.parentIndex(l), guard = 0; up >= 0 && guard < 256 && !under; up = scene.parentIndex(up), ++guard)
                under = up == layer();
            int const index = scene.layers[l].holdsDrawings() ? scene.layers[l].drawingAt(frame()) : Layer::kNoDrawing;
            if (!under || index == Layer::kNoDrawing)
                continue;
            // Into the peg's own coordinates.
            QTransform const into = scene.worldTransform(l, frame()) * there.inverted();
            Drawing const& d = scene.layers[l].drawings[index];
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Fill fill : d.fillsOf(sheet)) {
                    fill.transform(into);
                    all.fills.push_back(fill);
                }
                for (Stroke stroke : d.strokesOf(sheet)) {
                    stroke.transform(into);
                    all.strokes.push_back(stroke);
                }
            }
        }
        // (Round all of it - the hull - however many pieces there are, never one piece's outline.)
        QVector<QPointF> const points = Deform::envelopeAround(all, m_envelopePoints, 12.0, false);
        if (points.isEmpty()) {
            statusBar()->showMessage(tr("Nothing drawn hangs from this peg on this frame to put an envelope round."), 6000);
            return;
        }
        m_document->setWarp(layer(), points, true, tr("Create Envelope"));
        statusBar()->showMessage(tr("An envelope of %1 points round all that hangs from the peg. Pull them about with the Transform tool.").arg(points.size() / 3), 6000);
        return;
    }
    if (!now.holdsDrawings() || now.drawingAt(frame()) == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("An envelope goes round a drawing, or round all that hangs from a peg: select a cell that shows one, or a peg."), 6000);
        return;
    }
    if (!now.warp.isEmpty()) {
        statusBar()->showMessage(tr("This layer has a deformer already. Remove it first (Animation > Deformation > Remove Deformer)."), 7000);
        return;
    }
    QVector<QPointF> const points = Deform::envelopeAround(now.drawings[now.drawingAt(frame())], m_envelopePoints, 12.0, m_envelopeOutline);
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
        statusBar()->showMessage(tr("This layer has no curve or envelope deformer and no animated points."), 5000);
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
    // The velocity curve: how far along the change is as its time goes by, drawn and pulled about.
    MorphVelocityGraph* const graph = new MorphVelocityGraph(now.morphs[index], &dialog);
    auto easesFollow = [graph, in, out] {
        bool const free = graph->points().isEmpty();
        in->setEnabled(free);
        out->setEnabled(free);
    };
    graph->changed = [graph, easesFollow] {
        easesFollow();
        graph->update();
    };
    for (QSpinBox* box : {in, out})
        connect(box, &QSpinBox::valueChanged, graph, [graph, in, out] { graph->setEases(in->value() / 100.0, out->value() / 100.0); });
    easesFollow();
    QPushButton* const straighten = new QPushButton(tr("Clear the Curve"), &dialog);
    straighten->setObjectName(QStringLiteral("MorphVelocityClear"));
    straighten->setToolTip(tr("Takes out the points of the curve: the eases above say how it goes again."));
    connect(straighten, &QPushButton::clicked, graph, [graph] { graph->setPoints({}); });
    form->addRow(new QLabel(tr("Velocity: click to add a point, drag it, double-click it to take it out."), &dialog));
    form->addRow(graph);
    form->addRow(straighten);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // (One step to undo, and none when nothing changed.)
    Morph const was = m_document->scene().layers[layer()].morphs[index];
    bool const eased = was.easeIn != in->value() / 100.0 || was.easeOut != out->value() / 100.0, curved = was.velocity != graph->points();
    if (eased && curved)
        m_document->beginMacro(tr("Morphing Ease"));
    m_document->setMorphEase(layer(), frame(), in->value() / 100.0, out->value() / 100.0);
    m_document->setMorphVelocity(layer(), frame(), graph->points());
    if (eased && curved)
        m_document->endMacro();
}
