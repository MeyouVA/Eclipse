// Eclipse - the main window: point deformers - a lattice (Free Form Deformer), a mesh warp, weighted and rigid points.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QStatusBar>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"

namespace {

// The region a new lattice or mesh covers: the drawing shown, a little larger (the frame when there is none).
QRectF regionFor(Scene const& scene, int layer, int frame)
{
    Layer const& l = scene.layers[layer];
    int const index = l.drawingAt(frame);
    QRectF box = index != Layer::kNoDrawing && index < l.drawings.size() ? l.drawings[index].bounds() : QRectF();
    if (box.isEmpty())
        box = QRectF(0, 0, scene.width, scene.height);
    double const margin = std::max(box.width(), box.height()) * 0.05 + 4.0;
    return box.adjusted(-margin, -margin, margin, margin);
}

}  // namespace

bool MainWindow::createGridDeformer(int l, int kind, int cols, int rows)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings()) {
        statusBar()->showMessage(tr("A lattice or mesh goes on a drawing layer: select one."), 6000);
        return false;
    }
    PointDeformer const d = Deform::gridDeformer(kind, regionFor(scene, l, frame()), cols, rows);
    m_document->setPointDeformer(l, d, kind == PointDeformer::Mesh ? tr("Create Mesh Warp") : tr("Create Free Form Deformer"));
    statusBar()->showMessage(tr("Pull the points of the grid with the Transform tool: the drawing follows."), 6000);
    return true;
}

void MainWindow::onActionCreateLattice(QString const& which)
{
    int const kind = which == QLatin1String("mesh") ? PointDeformer::Mesh : PointDeformer::Lattice;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("GridDeformerDialog"));
    dialog.setWindowTitle(kind == PointDeformer::Mesh ? tr("Create Mesh Warp") : tr("Create Free Form Deformer"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const cols = new QSpinBox(&dialog);
    cols->setObjectName(QStringLiteral("GridColumns"));
    cols->setRange(1, 64);
    cols->setValue(kind == PointDeformer::Mesh ? 3 : 2);
    QSpinBox* const rows = new QSpinBox(&dialog);
    rows->setObjectName(QStringLiteral("GridRows"));
    rows->setRange(1, 64);
    rows->setValue(kind == PointDeformer::Mesh ? 3 : 2);
    form->addRow(tr("Columns"), cols);
    form->addRow(tr("Rows"), rows);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted)
        createGridDeformer(layer(), kind, cols->value(), rows->value());
}

void MainWindow::onActionRemovePointDeformer()
{
    if (!m_document->scene().layers[layer()].pointDeformer.any()) {
        statusBar()->showMessage(tr("This layer has no lattice, mesh or weighted points."), 5000);
        return;
    }
    m_document->setPointDeformer(layer(), PointDeformer(), tr("Remove Point Deformer"));
}

