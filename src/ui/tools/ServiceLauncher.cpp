// Eclipse - the Service Launcher (see ServiceLauncher.h).
#include "ui/tools/ServiceLauncher.h"

#include "ui/tools/BatchRun.h"
#include "ui/tools/CommandLine.h"
#include "ui/tools/ConfigFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>

#include <algorithm>

namespace {

QString const kRunKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
QString const kRunName = QStringLiteral("Eclipse Service Launcher");
char const* const kOnError[] = {"notify", "window", "nothing"};

}  // namespace

ServiceLauncher::ServiceLauncher(QString const& configFile, QObject* parent)
    : QObject(parent), m_file(configFile.isEmpty() ? defaultConfigFile() : configFile), m_location(defaultLocation())
{
}

ServiceLauncher::~ServiceLauncher()
{
    for (Running& r : m_running) {
        if (r.again)
            r.again->stop();
        if (r.process && r.process->state() != QProcess::NotRunning) {
            r.stopping = true;
            r.process->kill();
            r.process->waitForFinished(2000);
        }
    }
    // Nothing kept running any more: eclipse-info says the launcher is not running.
    QJsonObject const empty{{QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODate)}, {QStringLiteral("services"), QJsonArray()}};
    QSaveFile file(statusFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(empty).toJson());
        file.commit();
    }
}

QString ServiceLauncher::defaultConfigFile()
{
    return CommandLine::dataFolder() + QStringLiteral("/services.conf");
}

QString ServiceLauncher::defaultLocation()
{
    QString const set = ConfigFile::manager(QStringLiteral("services"), QStringLiteral("location"));
    return !set.isEmpty() && QFileInfo(set).isDir() ? set : QCoreApplication::applicationDirPath();
}

ServiceLauncher::Service ServiceLauncher::batchService()
{
    Service s;
    s.name = tr("Batch Rendering");
    s.program = QStringLiteral("eclipse-batch.exe");
    s.parameters = QStringLiteral("-queue -watch 10");
    return s;
}

bool ServiceLauncher::load(QString* error)
{
    for (int i = count() - 1; i >= 0; --i)
        removeService(i);
    if (!QFileInfo::exists(m_file)) {
        addService(batchService());
        return true;
    }
    ConfigFile conf;
    if (!conf.load(m_file, error))
        return false;
    QString const location = conf.value(QStringLiteral("launcher"), QStringLiteral("location"));
    m_location = !location.isEmpty() && QFileInfo(location).isDir() ? location : defaultLocation();
    m_notify = conf.boolValue(QStringLiteral("launcher"), QStringLiteral("notifyOnErrors"), true);
    m_showLast = std::clamp(conf.intValue(QStringLiteral("launcher"), QStringLiteral("showLast"), 200), 10, 100000);
    for (QString const& section : conf.sections()) {
        if (!section.startsWith(QLatin1String("service "), Qt::CaseInsensitive))
            continue;
        Service s;
        s.name = section.mid(8).trimmed();
        s.program = conf.value(section, QStringLiteral("program"));
        s.parameters = conf.value(section, QStringLiteral("parameters"));
        s.launchOnStart = conf.boolValue(section, QStringLiteral("launchOnStart"), true);
        s.keepAlive = conf.boolValue(section, QStringLiteral("keepAlive"), true);
        s.waitFor = std::clamp(conf.intValue(section, QStringLiteral("waitFor"), 5), 0, 3600);
        s.crashLimit = std::clamp(conf.intValue(section, QStringLiteral("crashLimit"), 5), 1, 1000);
        s.crashWindow = std::clamp(conf.intValue(section, QStringLiteral("crashWindow"), 60), 1, 86400);
        QString const onError = conf.value(section, QStringLiteral("onError"), QStringLiteral("notify"));
        for (int k = 0; k < 3; ++k)
            if (onError == QLatin1String(kOnError[k]))
                s.onError = k;
        if (!s.name.isEmpty())
            addService(s);
    }
    return true;
}

