// Eclipse - the 3D Graph view: the layers as they hang together, with where each stands in space, and the parts of
// the 3D model the current layer shows.
#include "ui/views/Graph3DView.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"

namespace {

constexpr int kNodeRole = Qt::UserRole + 1;

QString number(double v)
{
    QString text = QString::number(v, 'f', 2);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

}

Graph3DView::Graph3DView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("Graph3DTree"));
    m_tree->setHeaderLabels({tr("Layer"), tr("X"), tr("Y"), tr("Z"), tr("Turn X"), tr("Turn Y"), tr("Depth")});
    // The names take the room there is; the numbers only what they need.
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 7; ++column)
        m_tree->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setToolTip(tr("The layers as they hang together, with where each stands in space on this frame.\n"
                          "Depth is how far towards the camera the layer ends up, through all it hangs under.\n"
                          "Under a layer that shows a 3D model: the parts of the model (its subnodes).\n"
                          "Untick a layer or a part to hide it; click one to select it."));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tree, 1);

    // The properties of a part made available: its own move, turn, scale and pivot.
    m_properties = new QGroupBox(tr("Subnode Properties"), this);
    m_properties->setObjectName(QStringLiteral("Graph3DProperties"));
    QGridLayout* const grid = new QGridLayout(m_properties);
    char const* const rows[4] = {QT_TR_NOOP("Position"), QT_TR_NOOP("Rotation"), QT_TR_NOOP("Scale"), QT_TR_NOOP("Pivot")};
    char const* const keys[4] = {"Position", "Rotation", "Scale", "Pivot"};
    char const* const axes[3] = {"X", "Y", "Z"};
    for (int r = 0; r < 4; ++r) {
        grid->addWidget(new QLabel(tr(rows[r]), m_properties), r, 0);
        for (int a = 0; a < 3; ++a) {
            QDoubleSpinBox* const box = new QDoubleSpinBox(m_properties);
            box->setObjectName(QStringLiteral("Graph3D%1%2").arg(QLatin1String(keys[r]), QLatin1String(axes[a])));
            box->setRange(r == 1 ? -3600.0 : r == 2 ? -100.0 : -100000.0, r == 1 ? 3600.0 : r == 2 ? 100.0 : 100000.0);
            box->setDecimals(3);
            box->setSingleStep(r == 1 ? 5.0 : 0.1);
            box->setPrefix(QStringLiteral("%1 ").arg(QLatin1String(axes[a])));
            box->setKeyboardTracking(false);
            m_values[r * 3 + a] = box;
            grid->addWidget(box, r, a + 1);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
                if (m_rebuilding)
                    return;
                auto v = [this](int i) { return QVector3D(float(m_values[i]->value()), float(m_values[i + 1]->value()), float(m_values[i + 2]->value())); };
                setSubnodeValues(v(0), v(3), v(6), v(9));
            });
        }
    }
    QPushButton* const reset = new QPushButton(tr("Reset Subnode Transformation Attributes"), m_properties);
    reset->setObjectName(QStringLiteral("Graph3DReset"));
    connect(reset, &QPushButton::clicked, this, &Graph3DView::onActionResetSubnode);
    grid->addWidget(reset, 4, 0, 1, 4);
    layout->addWidget(m_properties);
    m_properties->hide();

    // The commands on the parts.
    struct Command {
        char const* id;
        char const* text;
        char const* tip;
        void (Graph3DView::*slot)();
        QKeySequence keys;
    };
    QVector<Command> const commands = {
        {"Graph3DEnableSubnodes", QT_TR_NOOP("Enable Selected Subnodes"), QT_TR_NOOP("Enables the visibility of a selection of node(s)."), &Graph3DView::onActionEnableSubnodes, {}},
        {"Graph3DDisableSubnodes", QT_TR_NOOP("Disable Selected Subnodes"), QT_TR_NOOP("Disables the visibility of a selection of node(s)."), &Graph3DView::onActionDisableSubnodes, {}},
        {"Graph3DShowShapes", QT_TR_NOOP("Show Selected Sub-Object Shapes"), QT_TR_NOOP("The parts' own shapes shown again."), &Graph3DView::onActionShowShapes, {}},
        {"Graph3DHideShapes", QT_TR_NOOP("Hide Selected Sub-Object Shapes"), QT_TR_NOOP("The parts' own shapes hidden; what hangs under them still shows."), &Graph3DView::onActionHideShapes, {}},
        {nullptr, nullptr, nullptr, nullptr, {}},
        {"Graph3DSelectParent", QT_TR_NOOP("Select Parent Subnode"), QT_TR_NOOP("The current selection will move to its subnode parent."), &Graph3DView::onActionSelectParentSubnode, QKeySequence(Qt::Key_B)},
        {"Graph3DSelectChild", QT_TR_NOOP("Select Child Subnodes"), QT_TR_NOOP("The current selection will move to its first subnode child."), &Graph3DView::onActionSelectChildSubnodes, QKeySequence(Qt::SHIFT | Qt::Key_B)},
        {"Graph3DSelectNextSibling", QT_TR_NOOP("Select Next Sibling Subnode"), QT_TR_NOOP("The current selection will go to the next sibling subnode on the same level and under the same parent subnode."), &Graph3DView::onActionSelectNextSibling, QKeySequence(Qt::Key_Slash)},
        {"Graph3DSelectPreviousSibling", QT_TR_NOOP("Select Previous Sibling Subnode"), QT_TR_NOOP("The current selection will go to the previous sibling subnode on the same level and under the same parent subnode."), &Graph3DView::onActionSelectPreviousSibling, QKeySequence(Qt::Key_Question)},
        {"Graph3DCopySubnodeName", QT_TR_NOOP("Copy Subnode Name"), QT_TR_NOOP("The name of the current part, copied."), &Graph3DView::onActionCopySubnodeName, {}},
        {nullptr, nullptr, nullptr, nullptr, {}},
        {"Graph3DMakePropertiesAvailable", QT_TR_NOOP("Make Properties Available"), QT_TR_NOOP("Create transformation attributes for a subnode that didn't have any transformation before."), &Graph3DView::onActionMakePropertiesAvailable, {}},
        {"Graph3DResetSubnode", QT_TR_NOOP("Reset Subnode Transformation Attributes"), QT_TR_NOOP("The parts' moves, turns and scales put back as they are in the file."), &Graph3DView::onActionResetSubnode, {}},
        {nullptr, nullptr, nullptr, nullptr, {}},
        {"Graph3DExpandAll", QT_TR_NOOP("Expand All Elements"), QT_TR_NOOP("Expand all branches."), &Graph3DView::onActionExpandAll, {}},
        {"Graph3DCollapseAll", QT_TR_NOOP("Collapse All Elements"), QT_TR_NOOP("Collapse all branches."), &Graph3DView::onActionCollapseAll, {}},
        {"Graph3DExpandSelection", QT_TR_NOOP("Expand Selection"), QT_TR_NOOP("Expand selected branches."), &Graph3DView::onActionExpandSelection, {}},
        {"Graph3DCollapseSelection", QT_TR_NOOP("Collapse Selection"), QT_TR_NOOP("Collapse selected branches."), &Graph3DView::onActionCollapseSelection, {}}};
    QVector<QAction*> menuActions;
    for (Command const& c : commands) {
        if (!c.id) {
            QAction* const separator = new QAction(this);
            separator->setSeparator(true);
            menuActions.push_back(separator);
            continue;
        }
        QAction* const action = new QAction(tr(c.text), this);
        action->setObjectName(QLatin1String(c.id));
        action->setToolTip(tr(c.tip));
        action->setStatusTip(tr(c.tip));
        if (!c.keys.isEmpty()) {
            action->setShortcut(c.keys);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        }
        connect(action, &QAction::triggered, this, c.slot);
        addAction(action);
        menuActions.push_back(action);
    }
    m_extendedAction = new QAction(tr("Extended Mode"), this);
    m_extendedAction->setObjectName(QStringLiteral("Graph3DExtendedMode"));
    m_extendedAction->setCheckable(true);
    m_extendedAction->setChecked(true);
    m_extendedAction->setToolTip(tr("The places, turns and depths shown beside the names (off: the names alone)."));
    connect(m_extendedAction, &QAction::toggled, this, &Graph3DView::setExtendedMode);
    addAction(m_extendedAction);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this, menuActions](QPoint const& at) {
        QMenu menu(this);
        for (QAction* a : menuActions)
            menu.addAction(a);
        menu.addSeparator();
        menu.addAction(m_extendedAction);
        menu.exec(m_tree->viewport()->mapToGlobal(at));
    });

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != 0)
            return;
        int const node = nodeOf(item);
        if (node >= 0) {
            std::shared_ptr<Model3D::Mesh const> const model = mesh();
            if (!model || node >= model->nodes.size())
                return;
            QString const name = model->nodes[node].name;
            bool const hidden = item->checkState(0) != Qt::Checked;
            changeLook(hidden ? tr("Disable Subnode") : tr("Enable Subnode"), [&](Model3D::Look& look) { look.overrideFor(name).hidden = hidden; });
            return;
        }
        int const layer = m_document->scene().layerIndex(item->data(0, Qt::UserRole).toInt());
        if (layer >= 0)
            m_document->setLayerVisible(layer, item->checkState(0) == Qt::Checked);
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (m_rebuilding || !item)
            return;
        showProperties();
        if (nodeOf(item) >= 0)
            return;
        int const layer = m_document->scene().layerIndex(item->data(0, Qt::UserRole).toInt());
        if (layer >= 0 && layer != m_layer)
            emit layerPicked(layer);
    });
    connect(m_document, &Document::changed, this, &Graph3DView::rebuild);
    rebuild();
}

