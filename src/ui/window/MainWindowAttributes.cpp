// Eclipse - the window's attribute commands: bindings removed, reassigned and cleaned, published attributes cleared,
// attributes described, and properties set on many layers at once.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "model/document/Commands.h"
#include "ui/panels/LayerPanel.h"
#include "ui/timeline/Timeline.h"

namespace {

// A binding, said in words: "Angle follows Arm: Angle x 2 + 0".
QString bindingText(Scene const& scene, Driver const& d)
{
    int const source = scene.layerIndex(d.source);
    return MainWindow::tr("%1 follows %2: %3").arg(LayerPanel::valueName(d.value), source >= 0 ? scene.layers[source].name : MainWindow::tr("(layer gone)"),
                                                   LayerPanel::valueName(d.sourceValue))
           + (d.factor != 1.0 || d.offset != 0.0 ? QStringLiteral(" x %1 + %2").arg(d.factor).arg(d.offset) : QString());
}

}  // namespace

QVector<int> MainWindow::attributeLayers() const
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty() && layer() >= 0 && layer() < m_document->scene().layers.size())
        layers = {layer()};
    return layers;
}

void MainWindow::onActionRemoveAttributeBinding()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drivers.isEmpty()) {
        statusBar()->showMessage(tr("The current layer has no attribute bindings."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("RemoveBindingDialog"));
    dialog.setWindowTitle(tr("Remove Attribute Binding"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const which = new QComboBox(&dialog);
    which->setObjectName(QStringLiteral("RemoveBindingWhich"));
    for (Driver const& d : scene.layers[layer()].drivers)
        which->addItem(bindingText(scene, d));
    form->addRow(tr("Remove Binding from Attribute:"), which);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    int const index = which->currentIndex();
    m_document->editLayers({layer()}, tr("Remove Attribute Binding"), [index](Layer& l) {
        if (index >= 0 && index < l.drivers.size())
            l.drivers.removeAt(index);
    });
}

void MainWindow::onActionRemoveAllAttributeBindings()
{
    QVector<int> layers;
    for (int l : attributeLayers())
        if (!m_document->scene().layers[l].drivers.isEmpty())
            layers << l;
    if (layers.isEmpty()) {
        statusBar()->showMessage(tr("The layers selected have no attribute bindings."), 5000);
        return;
    }
    m_document->editLayers(layers, tr("Remove All Attribute Bindings"), [](Layer& l) { l.drivers.clear(); });
}

bool MainWindow::reassignBinding(int fromLayer, int index, int toLayer, int toValue)
{
    Scene const& scene = m_document->scene();
    if (fromLayer < 0 || fromLayer >= scene.layers.size() || toLayer < 0 || toLayer >= scene.layers.size() || index < 0
        || index >= scene.layers[fromLayer].drivers.size() || toValue < 0 || toValue >= Driver::ValueCount)
        return false;
    Driver moved = scene.layers[fromLayer].drivers[index];
    if (scene.layers[toLayer].id == moved.source)
        return false;   // (a layer following itself)
    for (int k = 0; k < scene.layers[toLayer].drivers.size(); ++k)
        if (scene.layers[toLayer].drivers[k].value == toValue && !(toLayer == fromLayer && k == index))
            return false;   // (that value follows another already)
    moved.value = toValue;
    m_document->beginMacro(tr("Reassign Target Attributes"));
    m_document->editLayers({fromLayer}, tr("Reassign Target Attributes"), [index](Layer& l) { l.drivers.removeAt(index); });
    m_document->editLayers({toLayer}, tr("Reassign Target Attributes"), [moved](Layer& l) { l.drivers.push_back(moved); });
    m_document->endMacro();
    return true;
}

void MainWindow::onActionReassignTargetAttributes()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drivers.isEmpty()) {
        statusBar()->showMessage(tr("The current layer has no attribute bindings to reassign."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ReassignBindingDialog"));
    dialog.setWindowTitle(tr("Reassign Target Attributes"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const which = new QComboBox(&dialog);
    for (Driver const& d : scene.layers[layer()].drivers)
        which->addItem(bindingText(scene, d));
    form->addRow(tr("Binding"), which);
    QComboBox* const target = new QComboBox(&dialog);
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        target->addItem(scene.layers[l].name, l);
    target->setCurrentIndex(std::max(0, target->findData(layer())));
    form->addRow(tr("Onto the layer"), target);
    QComboBox* const value = new QComboBox(&dialog);
    for (int v = 0; v < Driver::ValueCount; ++v)
        value->addItem(LayerPanel::valueName(v));
    value->setCurrentIndex(scene.layers[layer()].drivers.first().value);
    connect(which, &QComboBox::currentIndexChanged, &dialog, [&, value](int i) {
        if (i >= 0 && i < scene.layers[layer()].drivers.size())
            value->setCurrentIndex(scene.layers[layer()].drivers[i].value);
    });
    form->addRow(tr("Its attribute"), value);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!reassignBinding(layer(), which->currentIndex(), target->currentData().toInt(), value->currentIndex()))
        QMessageBox::information(this, tr("Reassign Target Attributes"),
                                 tr("Tried to add an Attribute-Binding that already exists, or a layer following itself: nothing was changed."));
}

void MainWindow::onActionCleanAttributeBindings()
{
    // Bindings to layers that are gone (or to themselves) taken away, on every layer.
    Scene const& scene = m_document->scene();
    QVector<int> layers;
    int count = 0;
    for (int l = 0; l < scene.layers.size(); ++l) {
        int dead = 0;
        for (Driver const& d : scene.layers[l].drivers)
            dead += scene.layerIndex(d.source) < 0 || d.source == scene.layers[l].id;
        if (dead) {
            layers << l;
            count += dead;
        }
    }
    if (layers.isEmpty()) {
        statusBar()->showMessage(tr("Every attribute binding follows a layer of the scene."), 5000);
        return;
    }
    m_document->editLayers(layers, tr("Clean Attribute Delegates"), [&scene](Layer& l) {
        l.drivers.erase(std::remove_if(l.drivers.begin(), l.drivers.end(),
                                       [&](Driver const& d) { return scene.layerIndex(d.source) < 0 || d.source == l.id; }),
                        l.drivers.end());
    });
    statusBar()->showMessage(tr("%n binding(s) to layers no longer in the scene removed.", nullptr, count), 6000);
}

void MainWindow::onActionClearPublishedAttributes()
{
    Scene const& scene = m_document->scene();
    QVector<int> groups;
    for (int l : attributeLayers()) {
        int g = l;
        while (g >= 0 && scene.layers[g].kind != Layer::Group)
            g = scene.parentIndex(g);
        if (g >= 0 && !scene.layers[g].published.isEmpty() && !groups.contains(g))
            groups << g;
    }
    if (groups.isEmpty()) {
        statusBar()->showMessage(tr("No group selected has published attributes."), 5000);
        return;
    }
    m_document->editLayers(groups, tr("Clear Published Attributes"), [](Layer& l) { l.published.clear(); });
}

void MainWindow::onActionPublishAttributeMode(bool on)
{
    m_layerPanel->setPublishMode(on);
    if (on)
        showView(dockOf("LayerPropertiesDock"));
}

void MainWindow::onActionShowAttributeInformation()
{
    showAttributeInformation(layer());
}

void MainWindow::showAttributeInformation(int at)
{
    Scene const& scene = m_document->scene();
    if (at < 0 || at >= scene.layers.size())
        return;
    Layer const& l = scene.layers[at];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AttributeInformationDialog"));
    dialog.setWindowTitle(tr("Attribute Information: %1").arg(l.name));
    QTableWidget* const table = new QTableWidget(0, 5, &dialog);
    table->setObjectName(QStringLiteral("AttributeInformationTable"));
    table->setHorizontalHeaderLabels({tr("Attribute Display Name:"), tr("Attribute Keyword:"), tr("Attribute Type:"), tr("Animation"), tr("Attribute Binding ID")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    static char const* const kKeywords[] = {"position.x", "position.y", "scale.x", "scale.y", "rotation.angle", "opacity", "position.z", "skew", "fov"};
    auto row = [&](QString const& name, QString const& keyword, QString const& type, QString const& animation, QString const& binding) {
        int const r = table->rowCount();
        table->insertRow(r);
        QStringList const cells{name, keyword, type, animation, binding};
        for (int c = 0; c < cells.size(); ++c)
            table->setItem(r, c, new QTableWidgetItem(cells[c]));
    };
    for (int v = 0; v < Driver::ValueCount; ++v) {
        if (v == Driver::Fov && l.kind != Layer::Camera)
            continue;
        FunctionColumn const* const column = scene.function(l.linkOf(v));
        QString binding;
        for (Driver const& d : l.drivers)
            if (d.value == v)
                binding = bindingText(scene, d);
        QString const animation = column ? tr("Function column %1").arg(column->name) : l.keys.isEmpty() ? tr("Not animated") : tr("%n keyframe(s)", nullptr, int(l.keys.size()));
        row(LayerPanel::valueName(v), QLatin1String(kKeywords[v]), v == Driver::Angle || v == Driver::Skew || v == Driver::Fov ? tr("Angle") : tr("Number"), animation,
            binding);
    }
    for (CustomAttribute const& a : l.customAttributes) {
        FunctionColumn const* const column = scene.function(a.function);
        row(a.name, QStringLiteral("custom.") + a.name.toLower(), tr("Number (dynamic)"), column ? tr("Function column %1").arg(column->name) : tr("Not animated"), QString());
    }
    for (PublishedAttribute const& p : l.published) {
        int const inner = scene.layerIndex(p.layer);
        row(inner >= 0 ? scene.layers[inner].name + QStringLiteral(": ") + LayerPanel::valueName(p.value) : tr("(layer gone)"), QStringLiteral("published"),
            tr("Published"), QString(), QString());
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(table);
    layout->addWidget(buttons);
    dialog.resize(760, 360);
    dialog.exec();
}

int MainWindow::setPropertiesOnLayers(QVector<int> const& layers, ManyLayerProperties const& set)
{
    if (layers.isEmpty())
        return 0;
    m_document->editLayers(layers, tr("Set Properties on Many Layers"), [set](Layer& l) {
        if (set.visible >= 0)
            l.visible = set.visible;
        if (set.locked >= 0)
            l.locked = set.locked;
        if (set.blend >= 0)
            l.blend = set.blend;
        if (set.enable3d >= 0)
            l.enable3d = set.enable3d;
        if (set.animatable >= 0)
            l.animatable = set.animatable;
        if (set.flipHorizontal >= 0 && l.holdsDrawings())
            l.flipHorizontal = set.flipHorizontal;
        if (set.flipVertical >= 0 && l.holdsDrawings())
            l.flipVertical = set.flipVertical;
        if (set.clipping >= 0 && l.holdsDrawings())
            l.clipping = set.clipping;
        if (set.opacity >= 0.0) {
            for (Keyframe& key : l.keys)
                key.pose.opacity = set.opacity;
            if (l.keys.isEmpty())
                l.opacity = set.opacity;
        }
    });
    return int(layers.size());
}

void MainWindow::onActionSetPropertiesOnManyLayers()
{
    QVector<int> const layers = attributeLayers();
    if (layers.isEmpty())
        return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ManyLayersDialog"));
    dialog.setWindowTitle(tr("Set Properties on Many Layers (%n layer(s))", nullptr, int(layers.size())));
    QGridLayout* const grid = new QGridLayout(&dialog);
    grid->addWidget(new QLabel(tr("Tick what to set; what is not ticked stays as each layer has it."), &dialog), 0, 0, 1, 2);
    int row = 1;
    auto line = [&](QString const& text, QWidget* value) {
        QCheckBox* const change = new QCheckBox(text, &dialog);
        value->setEnabled(false);
        connect(change, &QCheckBox::toggled, value, &QWidget::setEnabled);
        grid->addWidget(change, row, 0);
        grid->addWidget(value, row, 1);
        ++row;
        return change;
    };
    auto yesNo = [&](char const* name) {
        QComboBox* const box = new QComboBox(&dialog);
        box->setObjectName(QLatin1String(name));
        box->addItems({tr("On"), tr("Off")});
        return box;
    };
    QComboBox* const visible = yesNo("ManyVisible");
    QCheckBox* const setVisible = line(tr("Enabled (shown)"), visible);
    QComboBox* const locked = yesNo("ManyLocked");
    QCheckBox* const setLocked = line(tr("Locked"), locked);
    QComboBox* const blend = new QComboBox(&dialog);
    blend->addItems({tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"), tr("Darken"), tr("Lighten"), tr("Colour Dodge"), tr("Colour Burn"), tr("Hard Light"),
                     tr("Soft Light"), tr("Difference"), tr("Exclusion"), tr("Add")});
    QCheckBox* const setBlend = line(tr("Blend Mode"), blend);
    QDoubleSpinBox* const opacity = new QDoubleSpinBox(&dialog);
    opacity->setRange(0.0, 100.0);
    opacity->setSuffix(QStringLiteral(" %"));
    opacity->setValue(100.0);
    QCheckBox* const setOpacity = line(tr("Opacity (every keyframe)"), opacity);
    QComboBox* const enable3d = yesNo("Many3D");
    QCheckBox* const set3d = line(tr("Enable 3D"), enable3d);
    QComboBox* const animatable = yesNo("ManyAnimatable");
    QCheckBox* const setAnimatable = line(tr("Animate Using Animation Tools"), animatable);
    QComboBox* const flipH = yesNo("ManyFlipH");
    QCheckBox* const setFlipH = line(tr("Flip Horizontal"), flipH);
    QComboBox* const flipV = yesNo("ManyFlipV");
    QCheckBox* const setFlipV = line(tr("Flip Vertical"), flipV);
    QComboBox* const clipping = yesNo("ManyClip");
    QCheckBox* const setClipping = line(tr("Clipping"), clipping);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    grid->addWidget(buttons, row, 0, 1, 2);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto flag = [](QCheckBox* change, QComboBox* box) { return change->isChecked() ? (box->currentIndex() == 0 ? 1 : 0) : -1; };
    ManyLayerProperties set;
    set.visible = flag(setVisible, visible);
    set.locked = flag(setLocked, locked);
    set.blend = setBlend->isChecked() ? blend->currentIndex() : -1;
    set.opacity = setOpacity->isChecked() ? opacity->value() / 100.0 : -1.0;
    set.enable3d = flag(set3d, enable3d);
    set.animatable = flag(setAnimatable, animatable);
    set.flipHorizontal = flag(setFlipH, flipH);
    set.flipVertical = flag(setFlipV, flipV);
    set.clipping = flag(setClipping, clipping);
    int const done = setPropertiesOnLayers(layers, set);
    statusBar()->showMessage(tr("Properties set on %n layer(s).", nullptr, done), 5000);
}