void MainWindow::onActionPointDeformerSettings()
{
    Layer const& now = m_document->scene().layers[layer()];
    PointDeformer d = now.pointDeformer;
    if (!d.any()) {
        statusBar()->showMessage(tr("This layer has no point deformer: make a lattice or mesh (Rigging > Deformation), or place points (Rigging tool, Points)."), 7000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("PointDeformerDialog"));
    dialog.setWindowTitle(tr("Point Deformer Settings: %1").arg(now.name));
    QFormLayout* const form = new QFormLayout(&dialog);
    bool const grid = d.kind == PointDeformer::Lattice || d.kind == PointDeformer::Mesh;
    QComboBox* const kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("PointDeformerKind"));
    if (grid) {
        kind->addItem(tr("Free Form Deformer (lattice)"), int(PointDeformer::Lattice));
        kind->addItem(tr("Mesh Warp (smooth)"), int(PointDeformer::Mesh));
    } else {
        kind->addItem(tr("Weighted Deform"), int(PointDeformer::Weighted));
        kind->addItem(tr("Rigid Point Deform"), int(PointDeformer::Rigid));
    }
    kind->setCurrentIndex(std::max(0, kind->findData(d.kind)));
    form->addRow(tr("Deformer"), kind);
    auto number = [&](char const* name, double value, double low, double high, int decimals) {
        QDoubleSpinBox* const box = new QDoubleSpinBox(&dialog);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setValue(value);
        return box;
    };
    // A grid: its region of interest (changing it lays the grid down again there).
    QDoubleSpinBox* const left = number("RoiLeft", d.area.left(), -100000, 100000, 1);
    QDoubleSpinBox* const top = number("RoiTop", d.area.top(), -100000, 100000, 1);
    QDoubleSpinBox* const width = number("RoiWidth", d.area.width(), 1, 200000, 1);
    QDoubleSpinBox* const height = number("RoiHeight", d.area.height(), 1, 200000, 1);
    // Weighted: how the points pull.
    QComboBox* const curve = new QComboBox(&dialog);
    curve->setObjectName(QStringLiteral("PointDeformerCurveType"));
    curve->addItems({tr("Straight Lines"), tr("Catmull-Rom Spline"), tr("Cubic Bezier Curve")});
    curve->setCurrentIndex(d.curveType);
    QDoubleSpinBox* const minimum = number("PointMinDistance", d.minDistance, 0, 100000, 1);
    QDoubleSpinBox* const maximum = number("PointMaxDistance", d.maxDistance, 0, 100000, 1);
    QDoubleSpinBox* const blend = number("PointBlendDistance", d.blendDistance, 0.01, 100000, 1);
    QDoubleSpinBox* const pre = number("PointPreBlend", d.preBlend, 0.1, 20, 2);
    QDoubleSpinBox* const post = number("PointPostBlend", d.postBlend, 0, 1, 2);
    QSpinBox* const consider = new QSpinBox(&dialog);
    consider->setObjectName(QStringLiteral("PointsToConsider"));
    consider->setRange(0, 1000);
    consider->setSpecialValueText(tr("All"));
    consider->setValue(d.pointsToConsider);
    QCheckBox* const individual = new QCheckBox(tr("Allow Individual Transformation"), &dialog);
    individual->setObjectName(QStringLiteral("PointIndividual"));
    individual->setChecked(d.individual);
    if (grid) {
        form->addRow(tr("Region of interest: left"), left);
        form->addRow(tr("top"), top);
        form->addRow(tr("width"), width);
        form->addRow(tr("height"), height);
    } else {
        form->addRow(tr("Curve Type"), curve);
        form->addRow(tr("Minimum Distance"), minimum);
        form->addRow(tr("Maximum Distance (0: none)"), maximum);
        form->addRow(tr("Blend Distance"), blend);
        form->addRow(tr("Pre-Blend Amount"), pre);
        form->addRow(tr("Post-Blend Amount"), post);
        form->addRow(tr("Points to Consider"), consider);
    }
    form->addRow(individual);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    int const chosen = kind->currentData().toInt();
    QRectF const area(left->value(), top->value(), width->value(), height->value());
    if (grid && area != d.area) {
        // Bezier Mesh modify the region of interest: the grid laid down again over the new region.
        PointDeformer again = Deform::gridDeformer(chosen, area, d.cols, d.rows);
        again.individual = individual->isChecked();
        m_document->setPointDeformer(layer(), again, tr("Bezier Mesh modify the region of interest"));
        return;
    }
    d.kind = chosen;
    d.curveType = curve->currentIndex();
    d.minDistance = minimum->value();
    d.maxDistance = maximum->value();
    d.blendDistance = blend->value();
    d.preBlend = pre->value();
    d.postBlend = post->value();
    d.pointsToConsider = consider->value();
    d.individual = individual->isChecked();
    m_document->setPointDeformer(layer(), d, tr("Point Deformer Settings"));
}
