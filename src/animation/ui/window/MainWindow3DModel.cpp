// Eclipse - the main window: a 3D model's own settings (3D Model Settings: its turn, scaling, the camera it is seen
// through, its light and toon shading), Fit Model to View, and Render 3D Model (the model drawn on a bitmap layer).
#include "animation/ui/window/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/geometry/Deform.h"
#include "model/geometry/Model3D.h"
#include "animation/ui/timeline/Timeline.h"

namespace {

void paintSwatch(QPushButton* button, QColor const& colour)
{
    button->setProperty("colour", colour);
    button->setText(colour.name(QColor::HexRgb));
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; }").arg(colour.name(QColor::HexRgb), colour.lightness() > 128 ? "black" : "white"));
}

}

bool MainWindow::modelSettings(int l, int f, ModelSettings& out) const
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return false;
    Layer const& layer = scene.layers[l];
    int const d = layer.drawingAt(f);
    if (d < 0 || d >= layer.drawings.size() || layer.drawings[d].model.isEmpty())
        return false;
    Drawing const& drawing = layer.drawings[d];
    out.centre = drawing.modelCentre;
    out.scale = drawing.modelScale;
    out.look = drawing.modelLook;
    return true;
}

bool MainWindow::setModelSettings(int l, int f, ModelSettings const& settings, bool allInLayer)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return false;
    Layer const& layer = scene.layers[l];
    int const shown = layer.drawingAt(f);
    QVector<int> drawings;
    for (int d = 0; d < layer.drawings.size(); ++d)
        if (!layer.drawings[d].model.isEmpty() && (allInLayer || d == shown))
            drawings.push_back(d);
    if (drawings.isEmpty())
        return false;
    double const scale = std::clamp(settings.scale, 0.01, 100.0);
    m_document->editScene(tr("3D Model Settings"), [&](Scene& s) {
        for (int d : drawings) {
            Drawing& drawing = s.layers[l].drawings[d];
            drawing.modelCentre = settings.centre;
            drawing.modelScale = scale;
            // (Each keeps its own parts hidden or moved.)
            QVector<Model3D::NodeOverride> const parts = d == shown ? settings.look.overrides : drawing.modelLook.overrides;
            drawing.modelLook = settings.look;
            drawing.modelLook.overrides = parts;
            drawing.modelLook.tidy();
        }
    });
    return true;
}

void MainWindow::fitModelToView(int l)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return;
    // In the middle of the frame, as big as the frame is at its narrowest (with a little room round it), facing the camera.
    QPointF const middle(scene.width / 2.0, scene.height / 2.0);
    double const scale = std::clamp(0.9 * std::min(scene.width, scene.height) / Model3D::kSize, 0.01, 100.0);
    m_document->editScene(tr("Fit Model to View"), [&](Scene& s) {
        for (Drawing& drawing : s.layers[l].drawings) {
            if (drawing.model.isEmpty())
                continue;
            drawing.modelCentre = middle;
            drawing.modelScale = scale;
            drawing.modelLook.rotX = drawing.modelLook.rotY = drawing.modelLook.rotZ = 0.0;
            drawing.modelLook.stretch = QVector3D(1, 1, 1);
            drawing.modelLook.truck = 0.0;
        }
    });
}

void MainWindow::onActionFitModelToView()
{
    if (refuseUnlessModel(tr("Fit Model to View")))
        return;
    fitModelToView(layer());
}

