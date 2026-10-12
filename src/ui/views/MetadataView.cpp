// Eclipse - the Metadata view: names and values kept with the scene or with a layer.
#include "ui/views/MetadataView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "ui/canvas/Canvas.h"

MetadataView::MetadataView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_of = new QComboBox(this);
    m_of->setObjectName(QStringLiteral("MetadataOf"));
    m_of->addItems({tr("The scene"), tr("The current layer")});
    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("MetadataTable"));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Value")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    QPushButton* const add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("MetadataAdd"));
    QPushButton* const remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("MetadataRemove"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_of);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    connect(m_of, &QComboBox::currentIndexChanged, this, &MetadataView::rebuild);
    connect(add, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const of = subject();
        if (of >= scene.layers.size())
            return;
        QVector<QPair<QString, QString>> pairs = of < 0 ? scene.metadata : scene.layers[of].metadata;
        // A name of its own, to be typed over.
        QString name = tr("name");
        for (int n = 2; std::any_of(pairs.begin(), pairs.end(), [&](QPair<QString, QString> const& p) { return p.first == name; }); ++n)
            name = tr("name %1").arg(n);
        pairs.push_back({name, QString()});
        m_document->setMetadata(of, pairs, tr("Add Metadata"));
        m_table->setCurrentCell(int(pairs.size()) - 1, 0);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const of = subject(), row = m_table->currentRow();
        if (of >= scene.layers.size())
            return;
        QVector<QPair<QString, QString>> pairs = of < 0 ? scene.metadata : scene.layers[of].metadata;
        if (row < 0 || row >= pairs.size())
            return;
        pairs.removeAt(row);
        m_document->setMetadata(of, pairs, tr("Remove Metadata"));
    });
    connect(m_table, &QTableWidget::cellChanged, this, [this] {
        if (!m_rebuilding)
            store();
    });
    connect(m_document, &Document::changed, this, &MetadataView::rebuild);
    rebuild();
}

int MetadataView::subject() const
{
    return m_of->currentIndex() == 0 ? -1 : m_layer;
}

void MetadataView::setCurrentCell(int layer, int)
{
    if (layer == m_layer)
        return;
    m_layer = layer;
    if (m_of->currentIndex() == 1)
        rebuild();
}

void MetadataView::rebuild()
{
    Scene const& scene = m_document->scene();
    int const of = subject();
    m_rebuilding = true;
    int const row = m_table->currentRow(), column = m_table->currentColumn();
    m_table->setRowCount(0);
    if (of < scene.layers.size()) {
        QVector<QPair<QString, QString>> const& pairs = of < 0 ? scene.metadata : scene.layers[of].metadata;
        m_table->setRowCount(int(pairs.size()));
        for (int i = 0; i < pairs.size(); ++i) {
            m_table->setItem(i, 0, new QTableWidgetItem(pairs[i].first));
            m_table->setItem(i, 1, new QTableWidgetItem(pairs[i].second));
        }
        if (row >= 0 && row < pairs.size())
            m_table->setCurrentCell(row, std::max(column, 0));
    }
    m_rebuilding = false;
}

void MetadataView::store()
{
    Scene const& scene = m_document->scene();
    int const of = subject();
    if (of >= scene.layers.size())
        return;
    QVector<QPair<QString, QString>> pairs;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QString const name = m_table->item(i, 0) ? m_table->item(i, 0)->text().trimmed() : QString();
        // Something without a name cannot be asked for: it keeps the name it had.
        QVector<QPair<QString, QString>> const& was = of < 0 ? scene.metadata : scene.layers[of].metadata;
        pairs.push_back({name.isEmpty() && i < was.size() ? was[i].first : name, m_table->item(i, 1) ? m_table->item(i, 1)->text() : QString()});
    }
    m_document->setMetadata(of, pairs, tr("Change Metadata"));
    rebuild();
}
