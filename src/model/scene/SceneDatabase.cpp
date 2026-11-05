// Eclipse - the scene database: a shared folder of environments, jobs and scenes (see SceneDatabase.h).
#include "model/scene/SceneDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

#include "model/scene/Scene.h"

namespace {

bool fail(QString* error, QString const& why)
{
    if (error)
        *error = why;
    return false;
}

QJsonObject readJson(QString const& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeJson(QString const& file, QJsonObject const& json, QString* error)
{
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(json).toJson()) < 0 || !f.commit())
        return fail(error, QObject::tr("Could not write %1.").arg(QDir::toNativeSeparators(file)));
    return true;
}

// A name that can be a folder: no slashes or the characters files cannot have.
bool goodName(QString const& name)
{
    static QRegularExpression const bad(QStringLiteral(R"([\\/:*?"<>|])"));
    return !name.trimmed().isEmpty() && !name.contains(bad) && !name.startsWith(QLatin1Char('.'));
}

QStringList foldersIn(QString const& folder)
{
    QStringList out = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    out.erase(std::remove_if(out.begin(), out.end(), [](QString const& n) { return n.startsWith(QLatin1Char('.')); }), out.end());
    return out;
}

// A lock's file name for what it is on.
QString lockName(QString const& what)
{
    QString name = what;
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    return name + QStringLiteral(".lock");
}

// Moved into the trash folder (under a name not yet there).
bool toTrash(QString const& root, QString const& folder, QString* error)
{
    QString const trash = QDir(root).filePath(QStringLiteral(".trash"));
    QDir().mkpath(trash);
    QString target = QDir(trash).filePath(QFileInfo(folder).fileName() + QDateTime::currentDateTime().toString(QStringLiteral("-yyyyMMdd-hhmmss")));
    for (int n = 2; QFileInfo::exists(target); ++n)
        target += QString::number(n);
    if (!QDir().rename(folder, target))
        return fail(error, QObject::tr("Could not move %1 to the trash.").arg(QDir::toNativeSeparators(folder)));
    return true;
}

}  // namespace

SceneDatabase::SceneDatabase(QString root) : m_root(std::move(root))
{
    if (m_root.isEmpty())
        m_root = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/databaseRoot")).toString();
}

bool SceneDatabase::open(bool create, QString* error)
{
    if (m_root.isEmpty())
        return fail(error, QObject::tr("No database folder is set (Preferences > General > Scene database)."));
    if (QFileInfo(m_root).isDir())
        return true;
    if (create && QDir().mkpath(m_root))
        return true;
    return fail(error, QObject::tr("Could not connect to the database: %1 cannot be reached.").arg(QDir::toNativeSeparators(m_root)));
}

QString SceneDatabase::currentUser()
{
    QString const set = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/databaseUser")).toString().trimmed();
    if (!set.isEmpty())
        return set;
    QProcessEnvironment const env = QProcessEnvironment::systemEnvironment();
    QString const name = env.value(QStringLiteral("USERNAME"), env.value(QStringLiteral("USER")));
    return name.isEmpty() ? QStringLiteral("user") : name;
}

QString SceneDatabase::host()
{
    return QSysInfo::machineHostName();
}

QString SceneDatabase::jobFolder(QString const& environment, QString const& job) const
{
    return QDir(QDir(m_root).filePath(environment)).filePath(job);
}

QString SceneDatabase::sceneFolder(QString const& environment, QString const& job, QString const& name) const
{
    return QDir(jobFolder(environment, job)).filePath(name);
}

// ---- environments, jobs, scenes ----

QStringList SceneDatabase::environments() const
{
    return foldersIn(m_root);
}

bool SceneDatabase::createEnvironment(QString const& name, QString* error)
{
    if (!goodName(name) || !open(true, error))
        return fail(error, error && !error->isEmpty() ? *error : QObject::tr("Could not create environment %1").arg(name));
    if (environments().contains(name))
        return fail(error, QObject::tr("The environment %1 is already there.").arg(name));
    if (!QDir(m_root).mkdir(name))
        return fail(error, QObject::tr("Could not create environment %1").arg(name));
    return true;
}

bool SceneDatabase::deleteEnvironment(QString const& name, QString* error)
{
    if (!environments().contains(name))
        return fail(error, QObject::tr("Could not find environment %1").arg(name));
    if (!jobs(name).isEmpty())
        return fail(error, QObject::tr("Could not delete environment %1 as it still contains jobs.").arg(name));
    if (!QDir(m_root).rmdir(name))
        return fail(error, QObject::tr("Could not delete environment %1").arg(name));
    return true;
}

