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
};