int MainWindow::render3DModelToBitmap(int l, QString* error)
{
    Scene const& scene = m_document->scene();
    bool any = false;
    if (l >= 0 && l < scene.layers.size())
        for (Drawing const& d : scene.layers[l].drawings)
            any = any || !d.model.isEmpty();
    if (!any) {
        if (error)
            *error = tr("No 3D model selected.");
        return -1;
    }
    Layer const& model = scene.layers[l];
    // Each frame's picture of the model, as it is turned and lit there (the same picture kept once).
    QVector<Drawing> drawings;
    QVector<int> cells(scene.frameCount(), Layer::kNoDrawing);
    QHash<QString, int> made;
    for (int f = 0; f < scene.frameCount(); ++f) {
        int const d = model.drawingAt(f);
        if (d < 0 || d >= model.drawings.size())
            continue;
        Pose const pose = scene.poseOf(l, f);
        Drawing const& source = model.drawings[d];
        QString const key = source.model.isEmpty() ? QString::number(d) : QStringLiteral("%1|%2|%3").arg(d).arg(pose.rotX, 0, 'g', 12).arg(pose.rotY, 0, 'g', 12);
        if (auto const found = made.constFind(key); found != made.cend()) {
            cells[f] = *found;
            continue;
        }
        Drawing flat = source.model.isEmpty() ? source : Deform::shown(source, model, pose);
        // (A model's pictures numbered in the order they come.)
        flat.name = source.model.isEmpty() ? source.name : QString::number(drawings.size() + 1);
        made.insert(key, int(drawings.size()));
        cells[f] = int(drawings.size());
        drawings.push_back(flat);
    }
    if (drawings.isEmpty()) {
        if (error)
            *error = tr("The layer shows no 3D model on any frame.");
        return -1;
    }
    int const modelId = model.id;
    QString const name = model.name + tr(" Render");
    m_document->beginMacro(tr("Render 3D model to bitmap layer"));
    int const at = m_document->duplicateLayer(l);
    if (at >= 0) {
        int const copyId = m_document->scene().layers[at].id;
        m_document->editScene(tr("Render 3D model to bitmap layer"), [&](Scene& s) {
            int const c = s.layerIndex(copyId), m = s.layerIndex(modelId);
            if (c < 0)
                return;
            Layer& copy = s.layers[c];
            copy.drawings = drawings;
            copy.cells = cells;
            copy.name = name;
            // (The pictures are already turned: the layer is not turned in space again.)
            copy.enable3d = false;
            if (m >= 0)
                s.layers[m].visible = false;
        });
    }
    m_document->endMacro();
    if (at < 0 && error)
        *error = tr("The layer could not be copied.");
    return at < 0 ? -1 : m_document->scene().layerIndex(m_document->scene().layers[at].id);
}