QStringList SceneDatabase::jobs(QString const& environment) const
{
    return environment.isEmpty() ? QStringList() : foldersIn(QDir(m_root).filePath(environment));
}

bool SceneDatabase::createJob(QString const& environment, QString const& job, QString* error)
{
    if (!environments().contains(environment))
        return fail(error, QObject::tr("Could not create job as environment %1 does not exist.").arg(environment));
    if (!goodName(job) || jobs(environment).contains(job) || !QDir(QDir(m_root).filePath(environment)).mkdir(job))
        return fail(error, QObject::tr("Could not create job %1 in environment %2.").arg(job, environment));
    return true;
}

bool SceneDatabase::deleteJob(QString const& environment, QString const& job, QString* error)
{
    if (!jobs(environment).contains(job))
        return fail(error, QObject::tr("Could not delete job %1").arg(job));
    if (!scenes(environment, job).isEmpty())
        return fail(error, QObject::tr("Could not delete job %1 as it still contains scenes.").arg(job));
    QFile::remove(QDir(jobFolder(environment, job)).filePath(QStringLiteral("job.json")));
    if (!QDir(QDir(m_root).filePath(environment)).rmdir(job))
        return fail(error, QObject::tr("Could not delete job %1").arg(job));
    return true;
}

QStringList SceneDatabase::scenes(QString const& environment, QString const& job) const
{
    QStringList out;
    if (environment.isEmpty() || job.isEmpty())
        return out;
    for (QString const& name : foldersIn(jobFolder(environment, job)))
        if (QFileInfo::exists(QDir(sceneFolder(environment, job, name)).filePath(QStringLiteral("scene.json"))))
            out << name;
    return out;
}

bool SceneDatabase::createScene(QString const& environment, QString const& job, QString const& name, Scene const& start, QString* error)
{
    if (!jobs(environment).contains(job))
        return fail(error, QObject::tr("Could not find job %1 in environment %2.").arg(job, environment));
    if (!goodName(name))
        return fail(error, QObject::tr("%1 cannot be the name of a scene.").arg(name));
    if (scenes(environment, job).contains(name))
        return fail(error, QObject::tr("There is already a scene %1 in job %2.").arg(name, job));
    QString const folder = sceneFolder(environment, job, name);
    if (!QDir().mkpath(QDir(folder).filePath(QStringLiteral("versions"))))
        return fail(error, QObject::tr("Could not create the scene %1.").arg(name));
    if (!writeJson(QDir(folder).filePath(QStringLiteral("scene.json")), QJsonObject{{"current", 0}, {"versions", QJsonArray()}}, error))
        return false;
    return saveVersion(environment, job, name, start, QObject::tr("Created"), error) > 0;
}

bool SceneDatabase::deleteScene(QString const& environment, QString const& job, QString const& name, QString* error)
{
    if (!scenes(environment, job).contains(name))
        return fail(error, QObject::tr("Cannot find scene %1 in scene database.").arg(name));
    if (lockedByOther(environment, job, name, QStringLiteral("scene"))) {
        Lock const l = lockOf(environment, job, name, QStringLiteral("scene"));
        return fail(error, QObject::tr("The scene %1 is being worked on by %2 (on %3).").arg(name, l.user, l.host));
    }
    return toTrash(m_root, sceneFolder(environment, job, name), error);
}

bool SceneDatabase::renameScene(QString const& environment, QString const& job, QString const& name, QString const& to, QString* error)
{
    if (!scenes(environment, job).contains(name))
        return fail(error, QObject::tr("Cannot find scene %1 in scene database.").arg(name));
    if (!goodName(to) || scenes(environment, job).contains(to))
        return fail(error, QObject::tr("%1 cannot be the name of a scene.").arg(to));
    if (lockedByOther(environment, job, name, QStringLiteral("scene")))
        return fail(error, QObject::tr("The scene %1 is being worked on by %2.").arg(name, lockOf(environment, job, name, QStringLiteral("scene")).user));
    if (!QDir().rename(sceneFolder(environment, job, name), sceneFolder(environment, job, to)))
        return fail(error, QObject::tr("Could not rename the scene %1.").arg(name));
    return true;
}

// ---- versions ----

