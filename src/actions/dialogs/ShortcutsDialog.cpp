#include "actions/dialogs/ShortcutsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"

ShortcutsDialog::ShortcutsDialog(ActionManager* manager, QWidget* parent) : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(560, 560);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Command"), tr("Shortcut")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->setRootIsDecorated(true);
    QHash<QString, QTreeWidgetItem*> categories;
    for (QString const& name : m_manager->shortcutCategories()) {
        QTreeWidgetItem* const item = new QTreeWidgetItem(m_tree, {name});
        QFont bold = item->font(0);
        bold.setBold(true);
        item->setFont(0, bold);
        item->setFlags(Qt::ItemIsEnabled);
        categories.insert(name, item);
    }
    for (ActionManager::Shortcut const& s : m_manager->shortcuts()) {
        m_keys.insert(s.id, s.key);
        QTreeWidgetItem* const item = new QTreeWidgetItem(categories.value(s.category), {s.text});
        item->setData(0, Qt::UserRole, s.id);
        refreshItem(item);
    }
    m_tree->expandAll();

    m_edit = new QKeySequenceEdit(this);
    m_edit->setMaximumSequenceLength(1);
    m_edit->setEnabled(false);
    QPushButton* const clear = new QPushButton(tr("Clear"), this);
    QPushButton* const def = new QPushButton(tr("Default"), this);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Press the key:"), this));
    row->addWidget(m_edit, 1);
    row->addWidget(clear);
    row->addWidget(def);

    m_warning = new QLabel(this);
    m_warning->setStyleSheet(QStringLiteral("color: #f0a040;"));
    m_warning->setMinimumHeight(m_warning->fontMetrics().height());

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* const restore = buttons->addButton(tr("Restore All Defaults"), QDialogButtonBox::ResetRole);

    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_tree, 1);
    layout->addLayout(row);
    layout->addWidget(m_warning);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ShortcutsDialog::filter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ShortcutsDialog::currentChanged);
    connect(m_edit, &QKeySequenceEdit::editingFinished, this, [this] { setKey(m_edit->keySequence()); });
    connect(clear, &QPushButton::clicked, this, [this] { setKey(QKeySequence()); });
    connect(def, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem* const item = m_tree->currentItem();
        if (!item)
            return;
        QString const id = item->data(0, Qt::UserRole).toString();
        for (ActionManager::Shortcut const& s : m_manager->shortcuts())
            if (s.id == id)
                setKey(s.defaultKey);
    });
    connect(restore, &QPushButton::clicked, this, &ShortcutsDialog::restoreAll);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ShortcutsDialog::reject);
}

void ShortcutsDialog::refreshItem(QTreeWidgetItem* item)
{
    item->setText(1, m_keys.value(item->data(0, Qt::UserRole).toString()).toString(QKeySequence::NativeText));
}

QString ShortcutsDialog::conflict(QString const& id, QKeySequence const& key) const
{
    if (key.isEmpty())
        return QString();
    for (ActionManager::Shortcut const& s : m_manager->shortcuts())
        if (s.id != id && m_keys.value(s.id) == key)
            return s.text;
    return QString();
}

void ShortcutsDialog::filter(QString const& text)
{
    for (int c = 0; c < m_tree->topLevelItemCount(); ++c) {
        QTreeWidgetItem* const category = m_tree->topLevelItem(c);
        int visible = 0;
        for (int i = 0; i < category->childCount(); ++i) {
            QTreeWidgetItem* const item = category->child(i);
            bool const match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                               || item->text(1).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible += match;
        }
        category->setHidden(visible == 0);
    }
}

void ShortcutsDialog::currentChanged()
{
    QTreeWidgetItem* const item = m_tree->currentItem();
    QString const id = item ? item->data(0, Qt::UserRole).toString() : QString();
    m_edit->setEnabled(!id.isEmpty());
    m_edit->setKeySequence(m_keys.value(id));
    QString const other = conflict(id, m_keys.value(id));
    m_warning->setText(other.isEmpty() ? QString() : tr("Also used by: %1").arg(other));
}

void ShortcutsDialog::setKey(QKeySequence const& key)
{
    QTreeWidgetItem* const item = m_tree->currentItem();
    if (!item || item->data(0, Qt::UserRole).toString().isEmpty())
        return;
    m_keys[item->data(0, Qt::UserRole).toString()] = key;
    refreshItem(item);
    currentChanged();
}

void ShortcutsDialog::restoreAll()
{
    for (ActionManager::Shortcut const& s : m_manager->shortcuts())
        m_keys[s.id] = s.defaultKey;
    for (int c = 0; c < m_tree->topLevelItemCount(); ++c)
        for (int i = 0; i < m_tree->topLevelItem(c)->childCount(); ++i)
            refreshItem(m_tree->topLevelItem(c)->child(i));
    currentChanged();
}

void ShortcutsDialog::accept()
{
    // setShortcut edits the list being walked: go by a copy of the ids.
    QStringList ids;
    for (ActionManager::Shortcut const& s : m_manager->shortcuts())
        if (s.key != m_keys.value(s.id))
            ids << s.id;
    for (QString const& id : std::as_const(ids))
        m_manager->setShortcut(id, m_keys.value(id));
    QDialog::accept();
}
