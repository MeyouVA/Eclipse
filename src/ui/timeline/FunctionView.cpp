// Eclipse - the Function view: the list of the layers loaded and their values, the boxes and buttons over the graph.
#include "ui/timeline/FunctionView.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"

namespace {

// The kinds of value the buttons under the list tick at once.
QVector<Timeline::Parameter> kindOf(QString const& kind)
{
    if (kind == QLatin1String("motion"))
        return {Timeline::PosX, Timeline::PosY, Timeline::PosZ};
    if (kind == QLatin1String("rotation"))
        return {Timeline::Angle};
    if (kind == QLatin1String("scale"))
        return {Timeline::ScaleX, Timeline::ScaleY};
    if (kind == QLatin1String("skew"))
        return {Timeline::Skew};
    if (kind == QLatin1String("other"))
        return {Timeline::Skew, Timeline::Opacity, Timeline::Fov};
    QVector<Timeline::Parameter> all;
    for (int which = 0; which < Timeline::ParameterCount; ++which)
        all.push_back(Timeline::Parameter(which));
    return all;
}

// The order the values are listed in under a layer.
Timeline::Parameter const kListed[] = {Timeline::PosX, Timeline::PosY, Timeline::PosZ, Timeline::ScaleX, Timeline::ScaleY,
                                       Timeline::Angle, Timeline::Skew, Timeline::Opacity, Timeline::Fov};

}  // namespace

FunctionView::FunctionView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_graph = new FunctionGraph(document, this);
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("FunctionList"));
    m_list->setHeaderHidden(true);
    m_list->setFixedWidth(150);
    m_list->setRootIsDecorated(true);
    m_list->setIndentation(12);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (m_filling)
            return;
        bool const on = item->checkState(0) == Qt::Checked;
        if (!item->parent() && item->data(0, Qt::UserRole + 1).toBool()) {
            // A function column: its curve drawn or not.
            int const index = m_document->scene().functionIndex(item->data(0, Qt::UserRole).toInt());
            if (index >= 0)
                m_graph->setLayerHidden(-1 - index, !on);
            return;
        }
        if (!item->parent()) {
            // A layer: its curves drawn or not.
            int const index = m_document->scene().layerIndex(item->data(0, Qt::UserRole).toInt());
            if (index >= 0)
                m_graph->setLayerHidden(index, !on);
            return;
        }
        // A value: drawn for every layer loaded, and ticked under each.
        Timeline::Parameter const which = Timeline::Parameter(item->data(0, Qt::UserRole).toInt());
        m_graph->setShown(which, on);
        QSignalBlocker block(m_list);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i)
            for (int c = 0; c < m_list->topLevelItem(i)->childCount(); ++c) {
                QTreeWidgetItem* const child = m_list->topLevelItem(i)->child(c);
                if (child->data(0, Qt::UserRole).toInt() == which)
                    child->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            }
        syncKinds();
    });
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        // A value clicked: its points selected (the curve to work on).  A column: its points.
        if (!item->parent() && item->data(0, Qt::UserRole + 1).toBool()) {
            int const index = m_document->scene().functionIndex(item->data(0, Qt::UserRole).toInt());
            QVector<FunctionPoint> chosen;
            for (FunctionPoint const& p : m_graph->points())
                if (p.layer == -1 - index)
                    chosen.push_back(p);
            m_graph->setSelection(chosen);
            return;
        }
        if (!item->parent())
            return;
        int const index = m_document->scene().layerIndex(item->parent()->data(0, Qt::UserRole).toInt());
        int const which = item->data(0, Qt::UserRole).toInt();
        if (index < 0 || !m_graph->isShown(Timeline::Parameter(which)))
            return;
        QPair<int, int> const source = m_graph->sourceOf(index, which);
        QVector<FunctionPoint> chosen;
        for (FunctionPoint const& p : m_graph->points())
            if (p.layer == source.first && p.value == source.second)
                chosen.push_back(p);
        m_graph->setSelection(chosen);
    });

    // Under the list: the kinds of value, ticked at once.
    QWidget* const kinds = new QWidget(this);
    QHBoxLayout* const kindRow = new QHBoxLayout(kinds);
    kindRow->setContentsMargins(2, 2, 2, 2);
    kindRow->setSpacing(2);
    for (QString const& kind : {QStringLiteral("motion"), QStringLiteral("rotation"), QStringLiteral("scale"), QStringLiteral("skew"), QStringLiteral("other"), QStringLiteral("all")}) {
        QToolButton* const button = new QToolButton(kinds);
        button->setObjectName(QStringLiteral("FunctionKind_") + kind);
        button->setAutoRaise(true);
        button->setProperty("kind", kind);
        kindRow->addWidget(button);
        m_kindButtons.push_back(button);
    }
    kindRow->addStretch(1);
    QWidget* const left = new QWidget(this);
    QVBoxLayout* const leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(0);
    leftColumn->addWidget(m_list, 1);
    leftColumn->addWidget(kinds);
    left->setFixedWidth(150);

    // Over the graph: the frame and value of the point selected, and the buttons.
    m_strip = new QWidget(this);
    m_strip->setObjectName(QStringLiteral("FunctionStrip"));
    QHBoxLayout* const strip = new QHBoxLayout(m_strip);
    strip->setContentsMargins(4, 2, 4, 2);
    strip->setSpacing(4);
    m_frameBox = new QDoubleSpinBox(m_strip);
    m_frameBox->setObjectName(QStringLiteral("FunctionFrame"));
    m_frameBox->setDecimals(4);
    m_frameBox->setRange(-100000.0, 100000.0);
    m_frameBox->setKeyboardTracking(false);
    m_valueBox = new QDoubleSpinBox(m_strip);
    m_valueBox->setObjectName(QStringLiteral("FunctionValue"));
    m_valueBox->setDecimals(4);
    m_valueBox->setRange(-1e9, 1e9);
    m_valueBox->setKeyboardTracking(false);
    strip->addWidget(new QLabel(tr("Frame"), m_strip));
    strip->addWidget(m_frameBox);
    strip->addWidget(new QLabel(tr("Value"), m_strip));
    strip->addWidget(m_valueBox);
    strip->addStretch(1);
    auto typed = [this] {
        if (!m_filling)
            m_graph->setSelectedSpot(m_frameBox->value(), m_valueBox->value());
    };
    connect(m_frameBox, &QDoubleSpinBox::editingFinished, this, typed);
    connect(m_valueBox, &QDoubleSpinBox::editingFinished, this, typed);

    QWidget* const right = new QWidget(this);
    QVBoxLayout* const rightColumn = new QVBoxLayout(right);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(0);
    rightColumn->addWidget(m_strip);
    rightColumn->addWidget(m_graph, 1);

    QHBoxLayout* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(left);
    layout->addWidget(right, 1);

    connect(m_graph, &FunctionGraph::framePicked, this, &FunctionView::framePicked);
    connect(m_graph, &FunctionGraph::selectionChanged, this, &FunctionView::showSpot);
    connect(m_document, &Document::changed, this, [this] {
        // (Columns are kept by id: their places may have changed.)
        if (!m_loadedColumns.isEmpty())
            loadIntoGraph();
        else
            rebuildList();
        showSpot();
    });
    rebuildList();
    showSpot();
}

