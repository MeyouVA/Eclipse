// Eclipse - customising a toolbar: the commands it could show on the left, the ones it
// shows, in order, on the right.
#pragma once

#include "EclipseExport.h"

#include <QDialog>

class ActionManager;
class QListWidget;
class QListWidgetItem;

class ECLIPSE_ACTIONS_API ToolbarDialog : public QDialog {
    Q_OBJECT

public:
    ToolbarDialog(ActionManager* manager, QString const& toolbarId, QWidget* parent = nullptr);

    void accept() override;

    // What the buttons do, also for tests.
    void addSelected();
    void removeSelected();
    void moveSelected(int delta);
    void addSeparator();
    void restoreDefault();
    QStringList entries() const;

private:
    void fill(QStringList const& shown);
    QListWidgetItem* makeItem(QString const& entry) const;

    ActionManager* m_manager;
    QString m_toolbar;
    QListWidget* m_available;
    QListWidget* m_shown;
};