void Graph3DView::setCurrentCell(int layer, int frame)
{
    m_layer = layer;
    m_frame = frame;
    rebuild();
}

std::shared_ptr<Model3D::Mesh const> Graph3DView::mesh() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return nullptr;
    Layer const& layer = scene.layers[m_layer];
    int const d = layer.drawingAt(m_frame);
    if (d < 0 || d >= layer.drawings.size() || layer.drawings[d].model.isEmpty())
        return nullptr;
    return Model3D::load(layer.drawings[d].model);
}

Model3D::Look Graph3DView::look() const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return {};
    Layer const& layer = scene.layers[m_layer];
    int const d = layer.drawingAt(m_frame);
    return d >= 0 && d < layer.drawings.size() ? layer.drawings[d].modelLook : Model3D::Look();
}

void Graph3DView::changeLook(QString const& text, std::function<void(Model3D::Look&)> const& change)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    int const d = scene.layers[m_layer].drawingAt(m_frame);
    if (d < 0 || d >= scene.layers[m_layer].drawings.size() || scene.layers[m_layer].drawings[d].model.isEmpty())
        return;
    Model3D::Look changed = scene.layers[m_layer].drawings[d].modelLook;
    change(changed);
    changed.tidy();
    if (changed == scene.layers[m_layer].drawings[d].modelLook)
        return;
    int const l = m_layer;
    m_document->editScene(text, [l, d, changed](Scene& s) { s.layers[l].drawings[d].modelLook = changed; });
}

