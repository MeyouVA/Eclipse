// Eclipse - the main window: morphing and deformer commands.
#include "animation/ui/window/MainWindow.h"
#include "model/geometry/Deform.h"

#include <QDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "animation/ui/timeline/Timeline.h"

#include "animation/ui/window/MainWindowPrivate.h"
#include "animation/ui/window/MorphVelocityGraph.h"
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

void MainWindow::onActionCopyPoseToRest()
{
    if (!m_document->copyPoseToRest(layer(), frame()))
        statusBar()->showMessage(tr("The deformer is at rest on this frame: there is no other position to make its rest."), 6000);
}

void MainWindow::onActionCopyRestToPose()
{
    if (!m_document->copyRestToPose(layer(), frame()))
        statusBar()->showMessage(tr("This layer has no bones or curve deformer."), 5000);
}

void MainWindow::onActionToggleDeformation()
{
    // Deformation Switch: on and off from this frame (keyed), as the layer's Enable Deformation.
    Layer const& now = m_document->scene().layers[layer()];
    if (now.bones.size() < 2 && !now.hasWarp()) {
        statusBar()->showMessage(tr("This layer has no bones or curve deformer."), 5000);
        return;
    }
    bool const on = now.poseAt(frame()).deform > 0.0;
    m_document->setDeformEnabled(layer(), frame(), on ? 0.0 : 1.0);
    statusBar()->showMessage(on ? tr("Deformation off from frame %1.").arg(frame() + 1) : tr("Deformation on from frame %1.").arg(frame() + 1), 4000);
}

void MainWindow::onActionAddKinematicOutput()
{
    Layer const& now = m_document->scene().layers[layer()];
    if (now.bones.size() < 2) {
        statusBar()->showMessage(tr("A Kinematic Output follows a bone: select a layer with bones (Rigging tool)."), 6000);
        return;
    }
    int bone = int(now.bones.size()) - 2;
    if (now.bones.size() > 2 && qEnvironmentVariableIsEmpty("ECLIPSE_NO_SYMBOL_QUESTIONS")) {
        bool ok = false;
        bone = QInputDialog::getInt(this, tr("Kinematic Output"), tr("Output Kinematic Chain: the bone to follow (1 is the first):"), bone + 1, 1,
                                    int(now.bones.size()) - 1, 1, &ok) - 1;
        if (!ok)
            return;
    }
    int const made = m_document->addKinematicOutput(layer(), bone);
    if (made >= 0) {
        m_timeline->setCurrentCell(made, frame());
        statusBar()->showMessage(tr("Kinematic Output: what is put under %1 follows bone %2.").arg(m_document->scene().layers[made].name).arg(bone + 1), 6000);
    }
}

void MainWindow::onActionDeformationSettings()
{
    editDeformationSettings(layer(), std::max(0, m_canvas->currentBone()));
}

