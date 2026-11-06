// Eclipse - the main window: constraints (points, spring, static, limit, gate, switch) and Transform Loop.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/views/ControllerView.h"
#include <QDockWidget>

void MainWindow::onActionBakeConstraint(QString const& which)
{
    // Bake Transformation onto Constraint: a Static Transformation holding what it hangs from now.
    bool const immediate = which == QLatin1String("parent");
    Constraint const baked = m_document->scene().bakedConstraint(layer(), frame(), immediate);
    m_document->setConstraint(layer(), baked, immediate ? tr("Bake Immediate Parent's Transformation") : tr("Bake Transformation onto Constraint"));
}

void MainWindow::onActionRemoveConstraint()
{
    if (m_document->scene().layers[layer()].constraint.kind == Constraint::None) {
        statusBar()->showMessage(tr("This layer has no constraint."), 4000);
        return;
    }
    m_document->setConstraint(layer(), Constraint(), tr("Remove Constraint"));
}

void MainWindow::onActionConstraintSettings()
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    Layer const& now = scene.layers[l];
    Constraint c = now.constraint;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ConstraintDialog"));
    dialog.setWindowTitle(tr("Constraint and Loop: %1").arg(now.name));
    auto number = [&](char const* name, double value, double low, double high, int decimals = 2) {
        QDoubleSpinBox* const box = new QDoubleSpinBox(&dialog);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setDecimals(decimals);
        box->setValue(value);
        return box;
    };
    QComboBox* const kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("ConstraintKind"));
    kind->addItems({tr("None"), tr("Points Constraint (two, three or more pegs)"), tr("Dynamic Spring"), tr("Static Transformation"),
                    tr("Transformation Limit"), tr("Transformation Gate"), tr("Transformation Switch")});
    kind->setCurrentIndex(c.kind);
    // Anchors: the other layers, ticked in order (Transformation 1, 2 ...).
    QListWidget* const anchors = new QListWidget(&dialog);
    anchors->setObjectName(QStringLiteral("ConstraintAnchors"));
    anchors->setMaximumHeight(120);
    for (int i = 0; i < scene.layers.size(); ++i) {
        if (i == l || scene.layers[i].kind == Layer::Sound)
            continue;
        QListWidgetItem* const item = new QListWidgetItem(scene.layers[i].name, anchors);
        item->setData(Qt::UserRole, scene.layers[i].id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        int const place = int(c.anchors.indexOf(scene.layers[i].id));
        item->setCheckState(place >= 0 ? Qt::Checked : Qt::Unchecked);
        if (place >= 0)
            item->setText(QStringLiteral("%1   (Transformation %2)").arg(scene.layers[i].name).arg(place + 1));
    }
    QDoubleSpinBox* const active = number("ConstraintActive", now.poseAt(frame()).constraint, -100, 100, 3);
    QCheckBox* const match = new QCheckBox(tr("Match Animation on Active Attribute"), &dialog);
    match->setObjectName(QStringLiteral("ConstraintMatch"));
    match->setChecked(c.matchSwitch);
    match->setToolTip(tr("Switch: where it switches, the new transformation starts from where the old one was (no jump)."));
    active->setToolTip(tr("Active Value, keyed on this frame: how much (Limit, Gate: 0..1), or which Transformation (Switch: 1, 2 ...)."));
    // Points.
    QDoubleSpinBox* const volume = number("ConstraintVolume", c.volume, 0, 5);
    QDoubleSpinBox* const volumeMin = number("ConstraintVolumeMin", c.volumeMin, 0.01, 100);
    QDoubleSpinBox* const volumeMax = number("ConstraintVolumeMax", c.volumeMax, 0.01, 100);
    QDoubleSpinBox* const distanceMin = number("ConstraintDistanceMin", c.distanceMin, 0, 100000, 1);
    QDoubleSpinBox* const distanceMax = number("ConstraintDistanceMax", c.distanceMax, 0, 100000, 1);
    QDoubleSpinBox* const balance = number("ConstraintBalance", c.balance, 0, 1);
    QCheckBox* const flip = new QCheckBox(tr("Allow Flipping"), &dialog);
    flip->setObjectName(QStringLiteral("ConstraintFlip"));
    flip->setChecked(c.allowFlip);
    QCheckBox* const flipX = new QCheckBox(tr("Allow Flip on X-Axis"), &dialog);
    flipX->setChecked(c.flipX);
    QCheckBox* const flipY = new QCheckBox(tr("Allow Flip on Y-Axis"), &dialog);
    flipY->setChecked(c.flipY);
    QComboBox* const skew = new QComboBox(&dialog);
    skew->setObjectName(QStringLiteral("ConstraintSkew"));
    skew->addItems({tr("Skew Optimized for Rotation"), tr("Skew Ignored in Rotation"), tr("Skew Applied as Rotation")});
    skew->setCurrentIndex(c.skewType);
    QGroupBox* const points = new QGroupBox(tr("Points Constraint"), &dialog);
    QFormLayout* const pointsForm = new QFormLayout(points);
    pointsForm->addRow(tr("Volume Modifier"), volume);
    pointsForm->addRow(tr("Volume Min"), volumeMin);
    pointsForm->addRow(tr("Volume Max"), volumeMax);
    pointsForm->addRow(tr("Distance Min (0: none)"), distanceMin);
    pointsForm->addRow(tr("Distance Max (0: none)"), distanceMax);
    pointsForm->addRow(tr("Point Balance"), balance);
    pointsForm->addRow(tr("Skew Type"), skew);
    QDoubleSpinBox* const skewMod = number("ConstraintSkewModifier", c.skewModifier, 0, 1);
    pointsForm->addRow(tr("Skew Modifier"), skewMod);
    pointsForm->addRow(flip);
    pointsForm->addRow(flipX);
    pointsForm->addRow(flipY);
    // Spring.
    QGroupBox* const spring = new QGroupBox(tr("Dynamic Spring"), &dialog);
    QFormLayout* const springForm = new QFormLayout(spring);
    QDoubleSpinBox* const ix = number("SpringInertiaX", c.inertiaX, 0, 0.99);
    QDoubleSpinBox* const iy = number("SpringInertiaY", c.inertiaY, 0, 0.99);
    QDoubleSpinBox* const ir = number("SpringInertiaRotate", c.inertiaRotate, 0, 0.99);
    QDoubleSpinBox* const is = number("SpringInertiaScale", c.inertiaScale, 0, 0.99);
    QDoubleSpinBox* const tx = number("SpringTensionX", c.tensionX, 0, 1);
    QDoubleSpinBox* const ty = number("SpringTensionY", c.tensionY, 0, 1);
    QDoubleSpinBox* const tr_ = number("SpringTensionRotate", c.tensionRotate, 0, 1);
    QDoubleSpinBox* const ts = number("SpringTensionScale", c.tensionScale, 0, 1);
    QDoubleSpinBox* const iskew = number("SpringInertiaSkew", c.inertiaSkew, 0, 0.99);
    QDoubleSpinBox* const tskew = number("SpringTensionSkew", c.tensionSkew, 0, 1);
    springForm->addRow(tr("Inertia X"), ix);
    springForm->addRow(tr("Inertia Y"), iy);
    springForm->addRow(tr("Inertia Rotate"), ir);
    springForm->addRow(tr("Inertia Scale"), is);
    springForm->addRow(tr("Tension X"), tx);
    springForm->addRow(tr("Tension Y"), ty);
    springForm->addRow(tr("Tension Rotate"), tr_);
    springForm->addRow(tr("Tension Scale"), ts);
    springForm->addRow(tr("Inertia Skew"), iskew);
    springForm->addRow(tr("Tension Skew"), tskew);
    // Static.
    QGroupBox* const statics = new QGroupBox(tr("Static Transformation"), &dialog);
    QFormLayout* const staticForm = new QFormLayout(statics);
    QDoubleSpinBox* const sx = number("StaticX", c.staticX, -100000, 100000, 2);
    QDoubleSpinBox* const sy = number("StaticY", c.staticY, -100000, 100000, 2);
    QDoubleSpinBox* const sa = number("StaticAngle", c.staticAngle, -3600, 3600, 2);
    QDoubleSpinBox* const ssx = number("StaticScaleX", c.staticScaleX, -100, 100, 3);
    QDoubleSpinBox* const ssy = number("StaticScaleY", c.staticScaleY, -100, 100, 3);
    QDoubleSpinBox* const ssk = number("StaticSkew", c.staticSkew, -89, 89, 2);
    QDoubleSpinBox* const sskY = number("StaticSkewY", c.staticSkewY, -89, 89, 2);
    QCheckBox* const uniform = new QCheckBox(tr("Uniform Scale"), &dialog);
    uniform->setObjectName(QStringLiteral("StaticUniform"));
    uniform->setChecked(c.staticUniform);
    staticForm->addRow(tr("Translate X"), sx);
    staticForm->addRow(tr("Translate Y"), sy);
    staticForm->addRow(tr("Rotation"), sa);
    staticForm->addRow(tr("Scale X"), ssx);
    staticForm->addRow(tr("Scale Y"), ssy);
    staticForm->addRow(tr("Skew X"), ssk);
    staticForm->addRow(tr("Skew Y"), sskY);
    staticForm->addRow(uniform);
    // Limit.
    QGroupBox* const limit = new QGroupBox(tr("Transformation Limit"), &dialog);
    QFormLayout* const limitForm = new QFormLayout(limit);
    QCheckBox* const lt = new QCheckBox(tr("Translate"), &dialog);
    lt->setChecked(c.limitTranslate);
    QCheckBox* const lr = new QCheckBox(tr("Rotate"), &dialog);
    lr->setChecked(c.limitRotate);
    QCheckBox* const ls = new QCheckBox(tr("Scale"), &dialog);
    ls->setChecked(c.limitScale);
    QCheckBox* const lk = new QCheckBox(tr("Skew"), &dialog);
    lk->setChecked(c.limitSkew);
    QCheckBox* const invert = new QCheckBox(tr("Invert Transformation"), &dialog);
    invert->setObjectName(QStringLiteral("ConstraintInvert"));
    invert->setChecked(c.invert);
    QHBoxLayout* const parts = new QHBoxLayout;
    for (QCheckBox* box : {lt, lr, ls, lk})
        parts->addWidget(box);
    limitForm->addRow(tr("Limited Transform"), parts);
    limitForm->addRow(invert);
    // Loop.
    QGroupBox* const loop = new QGroupBox(tr("Transform Loop"), &dialog);
    QFormLayout* const loopForm = new QFormLayout(loop);
    QComboBox* const loopType = new QComboBox(&dialog);
    loopType->setObjectName(QStringLiteral("LoopType"));
    loopType->addItems({tr("No Loop"), tr("Repeat"), tr("Ping-Pong")});
    loopType->setCurrentIndex(now.loopType);
    QCheckBox* const automatic = new QCheckBox(tr("Automatic Range Detection"), &dialog);
    automatic->setObjectName(QStringLiteral("LoopAutomatic"));
    automatic->setChecked(now.loopStart < 0 || now.loopEnd < 0);
    QSpinBox* const loopStart = new QSpinBox(&dialog);
    loopStart->setObjectName(QStringLiteral("LoopStart"));
    loopStart->setRange(1, 100000);
    loopStart->setValue(std::max(1, now.loopStart + 1));
    QSpinBox* const loopEnd = new QSpinBox(&dialog);
    loopEnd->setObjectName(QStringLiteral("LoopEnd"));
    loopEnd->setRange(1, 100000);
    loopEnd->setValue(std::max(1, now.loopEnd + 1));
    loopForm->addRow(tr("Loop Type"), loopType);
    loopForm->addRow(automatic);
    loopForm->addRow(tr("From frame"), loopStart);
    loopForm->addRow(tr("To frame"), loopEnd);
    auto enable = [&] {
        int const k = kind->currentIndex();
        anchors->setEnabled(k == Constraint::Points || k == Constraint::Gate || k == Constraint::Switch);
        active->setEnabled(k == Constraint::Limit || k == Constraint::Gate || k == Constraint::Switch);
        points->setVisible(k == Constraint::Points);
        spring->setVisible(k == Constraint::Spring);
        statics->setVisible(k == Constraint::Static);
        limit->setVisible(k == Constraint::Limit);
        loopStart->setEnabled(!automatic->isChecked());
        loopEnd->setEnabled(!automatic->isChecked());
    };
    enable();
    connect(kind, &QComboBox::currentIndexChanged, &dialog, [&] { enable(); });
    connect(automatic, &QCheckBox::toggled, &dialog, [&] { enable(); });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QWidget* const page = new QWidget;
    QFormLayout* const form = new QFormLayout(page);
    form->addRow(tr("Constraint"), kind);
    form->addRow(tr("Anchors / transformations (ticked)"), anchors);
    form->addRow(tr("Active Value"), active);
    form->addRow(match);
    form->addRow(points);
    form->addRow(spring);
    form->addRow(statics);
    form->addRow(limit);
    form->addRow(loop);
    QScrollArea* const scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
    dialog.resize(560, 700);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Constraint set = c;
    set.kind = kind->currentIndex();
    // The anchors in their order: those kept where they were, then the newly ticked.
    QVector<int> ticked;
    for (int i = 0; i < anchors->count(); ++i)
        if (anchors->item(i)->checkState() == Qt::Checked)
            ticked.push_back(anchors->item(i)->data(Qt::UserRole).toInt());
    QVector<int> ordered;
    for (int id : c.anchors)
        if (ticked.contains(id))
            ordered.push_back(id);
    for (int id : ticked)
        if (!ordered.contains(id))
            ordered.push_back(id);
    set.anchors = ordered;
    // A Points constraint takes its rest from where its anchors are now, when they change.
    if (set.kind == Constraint::Points && (set.anchors != c.anchors || c.rest.size() != set.anchors.size() || c.kind != Constraint::Points))
        set.rest = scene.anchorPlaces(set.anchors, frame());
    set.volume = volume->value();
    set.volumeMin = volumeMin->value();
    set.volumeMax = volumeMax->value();
    set.distanceMin = distanceMin->value();
    set.distanceMax = distanceMax->value();
    set.balance = balance->value();
    set.skewType = skew->currentIndex();
    set.allowFlip = flip->isChecked();
    set.flipX = flipX->isChecked();
    set.flipY = flipY->isChecked();
    set.inertiaX = ix->value();
    set.inertiaY = iy->value();
    set.inertiaRotate = ir->value();
    set.inertiaScale = is->value();
    set.tensionX = tx->value();
    set.tensionY = ty->value();
    set.tensionRotate = tr_->value();
    set.tensionScale = ts->value();
    set.staticX = sx->value();
    set.staticY = sy->value();
    set.staticAngle = sa->value();
    set.staticScaleX = ssx->value();
    set.staticScaleY = ssy->value();
    set.staticSkew = ssk->value();
    set.limitTranslate = lt->isChecked();
    set.limitRotate = lr->isChecked();
    set.limitScale = ls->isChecked();
    set.limitSkew = lk->isChecked();
    set.invert = invert->isChecked();
    set.inertiaSkew = iskew->value();
    set.tensionSkew = tskew->value();
    set.skewModifier = skewMod->value();
    set.staticSkewY = sskY->value();
    set.staticUniform = uniform->isChecked();
    set.matchSwitch = match->isChecked();
    QString const names[] = {tr("Remove Constraint"), tr("Points Constraint"), tr("Dynamic Spring"), tr("Static Transform"), tr("Change TransformLimit Module"),
                             tr("Transformation Gate"), tr("Change Switch Module")};
    m_document->beginMacro(tr("Constraint"));
    m_document->setConstraint(l, set, names[std::clamp(set.kind, 0, 6)]);
    if (active->isEnabled() && std::abs(active->value() - now.poseAt(frame()).constraint) > 1e-9)
        m_document->setConstraintValue(l, frame(), active->value());
    m_document->setLoop(l, loopType->currentIndex(), automatic->isChecked() ? -1 : loopStart->value() - 1, automatic->isChecked() ? -1 : loopEnd->value() - 1);
    m_document->endMacro();
}

