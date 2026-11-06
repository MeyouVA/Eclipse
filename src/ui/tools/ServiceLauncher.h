// Eclipse - the Service Launcher (eclipse-services): programs that are to keep running beside Eclipse - the batch program
// working through the Batch Rendering queue, as a rule - started, watched and started again when they stop (kept alive),
// until they stop so often that it is better to look at why. What they say is kept in a log (a file of each, and the last
// lines shown); their state is written where eclipse-info -status reads it.
//
// The services are kept in services.conf (in CommandLine::dataFolder()), a [service <name>] section each.
#pragma once

#include "EclipseExport.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

class QTimer;

class ECLIPSE_UI_API ServiceLauncher : public QObject {
    Q_OBJECT

public:
    enum OnError { NotifyOnError, DisplayWindow, NothingOnError };
    struct Service {
        QString name;
        QString program;            // a name in the services location, or a whole path
        QString parameters;         // Other Parameters
        bool launchOnStart = true;  // Launch when Service Launcher starts
        bool keepAlive = true;      // started again when it stops
        int waitFor = 5;            // Wait for so many seconds before it is started again
        int crashLimit = 5;         // stopped so many times...
        int crashWindow = 60;       // ...within so many seconds: not started again
        int onError = NotifyOnError;
        bool operator==(Service const&) const = default;
    };

    explicit ServiceLauncher(QString const& configFile = QString(), QObject* parent = nullptr);
    ~ServiceLauncher() override;

    static QString defaultConfigFile();
    // The folder the services' programs are looked for in: the programs' own folder.
    static QString defaultLocation();
    // The batch program working through the queue, as a new list has it.
    static Service batchService();

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    QVector<Service> const& services() const { return m_services; }
    int count() const { return int(m_services.size()); }
    int indexOf(QString const& name) const;
    // A service added (a name not yet taken), changed (Update Parameters) or taken away (stopped first).
    int addService(Service const& service);
    bool updateService(int index, Service const& service, QString* error = nullptr);
    void removeService(int index);

    QString location() const { return m_location; }
    // The services location: false, with "Incorrect Path." / "Please enter a valid path.", when it is not a folder.
    bool setLocation(QString const& folder, QString* error = nullptr);
    // Reset the Services Location to the default value.
    void resetLocation();
    // The program a service runs, found in the location (or as given, when it is a whole path).
    QString programPath(int index) const;
    // What stops the program being run (empty: nothing): "Incorrect Path.", "Exec format error" (not a program).
    QString programProblem(int index) const;

    bool notifyOnErrors() const { return m_notify; }
    void setNotifyOnErrors(bool on) { m_notify = on; }
    int showLast() const { return m_showLast; }
    void setShowLast(int lines);

    // Started (false, with `error`, when it cannot be); stopped (the batch program by its stop file, others asked to end,
    // then made to); every one stopped ("Shutting Down..."); those to be launched when the launcher starts, started.
    bool start(int index, QString* error = nullptr);
    bool stop(int index, int waitMs = 5000, QString* error = nullptr);
    // How many could not be shut down.
    int stopAll(int waitMs = 5000);
    int startAll();
    bool isRunning(int index) const;
    // "Running", "Stopped", "Shutting down...", "Still running", "Waiting to start again", or why it is not started again.
    QString state(int index) const;
    int restarts(int index) const;

    // What it said: the last lines (0: all kept), cleared from the window (Clear Window), and the file of all of it
    // (View Complete Log).
    QStringList log(int index, int last = 0) const;
    void clearLog(int index);
    QString logFile(int index) const;

    // Launch on login: the launcher started when the user logs in (the Run key of Windows).
    static bool launchOnLogin();
    static bool setLaunchOnLogin(bool on, QString const& program = QString());

    // The state of the services as eclipse-info reads it ({updated, services: [{name, state, pid, restarts}]}).
    static QString statusFile();
    static QJsonObject readStatus();
    void writeStatus() const;

    // For checks: how long a service must run to be counted as started (milliseconds).
    void setStartTimeout(int ms) { m_startTimeout = ms; }

signals:
    void stateChanged(int index);
    void output(int index, QString const& line);
    // Something went wrong with a service ("Batch Rendering stopped working", "Stopped Relaunching because of continuous
    // crashes. Please investigate the issue."); `show`: the window is to be shown (On error : Display window).
    void problem(int index, QString const& message, bool show);

private:
    struct Running {
        QProcess* process = nullptr;
        QString state;
        QStringList lines;
        QVector<QDateTime> stops;   // when it stopped by itself, lately
        int restarts = 0;
        bool stopping = false;
        bool givenUp = false;
        QTimer* again = nullptr;
    };
    void finished(int index);
    void say(int index, QString const& text);
    void setState(int index, QString const& state);
    QString m_file;
    QString m_location;
    bool m_notify = true;
    int m_showLast = 200;
    int m_startTimeout = 3000;
    QVector<Service> m_services;
    QVector<Running> m_running;
};
