// Eclipse - the Node View's searches: Find and Insert Node (a list to add from, typed into), and Search For Nodes.
#include "ui/views/NodeView.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "model/document/Commands.h"
#include "render/effects/Effects.h"
#include "ui/views/NodeLibraryView.h"

namespace {

// The nodes only a graph wired by hand has, offered by name.
QStringList graphKinds()
{
    return {NodeView::tr("Composite"), NodeView::tr("Group"), NodeView::tr("Display"), NodeView::tr("Note"), NodeView::tr("Waypoint"),
            NodeView::tr("Backdrop")};
}

}  // namespace

QStringList NodeView::insertableNames() const
{
    QStringList names;
    for (NodeLibraryView::Item const& item : NodeLibraryView::items())
        names << item.name;
    for (QString const& kind : graphKinds())
        if (graphMode() || kind == tr("Backdrop"))
            names << kind;
    names.removeDuplicates();
    return names;
}

bool NodeView::insertByName(QString const& name, QPointF graphPos)
{
    QString const wanted = name.trimmed();
    QPointF const corner = graphPos - QPointF(kNodeWidth / 2.0, kNodeHeight / 2.0);
    if (wanted.compare(tr("Backdrop"), Qt::CaseInsensitive) == 0) {
        addBackdrop();
        return true;
    }
    if (graphMode()) {
        QStringList const kinds = graphKinds();
        GraphNode::Kind const made[] = {GraphNode::Composite, GraphNode::Group, GraphNode::Display, GraphNode::Note, GraphNode::Waypoint};
        for (int k = 0; k < 5; ++k)
            if (wanted.compare(kinds[k], Qt::CaseInsensitive) == 0) {
                if (made[k] == GraphNode::Waypoint) {
                    addWaypoint(graphPos);
                } else if (made[k] == GraphNode::Display && m_graphGroup) {
                    emit statusMessage(tr("A display goes at the top of the graph, outside the groups."));
                    return false;
                } else {
                    GraphNode n;
                    n.kind = made[k];
                    addGraphNode(n, corner);
                }
                return true;
            }
    }
    for (NodeLibraryView::Item const& item : NodeLibraryView::items()) {
        if (item.name.compare(wanted, Qt::CaseInsensitive) != 0)
            continue;
        if (item.effect && graphMode()) {
            // In a graph wired by hand an effect is a node of its own.
            GraphNode n;
            n.kind = GraphNode::EffectNode;
            n.effect = Effect::made(Effect::Type(item.what.mid(7).toInt()));
            addGraphNode(n, corner);
            return true;
        }
        emit nodeDropped(item.what, corner);
        return true;
    }
    return false;
}

void NodeView::onActionFindAndInsert()
{
    // A list of what can be added, narrowed as one types; Enter adds the one chosen where the pointer is.
    QPointF const at = rect().contains(m_hover.toPoint()) ? m_hover : QPointF(width() / 2.0, height() / 2.0);
    QDialog popup(this, Qt::Popup);
    popup.setObjectName(QStringLiteral("NodeFindInsert"));
    QVBoxLayout* const layout = new QVBoxLayout(&popup);
    layout->setContentsMargins(4, 4, 4, 4);
    QLineEdit* const typed = new QLineEdit(&popup);
    typed->setObjectName(QStringLiteral("NodeFindInsertText"));
    typed->setPlaceholderText(tr("Find and insert a node"));
    QListWidget* const list = new QListWidget(&popup);
    list->setObjectName(QStringLiteral("NodeFindInsertList"));
    list->addItems(insertableNames());
    list->setCurrentRow(0);
    layout->addWidget(typed);
    layout->addWidget(list);
    connect(typed, &QLineEdit::textChanged, &popup, [list](QString const& text) {
        int first = -1;
        for (int i = 0; i < list->count(); ++i) {
            bool const shown = list->item(i)->text().contains(text.trimmed(), Qt::CaseInsensitive);
            list->item(i)->setHidden(!shown);
            if (shown && first < 0)
                first = i;
        }
        list->setCurrentRow(first);
    });
    QString chosen;
    auto choose = [&] {
        if (QListWidgetItem* const item = list->currentItem(); item && !item->isHidden())
            chosen = item->text();
        popup.accept();
    };
    connect(typed, &QLineEdit::returnPressed, &popup, choose);
    connect(list, &QListWidget::itemActivated, &popup, choose);
    popup.resize(240, 280);
    popup.move(mapToGlobal(at.toPoint()));
    typed->setFocus();
    popup.exec();
    if (!chosen.isEmpty())
        insertByName(chosen, toGraph(at));
}

