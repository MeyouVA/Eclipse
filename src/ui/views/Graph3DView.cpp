// Eclipse - the 3D Graph view: the layers as they hang together, with where each stands in space.
#include "ui/views/Graph3DView.h"

#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"

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
    m_tree->setToolTip(tr("The layers as they hang together, with where each stands in space on this frame.\n"
                          "Depth is how far towards the camera the layer ends up, through all it hangs under.\n"
                          "Untick a layer to hide it; click one to select it."));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tree);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != 0)
            return;
        int const layer = m_document->scene().layerIndex(item->data(0, Qt::UserRole).toInt());
        if (layer >= 0)
            m_document->setLayerVisible(layer, item->checkState(0) == Qt::Checked);
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (m_rebuilding || !item)
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

void Graph3DView::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    auto number = [](double v) {
        QString text = QString::number(v, 'f', 2);
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
        return text == QLatin1String("-0") ? QStringLiteral("0") : text;
    };
    static char const* const kKinds[] = {QT_TR_NOOP("drawing"), QT_TR_NOOP("peg"), QT_TR_NOOP("camera"), QT_TR_NOOP("sound"),
                                         QT_TR_NOOP("colour-card"), QT_TR_NOOP("group"), QT_TR_NOOP("bone"), QT_TR_NOOP("quadmap")};
    QVector<int> shape;
    for (Layer const& layer : scene.layers)
        shape << layer.id << layer.parent;
    QVector<QTreeWidgetItem*> items(scene.layers.size(), nullptr);
    if (shape == m_shape) {
        QList<QTreeWidgetItem*> const all = m_tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive, 0);
        for (QTreeWidgetItem* const item : all) {
            int const l = scene.layerIndex(item->data(0, Qt::UserRole).toInt());
            if (l >= 0)
                items[l] = item;
        }
    } else {
        m_tree->clear();
        m_shape = shape;
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
        bool const placed = layer.kind != Layer::Sound && layer.kind != Layer::Group && layer.kind != Layer::ColourCard;
        item->setText(1, placed ? number(pose.x) : QString());
        item->setText(2, placed ? number(pose.y) : QString());
        item->setText(3, placed ? number(pose.z) : QString());
        item->setText(4, placed ? number(pose.rotX) : QString());
        item->setText(5, placed ? number(pose.rotY) : QString());
        item->setText(6, placed ? number(scene.depthOf(l, m_frame)) : QString());
        item->setToolTip(6, placed && scene.behindCamera(l, m_frame) ? tr("Behind the camera on this frame: not seen.") : QString());
    }
    if (m_layer >= 0 && m_layer < items.size() && items[m_layer] && m_tree->currentItem() != items[m_layer])
        m_tree->setCurrentItem(items[m_layer]);
    m_rebuilding = false;
}