bool ServiceLauncher::save(QString* error) const
{
    ConfigFile conf;
    conf.parse(QStringLiteral("# The services the Service Launcher (eclipse-services) keeps running. It writes this file; the Configuration Editor\n"
                              "# (eclipse-config) can change it too.\n"));
    conf.setValue(QStringLiteral("launcher"), QStringLiteral("location"), m_location);
    conf.setValue(QStringLiteral("launcher"), QStringLiteral("notifyOnErrors"), m_notify ? QStringLiteral("true") : QStringLiteral("false"));
    conf.setValue(QStringLiteral("launcher"), QStringLiteral("showLast"), QString::number(m_showLast));
    for (Service const& s : m_services) {
        QString const section = QStringLiteral("service ") + s.name;
        conf.setValue(section, QStringLiteral("program"), s.program);
        conf.setValue(section, QStringLiteral("parameters"), s.parameters);
        conf.setValue(section, QStringLiteral("launchOnStart"), s.launchOnStart ? QStringLiteral("true") : QStringLiteral("false"));
        conf.setValue(section, QStringLiteral("keepAlive"), s.keepAlive ? QStringLiteral("true") : QStringLiteral("false"));
        conf.setValue(section, QStringLiteral("waitFor"), QString::number(s.waitFor));
        conf.setValue(section, QStringLiteral("crashLimit"), QString::number(s.crashLimit));
        conf.setValue(section, QStringLiteral("crashWindow"), QString::number(s.crashWindow));
        conf.setValue(section, QStringLiteral("onError"), QLatin1String(kOnError[std::clamp(s.onError, 0, 2)]));
    }
    return conf.save(m_file, error);
}

