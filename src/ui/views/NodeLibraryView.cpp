// Eclipse - the Node Library view: the kinds of layer and effect there are, to add from.
#include "ui/views/NodeLibraryView.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"

NodeLibraryView::NodeLibraryView(QWidget* parent) : QWidget(parent)
{
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("NodeLibrarySearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    // (A list whose items, dragged out, say what they are - to drop in the Node View.)
    class Dragged : public QListWidget {
    public:
        using QListWidget::QListWidget;

    protected:
        QMimeData* mimeData(QList<QListWidgetItem*> const& items) const override
        {
            QString const what = items.isEmpty() ? QString() : items.first()->data(Qt::UserRole).toString();
            if (what.isEmpty())
                return nullptr;
            QMimeData* const data = new QMimeData;
            data->setData(QLatin1String(NodeLibraryView::mimeType()), what.toUtf8());
            return data;
        }
    };
    m_list = new Dragged(this);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setObjectName(QStringLiteral("NodeLibraryList"));
    struct Entry {
        char const* name;
        char const* what;
        char const* about;
        char const* category;
    };
    static Entry const kLayers[] = {
        {QT_TR_NOOP("Drawing"), "InsertDrawing", QT_TR_NOOP("A layer of drawings"), QT_TR_NOOP("Image")},
        {QT_TR_NOOP("Peg"), "InsertPeg", QT_TR_NOOP("Moves the layers attached to it"), QT_TR_NOOP("Move")},
        {QT_TR_NOOP("Camera"), "InsertCamera", QT_TR_NOOP("What the scene is seen through"), QT_TR_NOOP("Move")},
        {QT_TR_NOOP("Colour-Card"), "InsertColourCard", QT_TR_NOOP("Fills the frame with one colour"), QT_TR_NOOP("Image")},
        {QT_TR_NOOP("Group"), "InsertGroup", QT_TR_NOOP("Holds other layers"), QT_TR_NOOP("Group")},
        {QT_TR_NOOP("Bone"), "InsertBone", QT_TR_NOOP("A peg with a length; what hangs on it turns with it"), QT_TR_NOOP("Move")},
        {QT_TR_NOOP("Quadmap"), "InsertQuadmap", QT_TR_NOOP("Pulls what is attached to it by four corners"), QT_TR_NOOP("Deformation")},
        {QT_TR_NOOP("Sound"), "ImportSound", QT_TR_NOOP("A sound file, played from a frame on"), QT_TR_NOOP("Sound")},
        {QT_TR_NOOP("Parent Peg"), "InsertParentPeg", QT_TR_NOOP("A peg put over the current layer, which then hangs from it"), QT_TR_NOOP("Move")},
        {QT_TR_NOOP("Clone"), "CloneLayer", QT_TR_NOOP("Shows the current layer's drawings, with timing of its own"), QT_TR_NOOP("Image")},
        {QT_TR_NOOP("Clone with Timing"), "CloneLayerTiming", QT_TR_NOOP("Shows the current layer's drawings with its timing too"), QT_TR_NOOP("Image")},
        {QT_TR_NOOP("Synced Drawing"), "AddSyncedLayer", QT_TR_NOOP("A drawing layer of its own that keeps the current layer's timing"), QT_TR_NOOP("Image")},
        {QT_TR_NOOP("Symbol"), "CreateEmptySymbol", QT_TR_NOOP("A symbol: layers kept together, shown by one layer"), QT_TR_NOOP("Group")},
        {QT_TR_NOOP("Envelope Deformer"), "CreateEnvelope", QT_TR_NOOP("An outline round what is selected that pulls the artwork about"), QT_TR_NOOP("Deformation")},
        {QT_TR_NOOP("Write"), "WriteOutputs", QT_TR_NOOP("A render kept with the scene: images or a movie, made with Render Write Nodes"), QT_TR_NOOP("Output")},
    };
    static Entry const kEffects[] = {
        {QT_TR_NOOP("Blur"), "effect:0", QT_TR_NOOP("Softens the layer"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Tint"), "effect:1", QT_TR_NOOP("Colours the layer towards one colour"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Cutter"), "effect:2", QT_TR_NOOP("Cuts the layer by the drawing of another"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Glow"), "effect:3", QT_TR_NOOP("A soft light round the layer"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Shadow"), "effect:4", QT_TR_NOOP("A shadow cast by the layer"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Particles"), "effect:5", QT_TR_NOOP("The layer's drawing thrown out again and again: sparks, rain, smoke"), QT_TR_NOOP("Particle")},
        {QT_TR_NOOP("Transparency"), "effect:6", QT_TR_NOOP("Makes the layer see-through"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Directional Blur"), "effect:7", QT_TR_NOOP("Smears the layer one way, as if it moved fast"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Hue-Saturation"), "effect:8", QT_TR_NOOP("Turns the colours round the wheel, richer or greyer, lighter or darker"), QT_TR_NOOP("Filter")},
        {QT_TR_NOOP("Brightness-Contrast"), "effect:9", QT_TR_NOOP("Makes the layer brighter or darker, with more or less contrast"), QT_TR_NOOP("Filter")},
    };
    auto heading = [this](QString const& text) {
        QListWidgetItem* const item = new QListWidgetItem(text, m_list);
        item->setFlags(Qt::NoItemFlags);
        QFont bold = item->font();
        bold.setBold(true);
        item->setFont(bold);
        item->setForeground(QColor(170, 170, 170));
    };
    auto entry = [this](Entry const& e) {
        QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("    ") + tr(e.name), m_list);
        item->setData(Qt::UserRole, QString::fromLatin1(e.what));
        item->setToolTip(tr(e.about));
        item->setData(Qt::UserRole + 1, tr(e.category));
    };
    heading(tr("Layers"));
    for (Entry const& e : kLayers)
        entry(e);
    heading(tr("Effects (added to the current layer)"));
    for (Entry const& e : kEffects)
        entry(e);
    QPushButton* const addButton = new QPushButton(tr("Add"), this);
    addButton->setObjectName(QStringLiteral("NodeLibraryAdd"));
    addButton->setToolTip(tr("Adds what is chosen to the scene. A double click does the same."));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    // The kinds there are, down the left: one chosen shows only what is of that kind.
    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("NodeLibraryCategories"));
    m_categories->addItems({tr("All"), tr("Deformation"), tr("Filter"), tr("Group"), tr("Image"), tr("Move"), tr("Sound"), tr("Output"), tr("Particle")});
    m_categories->setCurrentRow(0);
    m_categories->setFixedWidth(104);
    QHBoxLayout* const lists = new QHBoxLayout;
    lists->setSpacing(4);
    lists->addWidget(m_categories);
    lists->addWidget(m_list, 1);
    layout->addWidget(m_search);
    layout->addLayout(lists, 1);
    layout->addWidget(addButton, 0, Qt::AlignRight);
    connect(m_search, &QLineEdit::textChanged, this, &NodeLibraryView::filter);
    connect(m_categories, &QListWidget::currentRowChanged, this, [this] { filter(m_search->text()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { add(item->data(Qt::UserRole).toString()); });
    connect(addButton, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* const item = m_list->currentItem())
            add(item->data(Qt::UserRole).toString());
    });
}

void NodeLibraryView::filter(QString const& text)
{
    // The headings stay; of the rest, those whose name or description has the words.
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* const item = m_list->item(i);
        if (item->data(Qt::UserRole).toString().isEmpty())
            continue;
        bool const worded = text.trimmed().isEmpty() || item->text().contains(text.trimmed(), Qt::CaseInsensitive)
                            || item->toolTip().contains(text.trimmed(), Qt::CaseInsensitive);
        bool const kind = m_categories->currentRow() <= 0 || item->data(Qt::UserRole + 1).toString() == m_categories->currentItem()->text();
        item->setHidden(!(worded && kind));
    }
}

void NodeLibraryView::add(QString const& what)
{
    if (what.isEmpty())
        return;
    if (what.startsWith(QLatin1String("effect:")))
        emit effectRequested(what.mid(7).toInt());
    else
        emit actionRequested(what);
}