QVector<SceneDatabase::Version> SceneDatabase::versions(QString const& environment, QString const& job, QString const& scene) const
{
    QVector<Version> out;
    QJsonObject const meta = readJson(QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json")));
    for (QJsonValue const& v : meta["versions"].toArray()) {
        QJsonObject const o = v.toObject();
        out.push_back(Version{o["number"].toInt(), o["user"].toString(), QDateTime::fromString(o["when"].toString(), Qt::ISODate), o["comment"].toString()});
    }
    return out;
}

int SceneDatabase::currentVersion(QString const& environment, QString const& job, QString const& scene) const
{
    QJsonObject const meta = readJson(QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json")));
    int const current = meta["current"].toInt();
    if (current > 0)
        return current;
    QVector<Version> const all = versions(environment, job, scene);
    return all.isEmpty() ? 0 : all.last().number;
}

bool SceneDatabase::setCurrentVersion(QString const& environment, QString const& job, QString const& scene, int number, QString* error)
{
    QString const file = QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json"));
    QJsonObject meta = readJson(file);
    bool known = false;
    for (Version const& v : versions(environment, job, scene))
        known = known || v.number == number;
    if (!known)
        return fail(error, QObject::tr("The scene %1 has no version %2.").arg(scene).arg(number));
    meta["current"] = number;
    return writeJson(file, meta, error);
}

QString SceneDatabase::versionFile(QString const& environment, QString const& job, QString const& scene, int number) const
{
    if (number <= 0)
        number = currentVersion(environment, job, scene);
    if (number <= 0)
        return QString();
    QString const file = QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("versions/v%1.eclipse").arg(number, 4, 10, QLatin1Char('0')));
    return QFileInfo::exists(file) ? file : QString();
}

int SceneDatabase::saveVersion(QString const& environment, QString const& job, QString const& scene, Scene const& content, QString const& comment, QString* error)
{
    QString const folder = sceneFolder(environment, job, scene);
    QString const metaFile = QDir(folder).filePath(QStringLiteral("scene.json"));
    if (!QFileInfo::exists(metaFile)) {
        fail(error, QObject::tr("Cannot find scene %1 in scene database.").arg(scene));
        return 0;
    }
    if (lockedByOther(environment, job, scene, QStringLiteral("scene"))) {
        Lock const l = lockOf(environment, job, scene, QStringLiteral("scene"));
        fail(error, QObject::tr("The scene %1 is being worked on by %2 (on %3): it cannot be saved over.").arg(scene, l.user, l.host));
        return 0;
    }
    // (A number is never given again, even when the version that had it was deleted.)
    QVector<Version> const all = versions(environment, job, scene);
    int const number = std::max(all.isEmpty() ? 1 : all.last().number + 1, readJson(metaFile)["next"].toInt(1));
    QDir().mkpath(QDir(folder).filePath(QStringLiteral("versions")));
    QString const file = QDir(folder).filePath(QStringLiteral("versions/v%1.eclipse").arg(number, 4, 10, QLatin1Char('0')));
    if (!content.save(file, error))
        return 0;
    QJsonObject meta = readJson(metaFile);
    QJsonArray list = meta["versions"].toArray();
    list.append(QJsonObject{{"number", number}, {"user", currentUser()}, {"when", QDateTime::currentDateTime().toString(Qt::ISODate)}, {"comment", comment}});
    meta["versions"] = list;
    meta["next"] = number + 1;
    // (A job that wants approval keeps the version it had until a new one is chosen.)
    if (!manualApproval(environment, job) || meta["current"].toInt() <= 0)
        meta["current"] = number;
    if (!writeJson(metaFile, meta, error))
        return 0;
    return number;
}

bool SceneDatabase::deleteVersion(QString const& environment, QString const& job, QString const& scene, int number, QString* error)
{
    QVector<Version> const all = versions(environment, job, scene);
    if (all.size() <= 1)
        return fail(error, QObject::tr("The only version of a scene cannot be deleted."));
    if (number == currentVersion(environment, job, scene))
        return fail(error, QObject::tr("The current version cannot be deleted: choose another first."));
    QString const metaFile = QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json"));
    QJsonObject meta = readJson(metaFile);
    QJsonArray kept;
    bool found = false;
    for (QJsonValue const& v : meta["versions"].toArray()) {
        if (v.toObject()["number"].toInt() == number) {
            found = true;
            continue;
        }
        kept.append(v);
    }
    if (!found)
        return fail(error, QObject::tr("The scene %1 has no version %2.").arg(scene).arg(number));
    QFile::remove(versionFile(environment, job, scene, number));
    meta["versions"] = kept;
    return writeJson(metaFile, meta, error);
}

// ---- stages and approval ----

QString SceneDatabase::stage(QString const& environment, QString const& job, QString const& scene) const
{
    QString const file = scene.isEmpty() ? QDir(jobFolder(environment, job)).filePath(QStringLiteral("job.json"))
                                         : QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json"));
    return readJson(file)["stage"].toString();
}

bool SceneDatabase::setStage(QString const& environment, QString const& job, QString const& scene, QString const& stage, QString* error)
{
    QString const file = scene.isEmpty() ? QDir(jobFolder(environment, job)).filePath(QStringLiteral("job.json"))
                                         : QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("scene.json"));
    if (!QFileInfo::exists(QFileInfo(file).path()))
        return fail(error, scene.isEmpty() ? QObject::tr("Could not change stage of the job %1").arg(job) : QObject::tr("Cannot find scene %1 in scene database.").arg(scene));
    QJsonObject meta = readJson(file);
    meta["stage"] = stage;
    return writeJson(file, meta, error);
}