int Graph3DView::nodeOf(QTreeWidgetItem const* item) const
{
    if (!item)
        return -1;
    QVariant const v = item->data(0, kNodeRole);
    return v.isValid() ? v.toInt() : -1;
}

QTreeWidgetItem* Graph3DView::subnodeItem(int node) const
{
    return node >= 0 && node < m_subnodes.size() ? m_subnodes[node] : nullptr;
}

QStringList Graph3DView::selectedSubnodes() const
{
    QStringList names;
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (!model)
        return names;
    for (QTreeWidgetItem* item : m_tree->selectedItems())
        if (int const n = nodeOf(item); n >= 0 && n < model->nodes.size())
            names << model->nodes[n].name;
    return names;
}

QString Graph3DView::currentSubnode() const
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    int const n = nodeOf(m_tree->currentItem());
    return model && n >= 0 && n < model->nodes.size() ? model->nodes[n].name : QString();
}

bool Graph3DView::selectSubnode(QString const& name)
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (!model)
        return false;
    int const n = model->nodeNamed(name);
    QTreeWidgetItem* const item = subnodeItem(n);
    if (!item)
        return false;
    m_tree->clearSelection();
    m_tree->setCurrentItem(item);
    item->setSelected(true);
    return true;
}

QVector<int> Graph3DView::pickedNodes(QString const& what)
{
    QVector<int> nodes;
    for (QTreeWidgetItem* item : m_tree->selectedItems())
        if (int const n = nodeOf(item); n >= 0)
            nodes.push_back(n);
    if (nodes.isEmpty() && nodeOf(m_tree->currentItem()) >= 0)
        nodes.push_back(nodeOf(m_tree->currentItem()));
    if (nodes.isEmpty())
        emit message(mesh() ? tr("%1: no subnode selected.").arg(what) : tr("No 3D model selected."));
    return nodes;
}