void FunctionView::setActions(ActionManager* actions)
{
    m_actions = actions;
    m_graph->setActions(actions);
    if (!actions)
        return;
    QHBoxLayout* const strip = static_cast<QHBoxLayout*>(m_strip->layout());
    auto add = [&](QString const& id, bool forPoints) {
        QAction* const action = actions->action(id);
        if (!action)
            return;
        QToolButton* const button = new QToolButton(m_strip);
        button->setObjectName(QStringLiteral("FunctionButton_") + id);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(18, 18));
        strip->addWidget(button);
        if (forPoints)
            m_pointButtons.push_back(button);
    };
    add(QStringLiteral("FunctionSelectPreviousKey"), false);
    add(QStringLiteral("FunctionSelectNextKey"), false);
    add(QStringLiteral("FunctionStepped"), true);
    add(QStringLiteral("FunctionStraight"), true);
    add(QStringLiteral("FunctionCurved"), true);
    add(QStringLiteral("FunctionSmooth"), true);
    add(QStringLiteral("FunctionCorner"), true);
    add(QStringLiteral("FunctionInsertPoint"), false);
    add(QStringLiteral("FunctionToggleGrid"), false);
    add(QStringLiteral("FunctionResetView"), false);
    for (QToolButton* button : std::as_const(m_kindButtons)) {
        QString const kind = button->property("kind").toString();
        QString const id = QStringLiteral("FunctionShow") + kind.left(1).toUpper() + kind.mid(1);
        if (QAction* const action = actions->action(id)) {
            button->setDefaultAction(action);
            button->setIconSize(QSize(16, 16));
        }
    }
    actions->setChecked(QStringLiteral("FunctionAutoLoad"), m_autoLoad);
    actions->setChecked(QStringLiteral("FunctionShowCurrentFrame"), m_graph->currentFrameShown());
    syncKinds();
    showSpot();
}

