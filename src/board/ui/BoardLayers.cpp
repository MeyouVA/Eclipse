#include "board/ui/BoardLayers.h"
#include "board/ui/BoardLayerPreview.h"
#include "board/model/BoardDocument.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
class LayerFlagDelegate : public QStyledItemDelegate {
public:
    LayerFlagDelegate(QIcon on, QIcon off, QObject* parent)
        : QStyledItemDelegate(parent), m_on(std::move(on)), m_off(std::move(off)) {}

    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override
    {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        item.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        item.widget->style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        QRect const iconRect(item.rect.center() - QPoint(6, 6), QSize(14, 14));
        bool const checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        auto const& icon = checked ? m_on : m_off;
        icon.paint(painter, iconRect, Qt::AlignCenter, checked ? QIcon::Normal : QIcon::Disabled);
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model, QStyleOptionViewItem const& option, QModelIndex const& index) override
    {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto const* mouse = static_cast<QMouseEvent const*>(event);
            if (mouse->button() != Qt::LeftButton || !option.rect.contains(mouse->position().toPoint()))
                return false;
        } else if (event->type() == QEvent::KeyPress) {
            auto const* key = static_cast<QKeyEvent const*>(event);
            if (key->key() != Qt::Key_Space && key->key() != Qt::Key_Select)
                return false;
        } else {
            return false;
        }
        if (!(index.flags() & Qt::ItemIsUserCheckable) || !(index.flags() & Qt::ItemIsEnabled))
            return false;
        return model->setData(index, index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked,
                              Qt::CheckStateRole);
    }

private:
    QIcon m_on;
    QIcon m_off;
};
}

BoardLayers::BoardLayers(BoardDocument* document, QWidget* parent)
    : QWidget(parent), m_document(document), m_list(new QTreeWidget(this)), m_add(new QToolButton(this)), m_layerType(new QLabel(this))
{
    m_list->setObjectName(QStringLiteral("BoardLayerList"));
    m_list->setHeaderLabels({tr("Layer"), tr("Type"), tr("Visible"), tr("Locked")});
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setIconSize(QSize(72, 42));
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setMinimumSectionSize(18);
    m_list->hideColumn(1);
    m_list->header()->moveSection(m_list->header()->visualIndex(2), 0);
    for (int column : {2, 3}) {
        m_list->header()->setSectionResizeMode(column, QHeaderView::Fixed);
        m_list->setColumnWidth(column, 21);
    }

    QString const icons = ActionManager::defaultDirectory();
    auto const eye = Theme::icon(icons, QStringLiteral("timeline/enable"));
    m_list->setItemDelegateForColumn(2, new LayerFlagDelegate(eye, eye, m_list));
    m_list->setItemDelegateForColumn(3, new LayerFlagDelegate(Theme::icon(icons, QStringLiteral("timeline/lock")),
        Theme::icon(icons, QStringLiteral("timeline/unlock")), m_list));
    auto* tools = new QToolBar(this);
    tools->setObjectName(QStringLiteral("BoardLayerTools"));
    tools->setIconSize(QSize(16, 16));
    auto* space = new QWidget(tools);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(space);
    m_add->setIcon(Theme::icon(icons, QStringLiteral("timeline/adddrawing")));
    m_add->setToolTip(tr("Add Layer"));
    m_add->setAccessibleName(tr("Add Layer"));
    auto* menu = new QMenu(m_add);
    menu->addAction(tr("New Vector Layer"), this, [this] { editLayer(QStringLiteral("vector")); });
    menu->addAction(tr("New Bitmap Layer"), this, [this] { editLayer(QStringLiteral("bitmap")); });
    m_add->setMenu(menu);
    m_add->setPopupMode(QToolButton::InstantPopup);
    tools->addWidget(m_add);
    auto command = [this, tools, &icons](QString const& text, char const* icon, char const* operation) {
        auto* action = tools->addAction(Theme::icon(icons, QLatin1String(icon)), text);
        connect(action, &QAction::triggered, this, [this, operation] { editLayer(QLatin1String(operation)); });
        return action;
    };
    m_rename = command(tr("Rename Layer"), "timeline/renamelayer", "rename");
    m_duplicate = command(tr("Duplicate Layer"), "timeline/duplicatelayer", "duplicate");
    m_up = command(tr("Move Layer Up"), "timeline/layerup", "up");
    m_down = command(tr("Move Layer Down"), "timeline/layerdown", "down");
    m_remove = command(tr("Delete Layer"), "timeline/deletelayer", "remove");

    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("BoardLayerHeading"));
    header->setFixedHeight(29);
    auto* heading = new QHBoxLayout(header);
    heading->setContentsMargins(3, 0, 4, 0);
    auto* layerCommands = new QToolButton(header);
    layerCommands->setText(QStringLiteral("››"));
    layerCommands->setToolTip(tr("Layer commands"));
    layerCommands->setAccessibleName(layerCommands->toolTip());
    auto* layerMenu = new QMenu(layerCommands);
    connect(layerMenu, &QMenu::aboutToShow, this, [this, layerMenu] { populateMenu(layerMenu); });
    layerCommands->setMenu(layerMenu);
    layerCommands->setPopupMode(QToolButton::InstantPopup);
    heading->addWidget(layerCommands);
    heading->addStretch();
    heading->addWidget(m_layerType);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 0, 1, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_list, 1);
    layout->addWidget(tools);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
        updateActions();
        emit layerSelected(row());
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int column) {
        if (column == 0)
            editLayer(QStringLiteral("rename"));
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column < 2)
            return;
        QScopedValueRollback<bool> const writing(m_writingFlags, true);
        m_document->setLayerFlags(m_shot, m_panel, m_list->indexOfTopLevelItem(item),
                                  item->checkState(2) == Qt::Checked, item->checkState(3) == Qt::Checked);
    });
    updateActions();
}