void Graph3DView::setSubnodesHidden(bool hidden)
{
    QString const what = hidden ? tr("Disable Subnode") : tr("Enable Subnode");
    QVector<int> const nodes = pickedNodes(what);
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (nodes.isEmpty() || !model)
        return;
    changeLook(what, [&](Model3D::Look& look) {
        for (int n : nodes)
            look.overrideFor(model->nodes[n].name).hidden = hidden;
    });
}

void Graph3DView::setShapesHidden(bool hidden)
{
    QString const what = hidden ? tr("Hide Subnode Shape") : tr("Show Subnode Shape");
    QVector<int> const nodes = pickedNodes(what);
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (nodes.isEmpty() || !model)
        return;
    changeLook(what, [&](Model3D::Look& look) {
        for (int n : nodes)
            look.overrideFor(model->nodes[n].name).shapeHidden = hidden;
    });
}

void Graph3DView::onActionSelectParentSubnode()
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    int const n = nodeOf(m_tree->currentItem());
    if (!model || n < 0) {
        emit message(tr("No 3D model selected."));
        return;
    }
    int const parent = model->nodes[n].parent;
    if (QTreeWidgetItem* const item = parent >= 0 ? subnodeItem(parent) : m_tree->currentItem()->parent()) {
        m_tree->clearSelection();
        m_tree->setCurrentItem(item);
        item->setSelected(true);
    }
}

void Graph3DView::onActionSelectChildSubnodes()
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    QTreeWidgetItem* const current = m_tree->currentItem();
    int const n = nodeOf(current);
    QTreeWidgetItem* child = nullptr;
    if (model && n >= 0) {
        QVector<int> const children = model->children(n);
        child = children.isEmpty() ? nullptr : subnodeItem(children.first());
    } else if (current && current->childCount() > 0 && nodeOf(current->child(0)) >= 0) {
        // (From the layer: to the model itself.)
        child = current->child(0);
    }
    if (!child)
        return;
    m_tree->clearSelection();
    m_tree->setCurrentItem(child);
    child->setSelected(true);
}

void Graph3DView::selectSibling(int by)
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    int const n = nodeOf(m_tree->currentItem());
    if (!model || n < 0) {
        emit message(tr("No 3D model selected."));
        return;
    }
    QVector<int> const siblings = model->children(model->nodes[n].parent);
    int const at = int(siblings.indexOf(n)) + by;
    if (at < 0 || at >= siblings.size())
        return;
    if (QTreeWidgetItem* const item = subnodeItem(siblings[at])) {
        m_tree->clearSelection();
        m_tree->setCurrentItem(item);
        item->setSelected(true);
    }
}

void Graph3DView::onActionCopySubnodeName()
{
    QString const name = currentSubnode();
    if (name.isEmpty()) {
        emit message(tr("No 3D model selected."));
        return;
    }
    QApplication::clipboard()->setText(name);
}

void Graph3DView::onActionMakePropertiesAvailable()
{
    QVector<int> const nodes = pickedNodes(tr("Make Properties Available"));
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (nodes.isEmpty() || !model)
        return;
    changeLook(tr("Make Subnode Properties Available"), [&](Model3D::Look& look) {
        for (int n : nodes)
            look.overrideFor(model->nodes[n].name).available = true;
    });
    showProperties();
}

void Graph3DView::onActionResetSubnode()
{
    QVector<int> const nodes = pickedNodes(tr("Reset Subnode Transformation Attributes"));
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (nodes.isEmpty() || !model)
        return;
    changeLook(tr("Reset Subnode Transformation Attributes"), [&](Model3D::Look& look) {
        for (int n : nodes) {
            Model3D::NodeOverride& o = look.overrideFor(model->nodes[n].name);
            o.move = o.turn = o.pivot = QVector3D();
            o.scale = QVector3D(1, 1, 1);
        }
    });
    showProperties();
}

