#include "actions/dialogs/ShortcutsDialog.h"

#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFile>
#include <QComboBox>
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

    // Keyboard shortcut sets: the keys kept under a name, or in a file, to go back to.
    QHBoxLayout* const setRow = new QHBoxLayout;
    m_sets = new QComboBox(this);
    m_sets->setObjectName(QStringLiteral("ShortcutSets"));
    m_sets->setMinimumWidth(160);
    m_sets->addItem(tr("(as they are)"));
    m_sets->addItems(setNames());
    QPushButton* const newSet = new QPushButton(tr("New Set..."), this);
    newSet->setObjectName(QStringLiteral("ShortcutSetNew"));
    newSet->setToolTip(tr("Create a new set based on the current shortcut keys."));
    QPushButton* const deleteSetButton = new QPushButton(tr("Delete Set"), this);
    deleteSetButton->setObjectName(QStringLiteral("ShortcutSetDelete"));
    deleteSetButton->setToolTip(tr("Delete the current keyboard shortcut set."));
    QPushButton* const saveFile = new QPushButton(tr("Save..."), this);
    saveFile->setToolTip(tr("Save the current keyboard shortcut set to a file."));
    QPushButton* const loadFile = new QPushButton(tr("Load..."), this);
    loadFile->setToolTip(tr("Load keyboard shortcut set from a file."));
    setRow->addWidget(new QLabel(tr("Keyboard shortcut set:"), this));
    setRow->addWidget(m_sets, 1);
    setRow->addWidget(newSet);
    setRow->addWidget(deleteSetButton);
    setRow->addWidget(saveFile);
    setRow->addWidget(loadFile);
    connect(m_sets, &QComboBox::activated, this, [this](int index) {
        if (index > 0)
            useSet(m_sets->itemText(index));
    });
    connect(newSet, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("New Keyboard Shortcut Set"), tr("Keyboard Shortcut Set Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        saveSet(name);
        if (m_sets->findText(name) < 0)
            m_sets->addItem(name);
        m_sets->setCurrentText(name);
    });
    connect(deleteSetButton, &QPushButton::clicked, this, [this] {
        int const index = m_sets->currentIndex();
        if (index <= 0)
            return;
        QString const name = m_sets->itemText(index);
        if (QMessageBox::question(this, tr("Delete Keyboard Shortcut Set"), tr("Are you sure you want to delete the keyboard shortcut set %1?").arg(name))
            != QMessageBox::Yes)
            return;
        deleteSet(name);
        m_sets->removeItem(index);
        m_sets->setCurrentIndex(0);
    });
    connect(saveFile, &QPushButton::clicked, this, [this] {
        QString const path = QFileDialog::getSaveFileName(this, tr("Save Keyboard Shortcut Set"), QStringLiteral("shortcuts.eclkeys"), tr("Shortcut Sets (*.eclkeys)"));
        if (!path.isEmpty() && !saveSetFile(path))
            QMessageBox::warning(this, tr("Save Keyboard Shortcut Set"), tr("Unable to write shortcuts in %1").arg(path));
    });
    connect(loadFile, &QPushButton::clicked, this, [this] {
        QString const path = QFileDialog::getOpenFileName(this, tr("Load Keyboard Shortcut Set"), QString(), tr("Shortcut Sets (*.eclkeys)"));
        if (!path.isEmpty() && !loadSetFile(path))
            QMessageBox::warning(this, tr("Load Keyboard Shortcut Set"), tr("%1 is not a shortcut set.").arg(path));
    });

    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addLayout(setRow);
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

void ShortcutsDialog::refreshAll()
{
    for (int c = 0; c < m_tree->topLevelItemCount(); ++c)
        for (int i = 0; i < m_tree->topLevelItem(c)->childCount(); ++i)
            refreshItem(m_tree->topLevelItem(c)->child(i));
    currentChanged();
}

QStringList ShortcutsDialog::setText() const
{
    QStringList lines;
    for (ActionManager::Shortcut const& s : m_manager->shortcuts())
        lines << s.id + QLatin1Char('=') + m_keys.value(s.id).toString(QKeySequence::PortableText);
    return lines;
}

void ShortcutsDialog::applySetText(QStringList const& lines)
{
    for (QString const& line : lines) {
        int const eq = int(line.indexOf(QLatin1Char('=')));
        if (eq <= 0)
            continue;
        QString const id = line.left(eq).trimmed();
        if (m_keys.contains(id))
            m_keys[id] = QKeySequence::fromString(line.mid(eq + 1).trimmed(), QKeySequence::PortableText);
    }
    refreshAll();
}

bool ShortcutsDialog::saveSetFile(QString const& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write("# Eclipse keyboard shortcut set: a command's id = its key\n");
    file.write(setText().join(QLatin1Char('\n')).toUtf8());
    file.write("\n");
    return true;
}

bool ShortcutsDialog::loadSetFile(QString const& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QStringList lines;
    for (QByteArray const& raw : file.readAll().split('\n'))
        if (!raw.trimmed().isEmpty() && !raw.trimmed().startsWith('#'))
            lines << QString::fromUtf8(raw);
    bool known = false;
    for (QString const& line : std::as_const(lines))
        known = known || m_keys.contains(line.section(QLatin1Char('='), 0, 0).trimmed());
    if (!known)
        return false;
    applySetText(lines);
    return true;
}

QStringList ShortcutsDialog::setNames()
{
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    settings.beginGroup(QStringLiteral("shortcutSets"));
    return settings.childKeys();
}

void ShortcutsDialog::saveSet(QString const& name)
{
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("shortcutSets/") + name, setText());
}

bool ShortcutsDialog::useSet(QString const& name)
{
    QStringList const lines = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("shortcutSets/") + name).toStringList();
    if (lines.isEmpty())
        return false;
    applySetText(lines);
    return true;
}

void ShortcutsDialog::deleteSet(QString const& name)
{
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).remove(QStringLiteral("shortcutSets/") + name);
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