int BoardLayers::showPanel(int shot, int panel)
{
    if (m_writingFlags && m_shot == shot && m_panel == panel)
        return row();
    int const previous = m_shot == shot && m_panel == panel ? std::max(0, row()) : 0;
    m_shot = shot;
    m_panel = panel;
    QSignalBlocker const block(m_list);
    m_list->clear();
    if (auto const* current = m_document->panel(shot, panel))
        for (auto const& layer : current->layers) {
            auto* item = new QTreeWidgetItem({layer.name, layer.kind == BoardLayer::Kind::Bitmap ? tr("Bitmap") : tr("Vector")});
            item->setToolTip(0, tr("%1 · %2 layer").arg(layer.name, item->text(1)));
            item->setToolTip(2, tr("Toggle layer visibility"));
            item->setToolTip(3, tr("Toggle layer lock"));
            item->setCheckState(2, layer.visible ? Qt::Checked : Qt::Unchecked);
            item->setCheckState(3, layer.locked ? Qt::Checked : Qt::Unchecked);
            m_list->addTopLevelItem(item);
            updatePreview(m_list->topLevelItemCount() - 1);
        }
    m_list->setCurrentItem(m_list->topLevelItem(std::min(previous, m_list->topLevelItemCount() - 1)));
    updateActions();
    return row();
}

void BoardLayers::refreshSelectedPreview() { updatePreview(row()); }

void BoardLayers::populateMenu(QMenu* menu)
{
    menu->clear();
    menu->addActions(m_add->menu()->actions());
    menu->addSeparator();
    menu->addActions({m_rename, m_duplicate, m_up, m_down, m_remove});
}

void BoardLayers::updatePreview(int index)
{
    auto const* current = m_document->panel(m_shot, m_panel);
    if (!current || index < 0 || index >= current->layers.size())
        return;
    auto* item = m_list->topLevelItem(index);
    if (!item)
        return;
    QSize const size = m_list->iconSize();
    auto const preview = boardLayerPreview(m_document->project(), current->layers[index], size);
    QIcon icon(preview);
    icon.addPixmap(preview, QIcon::Selected);
    item->setIcon(0, icon);
    item->setSizeHint(0, QSize(0, size.height() + 4));
}

int BoardLayers::row() const { return m_list->indexOfTopLevelItem(m_list->currentItem()); }

void BoardLayers::updateActions()
{
    int const current = row();
    m_layerType->setText(current < 0 ? QString() : tr("%1 layer").arg(m_list->currentItem()->text(1)));
    m_add->setEnabled(m_document->panel(m_shot, m_panel) != nullptr);
    for (auto* action : m_add->menu()->actions())
        action->setEnabled(m_add->isEnabled());
    m_rename->setEnabled(current >= 0);
    m_duplicate->setEnabled(current >= 0);
    m_remove->setEnabled(current >= 0);
    m_up->setEnabled(current > 0);
    m_down->setEnabled(current >= 0 && current + 1 < m_list->topLevelItemCount());
}

void BoardLayers::editLayer(QString const& operation)
{
    int const shot = m_shot;
    int const panel = m_panel;
    auto const* current = m_document->panel(shot, panel);
    if (!current)
        return;
    int const layer = row();
    if (operation == QLatin1String("vector") || operation == QLatin1String("bitmap")) {
        auto const kind = operation == QLatin1String("bitmap") ? BoardLayer::Kind::Bitmap : BoardLayer::Kind::Vector;
        QString name;
        int number = 1;
        do {
            name = tr("Layer %1").arg(number++);
        } while (std::any_of(current->layers.cbegin(), current->layers.cend(), [&name](BoardLayer const& item) { return item.name == name; }));
        if (m_document->addLayer(shot, panel, name, kind, std::max(0, layer)))
            m_list->setCurrentItem(m_list->topLevelItem(std::max(0, layer)));
    } else if (layer < 0) {
        return;
    } else if (operation == QLatin1String("rename")) {
        bool accepted = false;
        QString const name = QInputDialog::getText(this, tr("Rename Layer"), tr("Layer name"),
            QLineEdit::Normal, current->layers[layer].name, &accepted);
        if (accepted)
            m_document->renameLayer(shot, panel, layer, name);
    } else if (operation == QLatin1String("duplicate")) {
        m_document->duplicateLayer(shot, panel, layer);
    } else if (operation == QLatin1String("remove")) {
        m_document->removeLayer(shot, panel, layer);
    } else {
        int const target = layer + (operation == QLatin1String("up") ? -1 : 1);
        if (m_document->moveLayer(shot, panel, layer, target))
            m_list->setCurrentItem(m_list->topLevelItem(target));
    }
}