void Graph3DView::setSubnodeValues(QVector3D const& move, QVector3D const& turn, QVector3D const& scale, QVector3D const& pivot)
{
    QVector<int> const nodes = pickedNodes(tr("Set Subnode Position"));
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    if (nodes.isEmpty() || !model)
        return;
    Model3D::Look const before = look();
    Model3D::NodeOverride const* const was = before.overrideOf(model->nodes[nodes.first()].name);
    // (Named for what was changed.)
    QString text = tr("Set Subnode Position");
    if (was && was->move == move)
        text = was->turn != turn ? tr("Set Subnode Rotation") : was->scale != scale ? tr("Set Subnode Scale") : tr("Set Subnode Pivot");
    changeLook(text, [&](Model3D::Look& look) {
        for (int n : nodes) {
            Model3D::NodeOverride& o = look.overrideFor(model->nodes[n].name);
            o.available = true;
            o.move = move;
            o.turn = turn;
            o.scale = scale;
            o.pivot = pivot;
        }
    });
}

void Graph3DView::onActionExpandAll()
{
    m_tree->expandAll();
}

void Graph3DView::onActionCollapseAll()
{
    m_tree->collapseAll();
}

void Graph3DView::expandSelection(bool expand)
{
    std::function<void(QTreeWidgetItem*)> all = [&](QTreeWidgetItem* item) {
        item->setExpanded(expand);
        for (int c = 0; c < item->childCount(); ++c)
            all(item->child(c));
    };
    for (QTreeWidgetItem* item : m_tree->selectedItems())
        all(item);
}

void Graph3DView::setExtendedMode(bool on)
{
    m_extended = on;
    if (m_extendedAction->isChecked() != on)
        m_extendedAction->setChecked(on);
    for (int column = 1; column < 7; ++column)
        m_tree->setColumnHidden(column, !on);
}

void Graph3DView::showProperties()
{
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    int const n = nodeOf(m_tree->currentItem());
    Model3D::Look const now = look();
    Model3D::NodeOverride const* const o = model && n >= 0 && n < model->nodes.size() ? now.overrideOf(model->nodes[n].name) : nullptr;
    m_properties->setVisible(o && o->available);
    if (!o || !o->available)
        return;
    m_properties->setTitle(tr("Subnode Properties: %1").arg(o->name));
    bool const was = m_rebuilding;
    m_rebuilding = true;
    QVector3D const values[4] = {o->move, o->turn, o->scale, o->pivot};
    for (int r = 0; r < 4; ++r)
        for (int a = 0; a < 3; ++a)
            m_values[r * 3 + a]->setValue(values[r][a]);
    m_rebuilding = was;
}