QVector<int> FunctionView::selectedIds() const
{
    Scene const& scene = m_document->scene();
    QVector<int> ids;
    if (m_current >= 0 && m_current < scene.layers.size())
        ids.push_back(scene.layers[m_current].id);
    for (int l : m_timelineSelection)
        if (l >= 0 && l < scene.layers.size() && !ids.contains(scene.layers[l].id))
            ids.push_back(scene.layers[l].id);
    return ids;
}

void FunctionView::setCurrentCell(int layer, int frame)
{
    m_current = layer;
    if (m_autoLoad) {
        QVector<int> const ids = selectedIds();
        if (ids != m_loaded) {
            m_loaded = ids;
            m_graph->setCurrentCell(layer, frame);
            loadIntoGraph();
            return;
        }
        m_graph->setCurrentCell(layer, frame);
        return;
    }
    int const first = m_loaded.isEmpty() ? -1 : m_document->scene().layerIndex(m_loaded.first());
    m_graph->setCurrentCell(first, frame);
}

void FunctionView::setOtherLayers(QVector<int> const& layers)
{
    m_timelineSelection = layers;
    if (!m_autoLoad)
        return;
    QVector<int> const ids = selectedIds();
    if (ids == m_loaded)
        return;
    m_loaded = ids;
    loadIntoGraph();
}

void FunctionView::loadIntoGraph()
{
    Scene const& scene = m_document->scene();
    QVector<int> indices;
    for (int id : std::as_const(m_loaded))
        if (int const index = scene.layerIndex(id); index >= 0)
            indices.push_back(index);
    int const first = indices.isEmpty() ? -1 : indices.first();
    if (first != m_graph->currentLayer())
        m_graph->setCurrentCell(first, m_graph->currentFrame());
    m_graph->setOtherLayers(indices.mid(1));
    QVector<int> columns;
    for (int id : std::as_const(m_loadedColumns))
        if (int const index = scene.functionIndex(id); index >= 0)
            columns.push_back(index);
    m_graph->setColumns(columns);
    rebuildList();
}

void FunctionView::loadColumn(int id)
{
    if (!m_document->scene().function(id))
        return;
    if (!m_loadedColumns.contains(id))
        m_loadedColumns.push_back(id);
    loadIntoGraph();
}

void FunctionView::sendToFunctionView(int layer, int value)
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return;
    if (int const id = scene.layers[layer].linkOf(value))
        loadColumn(id);
    if (value < 0 || value >= Timeline::ParameterCount)
        return;
    m_graph->setShown(Timeline::Parameter(value), true);
    m_list->setProperty("signature", QString());
    rebuildList();
    syncKinds();
    QPair<int, int> const source = m_graph->sourceOf(layer, value);
    QVector<FunctionPoint> chosen;
    for (FunctionPoint const& p : m_graph->points())
        if (p.layer == source.first && p.value == source.second)
            chosen.push_back(p);
    m_graph->setSelection(chosen);
}

QTreeWidgetItem* FunctionView::valueItem(int layer, Timeline::Parameter which) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return nullptr;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* const top = m_list->topLevelItem(i);
        if (top->data(0, Qt::UserRole).toInt() != scene.layers[layer].id)
            continue;
        for (int c = 0; c < top->childCount(); ++c)
            if (top->child(c)->data(0, Qt::UserRole).toInt() == which)
                return top->child(c);
    }
    return nullptr;
}

