// Eclipse - the Database Manager: the scene database's environments, jobs and scenes, their versions, stages, locks and
// users, and scenes brought in and sent out (see model/scene/SceneDatabase.h).
#pragma once

#include "EclipseExport.h"

#include <QDialog>

#include "model/scene/SceneDatabase.h"

class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class ECLIPSE_UI_API DatabaseManager : public QDialog {
    Q_OBJECT

public:
    explicit DatabaseManager(QWidget* parent = nullptr);

    SceneDatabase& database() { return m_db; }
    void setRoot(QString const& root);
    // What is picked in the tree (empty parts where nothing of that kind is).
    QString environment() const;
    QString job() const;
    QString scene() const;
    int pickedVersion() const;     // in the versions table (0: none)
    bool select(QString const& environment, QString const& job = QString(), QString const& scene = QString());
    void refresh();
    QString lastError() const { return m_error; }

signals:
    // Open: the scene picked (at the version picked; 0: the current one).
    void openRequested(QString const& environment, QString const& job, QString const& scene, int version);

public slots:
    bool newEnvironment(QString name = QString());
    bool newJob(QString name = QString());
    bool deletePicked();
    bool makeCurrent();
    bool changeStage(QString stage = QString());
    bool setApproval(bool manual);
    bool releaseLock(bool force);
    void editUsers();
    bool exportPicked(QString folder = QString());
    bool importInto(QString file = QString(), QString name = QString());

private:
    void showScene();
    bool report(bool ok);
    SceneDatabase m_db;
    QLineEdit* m_root;
    QTreeWidget* m_tree;
    QTableWidget* m_versions;
    QTableWidget* m_locks;
    QLabel* m_info;
    QString m_error;
};