void Graph3DView::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    static char const* const kKinds[] = {QT_TR_NOOP("drawing"), QT_TR_NOOP("peg"), QT_TR_NOOP("camera"), QT_TR_NOOP("sound"),
                                         QT_TR_NOOP("colour-card"), QT_TR_NOOP("group"), QT_TR_NOOP("bone"), QT_TR_NOOP("quadmap"), QT_TR_NOOP("annotation")};
    QVector<int> shape;
    for (Layer const& layer : scene.layers)
        shape << layer.id << layer.parent;
    std::shared_ptr<Model3D::Mesh const> const model = mesh();
    QString modelShape;
    if (model) {
        Layer const& layer = scene.layers[m_layer];
        modelShape = QStringLiteral("%1|%2|%3|%4").arg(layer.id).arg(layer.drawingAt(m_frame)).arg(layer.drawings[layer.drawingAt(m_frame)].model).arg(model->nodes.size());
    }
    QVector<QTreeWidgetItem*> items(scene.layers.size(), nullptr);
    QString const current = currentSubnode();
    if (shape == m_shape && modelShape == m_modelShape) {
        QList<QTreeWidgetItem*> const all = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
        for (QTreeWidgetItem* const item : all) {
            if (nodeOf(item) >= 0)
                continue;
            int const l = scene.layerIndex(item->data(0, Qt::UserRole).toInt());
            if (l >= 0)
                items[l] = item;
        }
    } else {
        m_tree->clear();
        m_subnodes.clear();
        m_shape = shape;
        m_modelShape = modelShape;
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            QTreeWidgetItem* const item = new QTreeWidgetItem;
            item->setData(0, Qt::UserRole, scene.layers[l].id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            for (int column = 1; column < 7; ++column)
                item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
            items[l] = item;
        }
        // A layer goes under what it hangs under; the front layers first, as in the Timeline.
        for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
            int const parent = scene.parentIndex(l);
            if (parent >= 0 && items[parent])
                items[parent]->addChild(items[l]);
            else
                m_tree->addTopLevelItem(items[l]);
        }
        // The parts of the model the current layer shows, under it.
        if (model && m_layer >= 0 && m_layer < items.size() && items[m_layer]) {
            m_subnodes.fill(nullptr, model->nodes.size());
            for (int n = 0; n < model->nodes.size(); ++n) {
                QTreeWidgetItem* const item = new QTreeWidgetItem;
                item->setData(0, Qt::UserRole, scene.layers[m_layer].id);
                item->setData(0, kNodeRole, n);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                for (int column = 1; column < 7; ++column)
                    item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
                m_subnodes[n] = item;
            }
            for (int n = 0; n < model->nodes.size(); ++n) {
                int const parent = model->nodes[n].parent;
                QTreeWidgetItem* const above = parent >= 0 && parent < n ? m_subnodes[parent] : parent >= 0 ? subnodeItem(parent) : items[m_layer];
                (above ? above : items[m_layer])->addChild(m_subnodes[n]);
            }
        }
        m_tree->expandAll();
    }
    for (int l = 0; l < scene.layers.size(); ++l) {
        QTreeWidgetItem* const item = items[l];
        if (!item)
            continue;
        Layer const& layer = scene.layers[l];
        Pose const pose = layer.poseAt(m_frame);
        item->setText(0, tr("%1  (%2)").arg(layer.name, tr(kKinds[layer.kind])));
        if (item->checkState(0) != (layer.visible ? Qt::Checked : Qt::Unchecked))
            item->setCheckState(0, layer.visible ? Qt::Checked : Qt::Unchecked);
        bool const placed = layer.kind != Layer::Sound && layer.kind != Layer::Group && layer.kind != Layer::ColourCard && layer.kind != Layer::Annotation;
        item->setText(1, placed ? number(pose.x) : QString());
        item->setText(2, placed ? number(pose.y) : QString());
        item->setText(3, placed ? number(pose.z) : QString());
        item->setText(4, placed ? number(pose.rotX) : QString());
        item->setText(5, placed ? number(pose.rotY) : QString());
        item->setText(6, placed ? number(scene.depthOf(l, m_frame)) : QString());
        item->setToolTip(6, placed && scene.behindCamera(l, m_frame) ? tr("Behind the camera on this frame: not seen.") : QString());
    }
    if (model) {
        Model3D::Look const now = look();
        for (int n = 0; n < m_subnodes.size(); ++n) {
            QTreeWidgetItem* const item = m_subnodes[n];
            if (!item)
                continue;
            Model3D::NodeOverride const* const o = now.overrideOf(model->nodes[n].name);
            item->setText(0, tr("%1  (subnode, %n triangle(s))", nullptr, model->nodes[n].triangles).arg(model->nodes[n].name));
            Qt::CheckState const state = o && o->hidden ? Qt::Unchecked : Qt::Checked;
            if (item->checkState(0) != state)
                item->setCheckState(0, state);
            QFont font = item->font(0);
            font.setItalic(o && o->shapeHidden);
            item->setFont(0, font);
            item->setToolTip(0, o && o->shapeHidden ? tr("Its own shape hidden.") : QString());
            bool const values = o && o->available;
            item->setText(1, values ? number(o->move.x()) : QString());
            item->setText(2, values ? number(o->move.y()) : QString());
            item->setText(3, values ? number(o->move.z()) : QString());
            item->setText(4, values ? number(o->turn.x()) : QString());
            item->setText(5, values ? number(o->turn.y()) : QString());
            item->setText(6, QString());
        }
    }
    if (!current.isEmpty() && model && subnodeItem(model->nodeNamed(current))) {
        if (m_tree->currentItem() != subnodeItem(model->nodeNamed(current)))
            m_tree->setCurrentItem(subnodeItem(model->nodeNamed(current)));
    } else if (m_layer >= 0 && m_layer < items.size() && items[m_layer] && m_tree->currentItem() != items[m_layer]) {
        m_tree->setCurrentItem(items[m_layer]);
    }
    m_rebuilding = false;
    showProperties();
}