void FunctionView::rebuildList()
{
    Scene const& scene = m_document->scene();
    // Only when what it lists has changed (its names, its kinds), so that it keeps where it is scrolled to.
    QStringList shape;
    for (int id : std::as_const(m_loaded))
        if (int const index = scene.layerIndex(id); index >= 0)
            shape << QString::number(id) + QLatin1Char(':') + QString::number(scene.layers[index].kind) + QLatin1Char(':') + scene.layers[index].name
                         + QString::number(scene.layers[index].links.size());
    for (int id : std::as_const(m_loadedColumns))
        if (FunctionColumn const* column = scene.function(id))
            shape << QStringLiteral("f") + QString::number(id) + column->name;
    QString const signature = shape.join(QLatin1Char('|'));
    if (m_list->property("signature").toString() == signature && m_list->topLevelItemCount() == shape.size())
        return;
    m_list->setProperty("signature", signature);
    m_filling = true;
    QSignalBlocker block(m_list);
    m_list->clear();
    QVector<int> const drawn = m_graph->layersDrawn();
    for (int id : std::as_const(m_loaded)) {
        int const index = scene.layerIndex(id);
        if (index < 0)
            continue;
        Layer const& l = scene.layers[index];
        QTreeWidgetItem* const top = new QTreeWidgetItem(m_list, {l.name});
        top->setData(0, Qt::UserRole, id);
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable);
        top->setCheckState(0, drawn.contains(index) ? Qt::Checked : Qt::Unchecked);
        for (Timeline::Parameter which : kListed) {
            if (!Timeline::parameterFor(l.kind, which))
                continue;
            QPixmap swatch(10, 10);
            swatch.fill(FunctionGraph::colourOf(which));
            // (Linked to a function column: its name after the value's.)
            QString text = Timeline::parameterName(which);
            if (FunctionColumn const* column = scene.function(l.linkOf(which)))
                text += QStringLiteral(" (") + column->name + QLatin1Char(')');
            QTreeWidgetItem* const child = new QTreeWidgetItem(top, {text});
            child->setIcon(0, QIcon(swatch));
            child->setData(0, Qt::UserRole, int(which));
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setCheckState(0, m_graph->isShown(which) ? Qt::Checked : Qt::Unchecked);
        }
        top->setExpanded(true);
    }
    for (int id : std::as_const(m_loadedColumns)) {
        FunctionColumn const* column = scene.function(id);
        if (!column)
            continue;
        QTreeWidgetItem* const top = new QTreeWidgetItem(m_list, {column->name + QStringLiteral(" [") + FunctionColumn::kindName(column->kind) + QLatin1Char(']')});
        top->setData(0, Qt::UserRole, id);
        top->setData(0, Qt::UserRole + 1, true);
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable);
        top->setCheckState(0, drawn.contains(-1 - scene.functionIndex(id)) ? Qt::Checked : Qt::Unchecked);
        QPixmap swatch(10, 10);
        swatch.fill(InterfaceColours::get("functionColumn"));
        top->setIcon(0, QIcon(swatch));
    }
    m_filling = false;
}

void FunctionView::showSpot()
{
    double frame = 0.0, value = 0.0;
    bool const any = m_graph->selectedSpot(frame, value);
    m_filling = true;
    m_frameBox->setEnabled(any);
    m_valueBox->setEnabled(any);
    if (any) {
        m_frameBox->setValue(frame);
        m_valueBox->setValue(value);
    } else {
        m_frameBox->clear();
        m_valueBox->clear();
    }
    m_filling = false;
    // The buttons for the shape of a curve, only while there are points to give it to.
    for (QToolButton* button : std::as_const(m_pointButtons))
        button->setVisible(any);
}

void FunctionView::syncKinds()
{
    if (!m_actions)
        return;
    for (QString const& kind : {QStringLiteral("motion"), QStringLiteral("rotation"), QStringLiteral("scale"), QStringLiteral("other"), QStringLiteral("all")}) {
        bool all = true;
        for (Timeline::Parameter which : kindOf(kind))
            all = all && m_graph->isShown(which);
        m_actions->setChecked(QStringLiteral("FunctionShow") + kind.left(1).toUpper() + kind.mid(1), all);
    }
}

void FunctionView::onActionShowKind(QString const& kind)
{
    // All of that kind ticked; or, when they all are already, none of them.
    QVector<Timeline::Parameter> const values = kindOf(kind);
    bool all = true;
    for (Timeline::Parameter which : values)
        all = all && m_graph->isShown(which);
    for (Timeline::Parameter which : values)
        m_graph->setShown(which, !all);
    m_list->setProperty("signature", QString());
    rebuildList();
    syncKinds();
}

void FunctionView::onActionAutoLoad(bool on)
{
    m_autoLoad = on;
    if (m_actions)
        m_actions->setChecked(QStringLiteral("FunctionAutoLoad"), on);
    if (on) {
        m_loaded = selectedIds();
        loadIntoGraph();
    }
}

void FunctionView::onActionClearList()
{
    onActionAutoLoad(false);
    m_loaded.clear();
    m_loadedColumns.clear();
    loadIntoGraph();
}

void FunctionView::onActionLoadSelection()
{
    onActionAutoLoad(false);
    m_loaded = selectedIds();
    loadIntoGraph();
}

void FunctionView::onActionAddSelection()
{
    onActionAutoLoad(false);
    for (int id : selectedIds())
        if (!m_loaded.contains(id))
            m_loaded.push_back(id);
    loadIntoGraph();
}

void FunctionView::onActionRemoveSelection()
{
    onActionAutoLoad(false);
    for (int id : selectedIds())
        m_loaded.removeAll(id);
    loadIntoGraph();
}