QString NodeView::nodeTypeName(int key) const
{
    Scene const& scene = m_document->scene();
    auto layerKind = [&](int l) {
        if (l < 0 || l >= scene.layers.size())
            return QString();
        static char const* const kinds[] = {QT_TR_NOOP("Drawing"), QT_TR_NOOP("Peg"), QT_TR_NOOP("Camera"), QT_TR_NOOP("Sound"), QT_TR_NOOP("Colour-Card"),
                                            QT_TR_NOOP("Group"), QT_TR_NOOP("Bone"), QT_TR_NOOP("Quadmap"), QT_TR_NOOP("Annotation")};
        int const kind = scene.layers[l].kind;
        return kind >= 0 && kind < 9 ? tr(kinds[kind]) : QString();
    };
    if (!graphMode())
        return layerKind(scene.layerIndex(key));
    GraphNode const* const n = scene.graph.node(key);
    if (!n)
        return QString();
    switch (n->kind) {
    case GraphNode::LayerNode: return layerKind(scene.layerIndex(n->layer));
    case GraphNode::Composite: return tr("Composite");
    case GraphNode::EffectNode: return Effects::name(Effect::made(n->effect.type), scene).section(QStringLiteral("  "), 0, 0);
    case GraphNode::Group: return tr("Group");
    case GraphNode::GroupIn: return tr("Multi-Port-In");
    case GraphNode::GroupOut: return tr("Multi-Port-Out");
    case GraphNode::Display: return tr("Display");
    case GraphNode::Note: return tr("Note");
    case GraphNode::Waypoint: return tr("Waypoint");
    default: return QString();
    }
}

QStringList NodeView::nodeTypeNames() const
{
    // The kinds the scene has nodes of.
    QStringList names;
    Scene const& scene = m_document->scene();
    if (graphMode())
        for (GraphNode const& n : scene.graph.nodes)
            names << nodeTypeName(n.id);
    else
        for (Layer const& l : scene.layers)
            names << nodeTypeName(l.id);
    names.removeAll(QString());
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

int NodeView::selectMatching(QString const& text, bool caseSensitive, QStringList const& types)
{
    Scene const& scene = m_document->scene();
    Qt::CaseSensitivity const how = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    auto fits = [&](QString const& name, int key) {
        return (text.trimmed().isEmpty() || name.contains(text.trimmed(), how)) && (types.isEmpty() || types.contains(nodeTypeName(key)));
    };
    // Found anywhere; the view goes to the level of the first, where those found are picked.
    if (graphMode()) {
        QVector<int> found;
        for (GraphNode const& n : scene.graph.nodes)
            if (n.kind != GraphNode::GroupIn && n.kind != GraphNode::GroupOut && fits(graphNodeName(n), n.id))
                found << n.id;
        if (found.isEmpty())
            return 0;
        int const level = scene.graph.node(found.first())->group;
        if (level != m_graphGroup)
            enterGraphGroup(level);
        m_graphPicked.clear();
        for (int id : std::as_const(found))
            if (scene.graph.node(id)->group == level)
                m_graphPicked << id;
        m_graphNode = m_graphPicked.last();
        emitPicked();
        onActionCentreOnSelection();
        update();
        return int(found.size());
    }
    QVector<int> found;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        if (fits(scene.layers[l].name, scene.layers[l].id))
            found << l;
    if (found.isEmpty())
        return 0;
    selectLayers(found);
    onActionCentreOnSelection();
    return int(found.size());
}

void NodeView::onActionSearchNodes()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("NodeSearchDialog"));
    dialog.setWindowTitle(tr("Search For Nodes"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Find :"), &dialog));
    QLineEdit* const text = new QLineEdit(&dialog);
    text->setObjectName(QStringLiteral("NodeSearchFind"));
    layout->addWidget(text);
    QCheckBox* const caseSensitive = new QCheckBox(tr("Case Sensitive"), &dialog);
    caseSensitive->setObjectName(QStringLiteral("NodeSearchCase"));
    layout->addWidget(caseSensitive);
    QCheckBox* const byType = new QCheckBox(tr("Search by Node Type"), &dialog);
    byType->setObjectName(QStringLiteral("NodeSearchByType"));
    layout->addWidget(byType);
    QLabel* const typesLabel = new QLabel(tr("Available Node Types :"), &dialog);
    layout->addWidget(typesLabel);
    QListWidget* const types = new QListWidget(&dialog);
    types->setObjectName(QStringLiteral("NodeSearchTypes"));
    types->setSelectionMode(QAbstractItemView::MultiSelection);
    types->addItems(nodeTypeNames());
    types->setEnabled(false);
    layout->addWidget(types);
    connect(byType, &QCheckBox::toggled, types, &QWidget::setEnabled);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList wanted;
    if (byType->isChecked())
        for (QListWidgetItem* item : types->selectedItems())
            wanted << item->text();
    int const found = selectMatching(text->text(), caseSensitive->isChecked(), wanted);
    emit statusMessage(found ? tr("%n node(s) found.", nullptr, found) : tr("No node found."));
}