int ServiceLauncher::indexOf(QString const& name) const
{
    for (int i = 0; i < count(); ++i)
        if (m_services[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

int ServiceLauncher::addService(Service const& service)
{
    Service s = service;
    if (s.name.trimmed().isEmpty())
        s.name = tr("Service");
    QString const base = s.name;
    for (int n = 2; indexOf(s.name) >= 0; ++n)
        s.name = QStringLiteral("%1 %2").arg(base).arg(n);
    m_services.push_back(s);
    Running r;
    r.state = tr("Stopped");
    m_running.push_back(r);
    return count() - 1;
}

bool ServiceLauncher::updateService(int index, Service const& service, QString* error)
{
    if (index < 0 || index >= count())
        return false;
    if (service.name.trimmed().isEmpty() || (indexOf(service.name) >= 0 && indexOf(service.name) != index)) {
        if (error)
            *error = tr("Another service is called %1.").arg(service.name);
        return false;
    }
    m_services[index] = service;
    emit stateChanged(index);
    return true;
}

void ServiceLauncher::removeService(int index)
{
    if (index < 0 || index >= count())
        return;
    stop(index, 3000);
    Running& r = m_running[index];
    if (r.again)
        r.again->deleteLater();
    if (r.process)
        r.process->deleteLater();
    m_services.removeAt(index);
    m_running.removeAt(index);
    writeStatus();
}

bool ServiceLauncher::setLocation(QString const& folder, QString* error)
{
    if (folder.trimmed().isEmpty()) {
        if (error)
            *error = tr("Please enter a valid path.");
        return false;
    }
    if (!QFileInfo(folder).isDir()) {
        if (error)
            *error = tr("Incorrect Path.") + QLatin1Char(' ') + tr("Please enter a valid path.");
        return false;
    }
    m_location = QDir(folder).absolutePath();
    return true;
}

void ServiceLauncher::resetLocation()
{
    m_location = defaultLocation();
}

QString ServiceLauncher::programPath(int index) const
{
    if (index < 0 || index >= count())
        return QString();
    QString const program = m_services[index].program;
    QString path = QDir::isAbsolutePath(program) ? program : QDir(m_location).filePath(program);
#ifdef Q_OS_WIN
    if (QFileInfo(path).suffix().isEmpty() && !QFileInfo::exists(path) && QFileInfo::exists(path + QStringLiteral(".exe")))
        path += QStringLiteral(".exe");
#endif
    return QDir::cleanPath(path);
}

QString ServiceLauncher::programProblem(int index) const
{
    QString const path = programPath(index);
    QFileInfo const info(path);
    if (path.isEmpty() || !info.exists() || info.isDir())
        return tr("Incorrect Path.");
#ifdef Q_OS_WIN
    QString const suffix = info.suffix().toLower();
    if (suffix == QLatin1String("bat") || suffix == QLatin1String("cmd"))
        return QString();
    QFile file(path);
    if (suffix != QLatin1String("exe") && suffix != QLatin1String("com"))
        return tr("Exec format error");
    if (!file.open(QIODevice::ReadOnly) || file.read(2) != "MZ")
        return tr("Exec format error");
#else
    if (!info.isExecutable())
        return tr("Exec format error");
#endif
    return QString();
}

void ServiceLauncher::setShowLast(int lines)
{
    m_showLast = std::clamp(lines, 10, 100000);
}

bool ServiceLauncher::isRunning(int index) const
{
    return index >= 0 && index < count() && m_running[index].process && m_running[index].process->state() != QProcess::NotRunning;
}

QString ServiceLauncher::state(int index) const
{
    return index >= 0 && index < count() ? m_running[index].state : QString();
}

int ServiceLauncher::restarts(int index) const
{
    return index >= 0 && index < count() ? m_running[index].restarts : 0;
}

void ServiceLauncher::setState(int index, QString const& state)
{
    m_running[index].state = state;
    writeStatus();
    emit stateChanged(index);
}

QString ServiceLauncher::logFile(int index) const
{
    if (index < 0 || index >= count())
        return QString();
    QString name = m_services[index].name;
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return CommandLine::dataFolder() + QStringLiteral("/logs/") + name + QStringLiteral(".log");
}

void ServiceLauncher::say(int index, QString const& text)
{
    Running& r = m_running[index];
    QString const line = QStringLiteral("%1  %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), text);
    r.lines << line;
    if (r.lines.size() > 5000)
        r.lines.remove(0, r.lines.size() - 5000);
    QString const file = logFile(index);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile log(file);
    if (log.open(QIODevice::Append | QIODevice::Text))
        log.write((line + QLatin1Char('\n')).toUtf8());
    emit output(index, line);
}

QStringList ServiceLauncher::log(int index, int last) const
{
    if (index < 0 || index >= count())
        return {};
    QStringList const& lines = m_running[index].lines;
    return last <= 0 || last >= lines.size() ? lines : lines.mid(lines.size() - last);
}

void ServiceLauncher::clearLog(int index)
{
    if (index >= 0 && index < count())
        m_running[index].lines.clear();
}

bool ServiceLauncher::start(int index, QString* error)
{
    if (index < 0 || index >= count())
        return false;
    Running& r = m_running[index];
    if (r.again)
        r.again->stop();
    if (isRunning(index))
        return true;
    if (QString const why = programProblem(index); !why.isEmpty()) {
        say(index, tr("Could not start %1: %2").arg(QDir::toNativeSeparators(programPath(index)), why));
        setState(index, tr("Stopped"));
        if (error)
            *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(programPath(index)), why);
        return false;
    }
    if (!r.process) {
        r.process = new QProcess(this);
        r.process->setProcessChannelMode(QProcess::MergedChannels);
        QProcess* const process = r.process;
        auto here = [this, process] {
            for (int i = 0; i < count(); ++i)
                if (m_running[i].process == process)
                    return i;
            return -1;
        };
        connect(process, &QProcess::readyRead, this, [this, process, here] {
            int const i = here();
            QString const text = QString::fromLocal8Bit(process->readAll());
            if (i < 0)
                return;
            for (QString line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (line.endsWith(QLatin1Char('\r')))
                    line.chop(1);
                if (!line.isEmpty())
                    say(i, line);
            }
        });
        connect(process, &QProcess::finished, this, [this, here] {
            if (int const i = here(); i >= 0)
                finished(i);
        });
    }
    // A batch program watching the queue is not told to stop by an old stop file.
    if (QFileInfo(programPath(index)).completeBaseName().compare(QLatin1String("eclipse-batch"), Qt::CaseInsensitive) == 0)
        QFile::remove(BatchRun::defaultStopFile());
    r.stopping = false;
    r.process->setWorkingDirectory(m_location);
    r.process->start(programPath(index), QProcess::splitCommand(m_services[index].parameters));
    if (!r.process->waitForStarted(m_startTimeout)) {
        QString const why = r.process->errorString();
        say(index, tr("Could not start %1: %2").arg(m_services[index].name, why));
        setState(index, tr("Stopped"));
        if (error)
            *error = why;
        return false;
    }
    say(index, tr("Started %1 %2").arg(QDir::toNativeSeparators(programPath(index)), m_services[index].parameters));
    setState(index, tr("Running"));
    return true;
}

void ServiceLauncher::finished(int index)
{
    Running& r = m_running[index];
    Service const& s = m_services[index];
    int const code = r.process ? r.process->exitCode() : 0;
    bool const crashed = r.process && r.process->exitStatus() == QProcess::CrashExit;
    if (r.stopping) {
        r.stopping = false;
        say(index, tr("%1 was shut down.").arg(s.name));
        setState(index, tr("Stopped"));
        return;
    }
    say(index, crashed ? tr("%1 stopped working.").arg(s.name) : tr("%1 ended (exit code %2).").arg(s.name).arg(code));
    if (s.onError != NothingOnError && m_notify && (crashed || code != 0))
        emit problem(index, tr("%1 stopped working.").arg(s.name), s.onError == DisplayWindow);
    if (!s.keepAlive || r.givenUp) {
        setState(index, tr("Stopped"));
        return;
    }
    // Kept alive - unless it stops again and again.
    QDateTime const now = QDateTime::currentDateTime();
    r.stops << now;
    r.stops.erase(std::remove_if(r.stops.begin(), r.stops.end(), [&](QDateTime const& t) { return t.secsTo(now) > s.crashWindow; }), r.stops.end());
    if (r.stops.size() >= s.crashLimit) {
        r.givenUp = true;
        QString const message = tr("Stopped Relaunching because of continuous crashes. Please investigate the issue.");
        say(index, message);
        setState(index, message);
        if (s.onError != NothingOnError)
            emit problem(index, QStringLiteral("%1: %2").arg(s.name, message), s.onError == DisplayWindow);
        return;
    }
    setState(index, tr("Waiting to start again"));
    if (!r.again) {
        r.again = new QTimer(this);
        r.again->setSingleShot(true);
        QTimer* const timer = r.again;
        connect(timer, &QTimer::timeout, this, [this, timer] {
            for (int i = 0; i < count(); ++i)
                if (m_running[i].again == timer) {
                    ++m_running[i].restarts;
                    start(i);
                }
        });
    }
    r.again->start(std::max(0, s.waitFor) * 1000);
}

bool ServiceLauncher::stop(int index, int waitMs, QString* error)
{
    if (index < 0 || index >= count())
        return false;
    Running& r = m_running[index];
    r.givenUp = false;
    r.stops.clear();
    if (r.again)
        r.again->stop();
    if (!isRunning(index)) {
        if (r.state != tr("Stopped"))
            setState(index, tr("Stopped"));
        return true;
    }
    r.stopping = true;
    setState(index, tr("Shutting down..."));
    // The batch program finishes the frame it is on when its stop file is there; the others are asked to end.
    if (QFileInfo(programPath(index)).completeBaseName().compare(QLatin1String("eclipse-batch"), Qt::CaseInsensitive) == 0) {
        QFile stopFile(BatchRun::defaultStopFile());
        if (stopFile.open(QIODevice::WriteOnly))
            stopFile.write("stop\n");
    } else {
        r.process->terminate();
    }
    if (!r.process->waitForFinished(waitMs)) {
        r.process->kill();
        if (!r.process->waitForFinished(3000)) {
            setState(index, tr("Still running"));
            QString const why = tr("Could not shut down : %1").arg(m_services[index].name);
            say(index, why);
            if (error)
                *error = why;
            return false;
        }
    }
    return true;
}

int ServiceLauncher::stopAll(int waitMs)
{
    int failed = 0;
    for (int i = 0; i < count(); ++i)
        if (isRunning(i))
            say(i, tr("Shutting Down..."));
    for (int i = 0; i < count(); ++i)
        if (!stop(i, waitMs))
            ++failed;
    return failed;
}

int ServiceLauncher::startAll()
{
    int started = 0;
    for (int i = 0; i < count(); ++i)
        if (m_services[i].launchOnStart && start(i))
            ++started;
    return started;
}

bool ServiceLauncher::launchOnLogin()
{
#ifdef Q_OS_WIN
    return QSettings(kRunKey, QSettings::NativeFormat).contains(kRunName);
#else
    return false;
#endif
}

bool ServiceLauncher::setLaunchOnLogin(bool on, QString const& program)
{
#ifdef Q_OS_WIN
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on)
        run.setValue(kRunName, QStringLiteral("\"%1\" -minimized").arg(QDir::toNativeSeparators(program.isEmpty() ? QCoreApplication::applicationFilePath() : program)));
    else
        run.remove(kRunName);
    run.sync();
    return run.status() == QSettings::NoError;
#else
    Q_UNUSED(on);
    Q_UNUSED(program);
    return false;
#endif
}

QString ServiceLauncher::statusFile()
{
    return CommandLine::dataFolder() + QStringLiteral("/services-status.json");
}

QJsonObject ServiceLauncher::readStatus()
{
    QFile file(statusFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void ServiceLauncher::writeStatus() const
{
    QJsonArray list;
    for (int i = 0; i < count(); ++i)
        list.append(QJsonObject{{QStringLiteral("name"), m_services[i].name},
                                {QStringLiteral("state"), m_running[i].state},
                                {QStringLiteral("pid"), isRunning(i) ? qint64(m_running[i].process->processId()) : qint64(0)},
                                {QStringLiteral("restarts"), m_running[i].restarts}});
    QJsonObject const status{{QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODate)}, {QStringLiteral("services"), list}};
    QSaveFile file(statusFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(status).toJson());
        file.commit();
    }
}
