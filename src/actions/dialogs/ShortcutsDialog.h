// Eclipse - the keyboard shortcuts editor: every named shortcut by category, with the key
// it has now.  Changes apply when the dialog is accepted.
#pragma once

#include "EclipseExport.h"

#include <QDialog>
#include <QHash>
#include <QKeySequence>

class ActionManager;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class ECLIPSE_ACTIONS_API ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(ActionManager* manager, QWidget* parent = nullptr);

    void accept() override;
    // Shortcut sets: every key as it is in the editor, kept under a name (in the settings) or in a file, and brought back.
    // A set is lines of "id=key".
    QStringList setText() const;
    void applySetText(QStringList const& lines);
    bool saveSetFile(QString const& path) const;
    bool loadSetFile(QString const& path);
    static QStringList setNames();
    void saveSet(QString const& name);
    bool useSet(QString const& name);
    static void deleteSet(QString const& name);
    QKeySequence editedKey(QString const& id) const { return m_keys.value(id); }

private:
    void filter(QString const& text);
    void currentChanged();
    void setKey(QKeySequence const& key);
    void restoreAll();
    void refreshItem(QTreeWidgetItem* item);
    // The other command already using `key`, or an empty string.
    QString conflict(QString const& id, QKeySequence const& key) const;

    ActionManager* m_manager;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    QKeySequenceEdit* m_edit;
    QLabel* m_warning;
    QHash<QString, QKeySequence> m_keys;  // the edited copy
    class QComboBox* m_sets = nullptr;
    void refreshAll();
};