void MainWindow::onActionRender3DModelToBitmap()
{
    if (refuseUnlessModel(tr("Render 3D Model")))
        return;
    QString error;
    statusBar()->showMessage(tr("Rendering 3D model"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const at = render3DModelToBitmap(layer(), &error);
    QApplication::restoreOverrideCursor();
    if (at < 0) {
        QMessageBox::information(this, tr("Render 3D Model"), error);
        return;
    }
    m_timeline->setCurrentCell(at, frame());
    statusBar()->showMessage(tr("Render 3D model to bitmap layer: %1").arg(m_document->scene().layers[at].name), 8000);
}

void MainWindow::onActionModel3DSettings()
{
    if (refuseUnlessModel(tr("3D Model Settings")))
        return;
    int const l = layer(), f = frame();
    ModelSettings start;
    if (!modelSettings(l, f, start)) {
        QMessageBox::information(this, tr("3D Model Settings"), tr("The layer shows no 3D model on this frame."));
        return;
    }
    Scene const& scene = m_document->scene();
    Layer const& layerNow = scene.layers[l];
    std::shared_ptr<Model3D::Mesh const> const mesh = Model3D::load(layerNow.drawings[layerNow.drawingAt(f)].model);
    Pose const pose = scene.poseOf(l, f);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("Model3DDialog"));
    dialog.setWindowTitle(tr("3D Model Settings"));
    QHBoxLayout* const columns = new QHBoxLayout(&dialog);
    QLabel* const preview = new QLabel(&dialog);
    preview->setObjectName(QStringLiteral("Model3DPreview"));
    preview->setFixedSize(256, 256);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("QLabel { background: #808080; }"));
    columns->addWidget(preview, 0, Qt::AlignTop);
    QVBoxLayout* const right = new QVBoxLayout;
    columns->addLayout(right);

    auto number = [&](QString const& id, double low, double high, double value, int decimals, QString const& suffix = QString()) {
        QDoubleSpinBox* const box = new QDoubleSpinBox(&dialog);
        box->setObjectName(id);
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setValue(value);
        box->setSuffix(suffix);
        return box;
    };
    // ---- Transformation ----
    QGroupBox* const transformation = new QGroupBox(tr("Transformation"), &dialog);
    QFormLayout* const tf = new QFormLayout(transformation);
    QDoubleSpinBox* const moveX = number(QStringLiteral("Model3DTranslateX"), -100000, 100000, start.centre.x(), 2);
    QDoubleSpinBox* const moveY = number(QStringLiteral("Model3DTranslateY"), -100000, 100000, start.centre.y(), 2);
    QHBoxLayout* const translation = new QHBoxLayout;
    translation->addWidget(moveX);
    translation->addWidget(moveY);
    tf->addRow(tr("Translation"), translation);
    QDoubleSpinBox* const turnX = number(QStringLiteral("Model3DRotateX"), -3600, 3600, start.look.rotX, 2, tr("°"));
    QDoubleSpinBox* const turnY = number(QStringLiteral("Model3DRotateY"), -3600, 3600, start.look.rotY, 2, tr("°"));
    QDoubleSpinBox* const turnZ = number(QStringLiteral("Model3DRotateZ"), -3600, 3600, start.look.rotZ, 2, tr("°"));
    QHBoxLayout* const rotation = new QHBoxLayout;
    rotation->addWidget(turnX);
    rotation->addWidget(turnY);
    rotation->addWidget(turnZ);
    tf->addRow(tr("Rotation"), rotation);
    QDoubleSpinBox* const scale = number(QStringLiteral("Model3DScale"), 0.01, 100, start.scale, 3);
    tf->addRow(tr("Scaling"), scale);
    QCheckBox* const lock = new QCheckBox(tr("Lock Scaling Axis"), &dialog);
    lock->setObjectName(QStringLiteral("Model3DLockScale"));
    lock->setToolTip(tr("Scaled the same along each of the model's axes; off, along each by a scale of its own."));
    lock->setChecked(start.look.stretch == QVector3D(1, 1, 1));
    tf->addRow(QString(), lock);
    QDoubleSpinBox* const stretch[3] = {number(QStringLiteral("Model3DStretchX"), 0.01, 100, start.look.stretch.x(), 3),
                                        number(QStringLiteral("Model3DStretchY"), 0.01, 100, start.look.stretch.y(), 3),
                                        number(QStringLiteral("Model3DStretchZ"), 0.01, 100, start.look.stretch.z(), 3)};
    QHBoxLayout* const stretches = new QHBoxLayout;
    for (QDoubleSpinBox* box : stretch)
        stretches->addWidget(box);
    tf->addRow(tr("Along X, Y, Z"), stretches);
    right->addWidget(transformation);
    // ---- Camera ----
    QGroupBox* const camera = new QGroupBox(tr("Camera"), &dialog);
    QFormLayout* const cf = new QFormLayout(camera);
    QDoubleSpinBox* const focal = number(QStringLiteral("Model3DFocal"), 0, 1000, start.look.focal, 1, tr(" mm"));
    focal->setSpecialValueText(tr("No perspective"));
    focal->setToolTip(tr("Focal Length: the model seen in perspective, as through a lens so long (shorter: more so)."));
    cf->addRow(tr("Focal Length"), focal);
    QDoubleSpinBox* const truck = number(QStringLiteral("Model3DTruck"), -50, 50, start.look.truck, 2);
    truck->setToolTip(tr("Camera Truck In/Out: the camera so many model sizes nearer (less than 0: further off). Only with a focal length."));
    cf->addRow(tr("Camera Truck In/Out"), truck);
    right->addWidget(camera);
    // ---- Light ----
    Model3D::Shading const& s0 = start.look.shading;
    QGroupBox* const light = new QGroupBox(tr("Light"), &dialog);
    QFormLayout* const lf = new QFormLayout(light);
    QDoubleSpinBox* const azimuth = number(QStringLiteral("Model3DLightAzimuth"), -180, 180, s0.azimuth, 2, tr("°"));
    azimuth->setToolTip(tr("Light Source Position: round from straight ahead (less than 0: from the left)."));
    lf->addRow(tr("Light Source Position"), azimuth);
    QDoubleSpinBox* const elevation = number(QStringLiteral("Model3DLightElevation"), -90, 90, s0.elevation, 2, tr("°"));
    lf->addRow(tr("Light Source Elevation"), elevation);
    QPushButton* const lightColour = new QPushButton(&dialog);
    lightColour->setObjectName(QStringLiteral("Model3DLightColour"));
    paintSwatch(lightColour, s0.light);
    lf->addRow(tr("Light Colour"), lightColour);
    QDoubleSpinBox* const intensity = number(QStringLiteral("Model3DIntensity"), 0, 10, s0.intensity, 2);
    lf->addRow(tr("Adjust Light Intensity"), intensity);
    QDoubleSpinBox* const ambient = number(QStringLiteral("Model3DAmbient"), 0, 1, s0.ambient, 2);
    ambient->setToolTip(tr("How lit the parts the light does not reach still are."));
    lf->addRow(tr("Ambient"), ambient);
    QDoubleSpinBox* const specular = number(QStringLiteral("Model3DSpecular"), 0, 1, s0.specular, 2);
    lf->addRow(tr("Specular Strength"), specular);
    QDoubleSpinBox* const shininess = number(QStringLiteral("Model3DShininess"), 1, 512, s0.shininess, 1);
    shininess->setToolTip(tr("How small and sharp the shine is."));
    lf->addRow(tr("Shininess"), shininess);
    QCheckBox* const smooth = new QCheckBox(tr("Generate Normals"), &dialog);
    smooth->setObjectName(QStringLiteral("Model3DSmooth"));
    smooth->setToolTip(tr("Shaded smoothly over the corners, rather than face by face."));
    smooth->setChecked(s0.smooth);
    lf->addRow(QString(), smooth);
    right->addWidget(light);
    // ---- Toon shader ----
    QGroupBox* const toon = new QGroupBox(tr("Toon Shader"), &dialog);
    QFormLayout* const of = new QFormLayout(toon);
    QSpinBox* const bands = new QSpinBox(&dialog);
    bands->setObjectName(QStringLiteral("Model3DToonBands"));
    bands->setRange(0, 16);
    bands->setSpecialValueText(tr("Smooth"));
    bands->setValue(s0.toonBands);
    bands->setToolTip(tr("The light in so many flat steps, as a cel is painted."));
    of->addRow(tr("Tones"), bands);
    QDoubleSpinBox* const lineRadius = number(QStringLiteral("Model3DLineRadius"), 0, 32, s0.lineRadius, 1, tr(" px"));
    lineRadius->setToolTip(tr("An outline round the model and where it folds over itself, so wide (0: none)."));
    of->addRow(tr("Line Radius"), lineRadius);
    QPushButton* const lineColour = new QPushButton(&dialog);
    lineColour->setObjectName(QStringLiteral("Model3DLineColour"));
    paintSwatch(lineColour, s0.lineColour);
    of->addRow(tr("Line Colour"), lineColour);
    QCheckBox* const grey = new QCheckBox(tr("Grayscale"), &dialog);
    grey->setObjectName(QStringLiteral("Model3DGrey"));
    grey->setChecked(s0.grey);
    of->addRow(QString(), grey);
    right->addWidget(toon);

    QCheckBox* const all = new QCheckBox(tr("All the models of the layer"), &dialog);
    all->setObjectName(QStringLiteral("Model3DAll"));
    all->setChecked(true);
    right->addWidget(all);
    QHBoxLayout* const actions = new QHBoxLayout;
    QPushButton* const fit = new QPushButton(tr("Fit Model to View"), &dialog);
    fit->setObjectName(QStringLiteral("Model3DFit"));
    QPushButton* const render = new QPushButton(tr("Render 3D Model"), &dialog);
    render->setObjectName(QStringLiteral("Model3DRender"));
    render->setToolTip(tr("Render 3D model to bitmap layer: these settings kept, and the model drawn frame by frame on a layer of its own."));
    actions->addWidget(fit);
    actions->addWidget(render);
    right->addLayout(actions);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    right->addWidget(buttons);

    auto chosen = [&]() {
        ModelSettings out = start;
        out.centre = QPointF(moveX->value(), moveY->value());
        out.scale = scale->value();
        out.look.rotX = turnX->value();
        out.look.rotY = turnY->value();
        out.look.rotZ = turnZ->value();
        out.look.stretch = lock->isChecked() ? QVector3D(1, 1, 1) : QVector3D(float(stretch[0]->value()), float(stretch[1]->value()), float(stretch[2]->value()));
        out.look.focal = focal->value();
        out.look.truck = truck->value();
        Model3D::Shading& s = out.look.shading;
        s.azimuth = azimuth->value();
        s.elevation = elevation->value();
        // (Left where they start, the light comes from where it always has.)
        if (std::abs(s.azimuth - Model3D::Shading().azimuth) < 0.005)
            s.azimuth = Model3D::Shading().azimuth;
        if (std::abs(s.elevation - Model3D::Shading().elevation) < 0.005)
            s.elevation = Model3D::Shading().elevation;
        s.light = lightColour->property("colour").value<QColor>();
        s.intensity = intensity->value();
        s.ambient = ambient->value();
        s.specular = specular->value();
        s.shininess = shininess->value();
        s.smooth = smooth->isChecked();
        s.toonBands = bands->value() == 1 ? 2 : bands->value();
        s.lineRadius = lineRadius->value();
        s.lineColour = lineColour->property("colour").value<QColor>();
        s.grey = grey->isChecked();
        return out;
    };
    auto refresh = [&]() {
        for (QDoubleSpinBox* box : stretch)
            box->setEnabled(!lock->isChecked());
        truck->setEnabled(focal->value() > 0.0);
        if (!mesh)
            return;
        Model3D::Look look = chosen().look;
        look.rotX += pose.rotX;
        look.rotY += pose.rotY;
        preview->setPixmap(QPixmap::fromImage(Model3D::render(*mesh, look, 256, nullptr)));
    };
    for (QDoubleSpinBox* box : dialog.findChildren<QDoubleSpinBox*>())
        connect(box, &QDoubleSpinBox::valueChanged, &dialog, refresh);
    for (QCheckBox* box : {lock, smooth, grey})
        connect(box, &QCheckBox::toggled, &dialog, refresh);
    connect(bands, &QSpinBox::valueChanged, &dialog, refresh);
    for (QPushButton* button : {lightColour, lineColour})
        connect(button, &QPushButton::clicked, &dialog, [&, button] {
            QColor const picked = QColorDialog::getColor(button->property("colour").value<QColor>(), &dialog, tr("Colour"));
            if (picked.isValid()) {
                paintSwatch(button, picked);
                refresh();
            }
        });
    connect(fit, &QPushButton::clicked, &dialog, [&] {
        Scene const& sc = m_document->scene();
        moveX->setValue(sc.width / 2.0);
        moveY->setValue(sc.height / 2.0);
        scale->setValue(0.9 * std::min(sc.width, sc.height) / Model3D::kSize);
        turnX->setValue(0.0);
        turnY->setValue(0.0);
        turnZ->setValue(0.0);
        truck->setValue(0.0);
        lock->setChecked(true);
    });
    bool renderAfter = false;
    connect(render, &QPushButton::clicked, &dialog, [&] {
        renderAfter = true;
        dialog.accept();
    });
    refresh();
    if (dialog.exec() != QDialog::Accepted)
        return;
    setModelSettings(l, f, chosen(), all->isChecked());
    if (renderAfter)
        onActionRender3DModelToBitmap();
}