void MainWindow::onActionConvertZoneToShape()
{
    // Convert Elliptic ROI to Shape: the chosen bone's ellipse of influence made a shape that can then be changed.
    Layer const& now = m_document->scene().layers[layer()];
    int const bone = std::max(0, m_canvas->currentBone());
    if (bone + 1 >= now.bones.size() || now.influenceOf(bone).type != BoneInfluence::Elliptic) {
        statusBar()->showMessage(tr("Choose a bone whose zone of influence is Elliptic (Deformation Settings)."), 6000);
        return;
    }
    QPolygonF const shape = Deform::zoneShape(now.bones, bone, now.influenceOf(bone));
    m_document->editLayers({layer()}, tr("Convert Elliptic ROI to Shape"), [bone, shape](Layer& l) {
        if (l.influences.size() <= bone)
            l.influences.resize(bone + 1);
        l.influences[bone].type = BoneInfluence::Shaped;
        l.influences[bone].shape = shape;
    });
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
    bool const bent = Deform::isBent(now.bones, pose.bend);
    if (!Deform::isWarped(now.warp, now.warpClosed, pose.warp) && !bent) {
        statusBar()->showMessage(now.hasWarp() || now.bones.size() >= 2 ? tr("The deformer is at rest on this frame already.") : tr("This layer has no curve or envelope deformer."), 5000);
        return;
    }
    pose.warp.clear();
    pose.bend.fill(0.0);
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

void MainWindow::onActionSetTransformation()
{
    QString error;
    if (m_document->setDrawingTransformation(layer(), frame(), true, &error) < 0)
        statusBar()->showMessage(error, 7000);
    else
        statusBar()->showMessage(tr("This drawing has a deformation of its own now: change its bones with the Rigging tool."), 6000);
}

void MainWindow::onActionDefaultTransformation()
{
    QString error;
    if (m_document->setDrawingTransformation(layer(), frame(), false, &error) < 0)
        statusBar()->showMessage(error, 7000);
}

void MainWindow::onActionConvertAddChain()
{
    QString error;
    if (m_document->convertToNewDrawingWithChain(layer(), frame(), &error) < 0)
        statusBar()->showMessage(error, 7000);
}

void MainWindow::onActionTransformationNames()
{
    // Transformation Names: the drawings of the layer with a deformation of their own (named as the drawings); renamed, or
    // given back the layer's.
    Layer const& now = m_document->scene().layers[layer()];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("TransformationNamesDialog"));
    dialog.setWindowTitle(tr("Transformation Names: %1").arg(now.name));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("TransformationList"));
    auto fill = [&] {
        list->clear();
        Layer const& l = m_document->scene().layers[layer()];
        for (int d = 0; d < l.drawings.size(); ++d)
            if (l.drawings[d].rig.size() >= 2) {
                QListWidgetItem* const item = new QListWidgetItem(l.drawings[d].name.isEmpty() ? QString::number(d + 1) : l.drawings[d].name, list);
                item->setData(Qt::UserRole, d);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
            }
    };
    fill();
    connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* item) {
        int const d = item->data(Qt::UserRole).toInt();
        QString const name = item->text().trimmed();
        Layer const& l = m_document->scene().layers[layer()];
        bool taken = false;
        for (int other = 0; other < l.drawings.size(); ++other)
            taken = taken || (other != d && l.drawings[other].name == name);
        if (!name.isEmpty() && !taken)
            m_document->editLayers({layer()}, tr("Rename Transformations"), [d, name](Layer& edited) {
                if (d < edited.drawings.size())
                    edited.drawings[d].name = name;
            });
    });
    QPushButton* const defaults = new QPushButton(tr("Default Transformation"), &dialog);
    defaults->setObjectName(QStringLiteral("TransformationDefault"));
    connect(defaults, &QPushButton::clicked, &dialog, [&] {
        if (QListWidgetItem* const item = list->currentItem()) {
            int const d = item->data(Qt::UserRole).toInt();
            m_document->editLayers({layer()}, tr("Default Transformation"), [d](Layer& l) {
                if (d < l.drawings.size())
                    l.drawings[d].rig.clear();
            });
            QSignalBlocker const quiet(list);
            fill();
        }
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Drawings with a deformation of their own (double-click to rename):"), &dialog));
    layout->addWidget(list, 1);
    layout->addWidget(defaults);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::onActionIkConstraint(QString const& which)
{
    if (which == QLatin1String("clearKeys")) {
        int const cleared = m_document->clearIkKeyframes(layer(), frame());
        statusBar()->showMessage(cleared > 0 ? tr("IK Clear All Affected Keyframes: %n keyframe(s).", nullptr, cleared)
                                             : tr("No keyframe of the IK chain on this frame."), 5000);
        return;
    }
    QVector<int> layers = m_timeline->selectedLayers();
    if (which == QLatin1String("resetAll")) {
        layers.clear();
        for (int l = 0; l < m_document->scene().layers.size(); ++l)
            layers.push_back(l);
    }
    if (layers.isEmpty() && layer() >= 0)
        layers = {layer()};
    if (layers.isEmpty()) {
        statusBar()->showMessage(tr("No Joint Selected"), 4000);
        return;
    }
    m_document->setIkConstraint(layers, which, frame());
}

void MainWindow::onActionSetIkKeyframe()
{
    m_ikKeyframe = frame();
    statusBar()->showMessage(tr("IK Keyframe: frame %1. Apply IK Constraints keeps the held parts where they are on it.").arg(frame() + 1), 6000);
}

void MainWindow::onActionApplyIkConstraints()
{
    // From the IK Keyframe to the current frame (or, before it, from the current frame to it).
    int const from = std::min(m_ikKeyframe, frame()), to = std::max(m_ikKeyframe, frame());
    if (to <= from) {
        statusBar()->showMessage(tr("Set the IK Keyframe on another frame first (Rigging > IK Constraints > Set IK Keyframe)."), 6000);
        return;
    }
    int const fixed = m_document->applyIkConstraints(from, to);
    statusBar()->showMessage(fixed > 0 ? tr("IK constraints applied to %n part(s), frames %1 to %2.", nullptr, fixed).arg(from + 2).arg(to + 1)
                                       : tr("No part is nailed or holds X or Y (Rigging > IK Constraints)."), 6000);
}
