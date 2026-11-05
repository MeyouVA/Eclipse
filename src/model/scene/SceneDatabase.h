// Eclipse - the scene database: scenes kept for a team in a shared folder, by environment and job.
//
//   <root>/users.json                          the users, and what each may do
//   <root>/<environment>/<job>/job.json        the job's approval policy and stage
//   <root>/<environment>/<job>/<scene>/scene.json        its versions (number, user, time, comment), the current one, its stage
//   <root>/<environment>/<job>/<scene>/versions/v0001.eclipse ...
//   <root>/<environment>/<job>/<scene>/locks/<what>.lock  who has the rights to modify it (the scene, a drawing, a palette)
//   <root>/.trash/...                          what was deleted
//
// Nothing runs on a server: whoever can reach the folder works on it, and the lock files say who has what.
#pragma once

#include "EclipseExport.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

class Scene;

class ECLIPSE_MODEL_API SceneDatabase {
public:
    explicit SceneDatabase(QString root = QString());
    QString root() const { return m_root; }
    // Whether the folder is there (made, with `create`).
    bool open(bool create, QString* error = nullptr);

    // Who is working: the user name (Preferences, or the computer's).
    static QString currentUser();
    static QString host();

    QStringList environments() const;
    bool createEnvironment(QString const& name, QString* error = nullptr);
    bool deleteEnvironment(QString const& name, QString* error = nullptr);
    QStringList jobs(QString const& environment) const;
    bool createJob(QString const& environment, QString const& job, QString* error = nullptr);
    bool deleteJob(QString const& environment, QString const& job, QString* error = nullptr);
    QStringList scenes(QString const& environment, QString const& job) const;
    // A new scene, `start` its first version.
    bool createScene(QString const& environment, QString const& job, QString const& name, Scene const& start, QString* error = nullptr);
    // Into the trash (not when someone else has the rights to modify it).
    bool deleteScene(QString const& environment, QString const& job, QString const& name, QString* error = nullptr);
    bool renameScene(QString const& environment, QString const& job, QString const& name, QString const& to, QString* error = nullptr);
    QString sceneFolder(QString const& environment, QString const& job, QString const& name) const;

    struct Version {
        int number = 0;
        QString user;
        QDateTime when;
        QString comment;
    };
    QVector<Version> versions(QString const& environment, QString const& job, QString const& scene) const;
    int currentVersion(QString const& environment, QString const& job, QString const& scene) const;
    bool setCurrentVersion(QString const& environment, QString const& job, QString const& scene, int number, QString* error = nullptr);
    // The file of a version (0: the current one); empty when there is none.
    QString versionFile(QString const& environment, QString const& job, QString const& scene, int number = 0) const;
    // A new version (made the current one); its number, or 0 (`error` says why: someone else has the rights to modify it).
    int saveVersion(QString const& environment, QString const& job, QString const& scene, Scene const& scene_, QString const& comment, QString* error = nullptr);
    bool deleteVersion(QString const& environment, QString const& job, QString const& scene, int number, QString* error = nullptr);

    // The stage a scene (or a job, with an empty scene) is at ("Layout", "Animation"...).
    QString stage(QString const& environment, QString const& job, QString const& scene) const;
    bool setStage(QString const& environment, QString const& job, QString const& scene, QString const& stage, QString* error = nullptr);
    // A job's approval policy: new versions taken as they are saved (automatic), or kept for approval (manual).
    bool manualApproval(QString const& environment, QString const& job) const;
    bool setManualApproval(QString const& environment, QString const& job, bool manual, QString* error = nullptr);

    // ---- rights to modify ----
    struct Lock {
        QString user, host;
        QDateTime when;
        bool held() const { return !user.isEmpty(); }
    };
    // `what`: "scene", "drawing:<layer id>:<drawing>", "palette:<name>", "palettes".
    Lock lockOf(QString const& environment, QString const& job, QString const& scene, QString const& what) const;
    QStringList locks(QString const& environment, QString const& job, QString const& scene) const;
    // Got for the current user; false when someone else has them (`error` says who).
    bool getRights(QString const& environment, QString const& job, QString const& scene, QString const& what, QString* error = nullptr);
    // Given up (only the current user's own, unless `force`).
    bool releaseRights(QString const& environment, QString const& job, QString const& scene, QString const& what, bool force, QString* error = nullptr);
    bool lockedByOther(QString const& environment, QString const& job, QString const& scene, QString const& what) const;

    // ---- users ----
    struct User {
        QString login, firstName, lastName;
        int type = 0;       // 0 a user, 1 an administrator
    };
    QVector<User> users() const;
    bool addUser(User const& user, QString* error = nullptr);
    bool modifyUser(QString const& login, User const& user, QString* error = nullptr);
    bool removeUser(QString const& login, QString* error = nullptr);
    // Whether the current user may manage the database (an administrator, or anyone while there are no users).
    bool isAdministrator() const;

    // ---- moving scenes in and out ----
    // The current version, with the files it uses, into `folder` (a scene file and its files beside it).
    bool exportScene(QString const& environment, QString const& job, QString const& scene, QString const& folder, QString* error = nullptr);
    // A scene file (and the files it uses) brought in as a new scene, or as a new version of one of the same name.
    bool importScene(QString const& file, QString const& environment, QString const& job, QString const& name, QString* error = nullptr);

private:
    QString jobFolder(QString const& environment, QString const& job) const;
    QString m_root;
};