bool SceneDatabase::manualApproval(QString const& environment, QString const& job) const
{
    return readJson(QDir(jobFolder(environment, job)).filePath(QStringLiteral("job.json")))["manualApproval"].toBool();
}

bool SceneDatabase::setManualApproval(QString const& environment, QString const& job, bool manual, QString* error)
{
    if (!jobs(environment).contains(job))
        return fail(error, QObject::tr("Could not change approval policy of the job %1").arg(job));
    QString const file = QDir(jobFolder(environment, job)).filePath(QStringLiteral("job.json"));
    QJsonObject meta = readJson(file);
    meta["manualApproval"] = manual;
    return writeJson(file, meta, error);
}

// ---- rights ----

SceneDatabase::Lock SceneDatabase::lockOf(QString const& environment, QString const& job, QString const& scene, QString const& what) const
{
    QJsonObject const o = readJson(QDir(QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("locks"))).filePath(lockName(what)));
    return Lock{o["user"].toString(), o["host"].toString(), QDateTime::fromString(o["when"].toString(), Qt::ISODate)};
}

QStringList SceneDatabase::locks(QString const& environment, QString const& job, QString const& scene) const
{
    QStringList out;
    QDir const folder(QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("locks")));
    for (QString const& f : folder.entryList({QStringLiteral("*.lock")}, QDir::Files, QDir::Name)) {
        QJsonObject const o = readJson(folder.filePath(f));
        out << o["what"].toString(f.chopped(5));
    }
    return out;
}

bool SceneDatabase::lockedByOther(QString const& environment, QString const& job, QString const& scene, QString const& what) const
{
    Lock const l = lockOf(environment, job, scene, what);
    return l.held() && (l.user != currentUser() || l.host != host());
}

bool SceneDatabase::getRights(QString const& environment, QString const& job, QString const& scene, QString const& what, QString* error)
{
    if (!QFileInfo(sceneFolder(environment, job, scene)).isDir())
        return fail(error, QObject::tr("Cannot find scene %1 in scene database.").arg(scene));
    if (lockedByOther(environment, job, scene, what)) {
        Lock const l = lockOf(environment, job, scene, what);
        return fail(error, QObject::tr("%1 is being modified by %2 (on %3, since %4).").arg(what, l.user, l.host, l.when.toString(Qt::TextDate)));
    }
    QString const folder = QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("locks"));
    QDir().mkpath(folder);
    return writeJson(QDir(folder).filePath(lockName(what)),
                     QJsonObject{{"what", what}, {"user", currentUser()}, {"host", host()}, {"when", QDateTime::currentDateTime().toString(Qt::ISODate)}}, error);
}

bool SceneDatabase::releaseRights(QString const& environment, QString const& job, QString const& scene, QString const& what, bool force, QString* error)
{
    Lock const l = lockOf(environment, job, scene, what);
    if (!l.held())
        return true;
    if (!force && lockedByOther(environment, job, scene, what))
        return fail(error, QObject::tr("The rights to modify %1 are %2's: Force Release takes them away.").arg(what, l.user));
    QString const file = QDir(QDir(sceneFolder(environment, job, scene)).filePath(QStringLiteral("locks"))).filePath(lockName(what));
    if (!QFile::remove(file))
        return fail(error, QObject::tr("Error locking %1").arg(what));
    return true;
}

