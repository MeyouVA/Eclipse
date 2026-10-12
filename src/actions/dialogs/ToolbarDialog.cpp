#include "actions/dialogs/ToolbarDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"

ToolbarDialog::ToolbarDialog(ActionManager* manager, QString const& toolbarId, QWidget* parent)
    : QDialog(parent), m_manager(manager), m_toolbar(toolbarId)
{
    setWindowTitle(tr("Customize Toolbar: %1").arg(m_manager->toolbarText(toolbarId)));
    resize(560, 420);

    m_available = new QListWidget(this);
    m_shown = new QListWidget(this);
    for (QListWidget* list : {m_available, m_shown})
        list->setIconSize(QSize(20, 20));

    QPushButton* const add = new QPushButton(QStringLiteral(">"), this);
    add->setToolTip(tr("Add the selected tool to the toolbar"));
    QPushButton* const remove = new QPushButton(QStringLiteral("<"), this);
    remove->setToolTip(tr("Take the selected tool off the toolbar"));
    QPushButton* const up = new QPushButton(tr("Up"), this);
    QPushButton* const down = new QPushButton(tr("Down"), this);
    QPushButton* const separator = new QPushButton(tr("Separator"), this);
    separator->setToolTip(tr("Add a dividing line after the selected tool"));

    QVBoxLayout* const middle = new QVBoxLayout;
    middle->addStretch();
    middle->addWidget(add);
    middle->addWidget(remove);
    middle->addStretch();
    QVBoxLayout* const order = new QVBoxLayout;
    order->addStretch();
    order->addWidget(up);
    order->addWidget(down);
    order->addWidget(separator);
    order->addStretch();

    QVBoxLayout* const left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Available Tools"), this));
    left->addWidget(m_available);
    QVBoxLayout* const right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Toolbar"), this));
    right->addWidget(m_shown);
    QHBoxLayout* const lists = new QHBoxLayout;
    lists->addLayout(left, 1);
    lists->addLayout(middle);
    lists->addLayout(right, 1);
    lists->addLayout(order);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* const defaults = buttons->addButton(tr("Default"), QDialogButtonBox::ResetRole);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addLayout(lists, 1);
    layout->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, &ToolbarDialog::addSelected);
    connect(remove, &QPushButton::clicked, this, &ToolbarDialog::removeSelected);
    connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelected(+1); });
    connect(separator, &QPushButton::clicked, this, &ToolbarDialog::addSeparator);
    connect(defaults, &QPushButton::clicked, this, &ToolbarDialog::restoreDefault);
    connect(m_available, &QListWidget::itemDoubleClicked, this, &ToolbarDialog::addSelected);
    connect(m_shown, &QListWidget::itemDoubleClicked, this, &ToolbarDialog::removeSelected);
    connect(buttons, &QDialogButtonBox::accepted, this, &ToolbarDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ToolbarDialog::reject);

    fill(m_manager->toolbarEntries(m_toolbar));
}

QListWidgetItem* ToolbarDialog::makeItem(QString const& entry) const
{
    QListWidgetItem* const item = new QListWidgetItem(m_manager->toolbarEntryIcon(m_toolbar, entry), m_manager->toolbarEntryText(m_toolbar, entry));
    item->setData(Qt::UserRole, entry);
    return item;
}

void ToolbarDialog::fill(QStringList const& shown)
{
    m_available->clear();
    m_shown->clear();
    for (QString const& entry : shown)
        m_shown->addItem(makeItem(entry));
    for (QString const& entry : m_manager->toolbarAvailable(m_toolbar))
        if (!shown.contains(entry))
            m_available->addItem(makeItem(entry));
}

QStringList ToolbarDialog::entries() const
{
    QStringList out;
    for (int i = 0; i < m_shown->count(); ++i)
        out << m_shown->item(i)->data(Qt::UserRole).toString();
    return out;
}

void ToolbarDialog::addSelected()
{
    QListWidgetItem* const item = m_available->takeItem(m_available->currentRow());
    if (!item)
        return;
    // After the tool picked on the right, or at the end.
    int const at = m_shown->currentRow() >= 0 ? m_shown->currentRow() + 1 : m_shown->count();
    m_shown->insertItem(at, item);
    m_shown->setCurrentRow(at);
}

void ToolbarDialog::removeSelected()
{
    QListWidgetItem* const item = m_shown->takeItem(m_shown->currentRow());
    if (!item)
        return;
    if (item->data(Qt::UserRole).toString() == ActionManager::kSeparatorEntry)
        delete item;  // a separator is not a tool: it just goes
    else
        m_available->addItem(item);
}

void ToolbarDialog::moveSelected(int delta)
{
    int const row = m_shown->currentRow();
    int const to = row + delta;
    if (row < 0 || to < 0 || to >= m_shown->count())
        return;
    m_shown->insertItem(to, m_shown->takeItem(row));
    m_shown->setCurrentRow(to);
}

void ToolbarDialog::addSeparator()
{
    int const at = m_shown->currentRow() >= 0 ? m_shown->currentRow() + 1 : m_shown->count();
    m_shown->insertItem(at, makeItem(ActionManager::kSeparatorEntry));
    m_shown->setCurrentRow(at);
}

void ToolbarDialog::restoreDefault()
{
    fill(m_manager->toolbarDefaultEntries(m_toolbar));
}

void ToolbarDialog::accept()
{
    m_manager->setToolbarEntries(m_toolbar, entries());
    QDialog::accept();
}