void MainWindow::onActionControllerCommand(QString const& which)
{
    ControllerView* const view = findChild<ControllerView*>();
    int const controller = view ? view->currentController() : -1;
    if (which == QLatin1String("setup")) {
        if (QDockWidget* const dock = findChild<QDockWidget*>(QStringLiteral("ControllerDock")))
            showView(dock);
        return;
    }
    if (which == QLatin1String("master") && view) {
        if (QDockWidget* const dock = findChild<QDockWidget*>(QStringLiteral("ControllerDock")))
            showView(dock);
        view->newMasterController();
        return;
    }
    if (which == QLatin1String("script") && view && controller >= 0) {
        view->editScript();
        return;
    }
    if (which == QLatin1String("all")) {
        int const baked = m_document->bakeAllControllers();
        statusBar()->showMessage(baked > 0 ? tr("Bake All Controllers: %n frame(s) keyed.", nullptr, baked) : tr("No Controller Nodes available."), 5000);
        return;
    }
    if (which == QLatin1String("clean")) {
        int const cleaned = m_document->cleanControllers();
        statusBar()->showMessage(cleaned > 0 ? tr("Clean Controllers: %n thing(s) taken away.", nullptr, cleaned) : tr("The controllers are clean."), 5000);
        return;
    }
    if (controller < 0) {
        statusBar()->showMessage(tr("No Controller Nodes available: choose a controller in the Controllers view."), 6000);
        return;
    }
    if (which == QLatin1String("apply")) {
        // Apply to Controller: the poses the layers have here made a position of the controller.
        m_document->addControllerState(controller, frame(), QString());
    } else if (which == QLatin1String("bake")) {
        int const baked = m_document->bakeController(controller, 0, m_document->scene().frameCount() - 1);
        statusBar()->showMessage(baked > 0 ? tr("Bake from Controllers: %n frame(s) keyed.", nullptr, baked) : tr("No Controller positions found in provided range."), 5000);
    } else if (which == QLatin1String("timing") || which == QLatin1String("drawings") || which == QLatin1String("timingDrawings")) {
        // Create Controller Timing / Drawings: the states' drawings made, and the layers' exposures set as the controller goes.
        m_document->beginMacro(tr("Setting Drawing Controller"));
        int const drawings = which != QLatin1String("timing") ? m_document->createControllerDrawings(controller) : 0;
        int const frames = which != QLatin1String("drawings") ? m_document->createControllerTiming(controller, 0, m_document->scene().frameCount() - 1) : 0;
        m_document->endMacro();
        statusBar()->showMessage(tr("Setting Drawing Controller: %1 drawing(s) made, %2 frame(s) timed.").arg(drawings).arg(frames), 5000);
    } else if (which == QLatin1String("positions")) {
        // Bake Controller Positions: the controller's own value keyed on every frame of its range.
        Scene::Controller options = m_document->scene().controllers[controller];
        int const first = options.point ? (options.pointKeys.isEmpty() ? 0 : options.pointKeys.firstKey()) : (options.keys.isEmpty() ? 0 : options.keys.firstKey());
        int const last = options.point ? (options.pointKeys.isEmpty() ? 0 : options.pointKeys.lastKey()) : (options.keys.isEmpty() ? 0 : options.keys.lastKey());
        m_document->beginMacro(tr("Bake Control Position(s)"));
        for (int f = first; f <= last; ++f) {
            Scene::Controller const& c = m_document->scene().controllers[controller];
            if (c.point)
                m_document->applyControllerPoint(controller, f, c.whereAt(f));
            else
                m_document->applyController(controller, f, c.valueAt(f));
        }
        m_document->endMacro();
    }
}