// ---- users ----

QVector<SceneDatabase::User> SceneDatabase::users() const
{
    QVector<User> out;
    for (QJsonValue const& v : readJson(QDir(m_root).filePath(QStringLiteral("users.json")))["users"].toArray()) {
        QJsonObject const o = v.toObject();
        out.push_back(User{o["login"].toString(), o["firstName"].toString(), o["lastName"].toString(), o["type"].toInt()});
    }
    return out;
}

namespace {
bool writeUsers(QString const& root, QVector<SceneDatabase::User> const& users, QString* error)
{
    QJsonArray list;
    for (SceneDatabase::User const& u : users)
        list.append(QJsonObject{{"login", u.login}, {"firstName", u.firstName}, {"lastName", u.lastName}, {"type", u.type}});
    return writeJson(QDir(root).filePath(QStringLiteral("users.json")), QJsonObject{{"users", list}}, error);
}
}

bool SceneDatabase::isAdministrator() const
{
    QVector<User> const all = users();
    if (all.isEmpty())
        return true;
    for (User const& u : all)
        if (u.login == currentUser())
            return u.type == 1;
    return false;
}

bool SceneDatabase::addUser(User const& user, QString* error)
{
    if (!isAdministrator())
        return fail(error, QObject::tr("Only an administrator can change the users."));
    if (user.login.trimmed().isEmpty())
        return fail(error, QObject::tr("A user needs a user name."));
    QVector<User> all = users();
    for (User const& u : all)
        if (u.login == user.login)
            return fail(error, QObject::tr("There is already a user %1.").arg(user.login));
    all.push_back(user);
    return writeUsers(m_root, all, error);
}

bool SceneDatabase::modifyUser(QString const& login, User const& user, QString* error)
{
    if (!isAdministrator())
        return fail(error, QObject::tr("Only an administrator can change the users."));
    QVector<User> all = users();
    for (User& u : all)
        if (u.login == login) {
            u = user;
            return writeUsers(m_root, all, error);
        }
    return fail(error, QObject::tr("There is no user %1.").arg(login));
}

bool SceneDatabase::removeUser(QString const& login, QString* error)
{
    if (!isAdministrator())
        return fail(error, QObject::tr("Only an administrator can change the users."));
    if (login == currentUser())
        return fail(error, QObject::tr("You are not allowed to delete the user you are logged in as."));
    QVector<User> all = users();
    int const before = int(all.size());
    all.erase(std::remove_if(all.begin(), all.end(), [&](User const& u) { return u.login == login; }), all.end());
    if (all.size() == before)
        return fail(error, QObject::tr("There is no user %1.").arg(login));
    return writeUsers(m_root, all, error);
}

// ---- in and out ----

bool SceneDatabase::exportScene(QString const& environment, QString const& job, QString const& scene, QString const& folder, QString* error)
{
    if (environment.isEmpty() || job.isEmpty())
        return fail(error, QObject::tr("Export needs at least an Environment and a Job."));
    QString const file = versionFile(environment, job, scene);
    if (file.isEmpty())
        return fail(error, QObject::tr("Cannot find scene %1 in scene database.").arg(scene));
    Scene content;
    if (!Scene::load(file, content, error))
        return false;
    if (!QDir().mkpath(folder))
        return fail(error, QObject::tr("Could not make the folder %1.").arg(QDir::toNativeSeparators(folder)));
    // (The files it uses copied beside it, and pointed at there.)
    if (content.packFiles(QDir(folder).filePath(QStringLiteral("files")), error) < 0)
        return false;
    return content.save(QDir(folder).filePath(scene + QStringLiteral(".eclipse")), error);
}

bool SceneDatabase::importScene(QString const& file, QString const& environment, QString const& job, QString const& name, QString* error)
{
    Scene content;
    if (!Scene::load(file, content, error))
        return false;
    if (!jobs(environment).contains(job))
        return fail(error, QObject::tr("Could not find job %1 in environment %2.").arg(job, environment));
    if (scenes(environment, job).contains(name))
        return saveVersion(environment, job, name, content, QObject::tr("Imported from %1").arg(QFileInfo(file).fileName()), error) > 0;
    // (Its files kept with the scene in the database.)
    if (content.packFiles(QDir(sceneFolder(environment, job, name)).filePath(QStringLiteral("files")), error) < 0)
        return false;
    return createScene(environment, job, name, content, error);
}
