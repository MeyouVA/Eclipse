// Eclipse - the Service Launcher's window: the services and their state, the parameters of the one picked (Update
// Parameters), the services location, Launch on login, and the log of the one picked (Show last, Clear Window, View
// Complete Log); Stop All Processes. Problems are told as the service asks (a note, or the window shown).
#pragma once

#include "EclipseExport.h"

#include <QMainWindow>

class ServiceLauncher;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QSystemTrayIcon;

class ECLIPSE_UI_API ServiceLauncherWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ServiceLauncherWindow(ServiceLauncher* launcher, QWidget* parent = nullptr);
    ServiceLauncher* launcher() const { return m_launcher; }
    int current() const;
    void setCurrent(int index);
    // The parameters shown taken into the service picked; false, with a message, when they are wrong.
    bool updateParameters(QString* error = nullptr);
    bool applyLocation(QString* error = nullptr);
    // The last message told (for checks).
    QString lastMessage() const { return m_lastMessage; }

public slots:
    void addService();
    void removeService();
    void startService();
    void stopService();
    void stopAllProcesses();
    void clearWindow();
    void viewCompleteLog();
    void resetLocation();
    void openDirectory();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshList();
    void showService(int index);
    void showLog();
    void tell(QString const& message, bool showWindow);
    ServiceLauncher* m_launcher;
    QListWidget* m_list;
    QLineEdit* m_name;
    QLineEdit* m_program;
    QLineEdit* m_parameters;
    QCheckBox* m_launchOnStart;
    QCheckBox* m_keepAlive;
    QSpinBox* m_waitFor;
    QSpinBox* m_crashLimit;
    QComboBox* m_onError;
    QLineEdit* m_location;
    QCheckBox* m_launchOnLogin;
    QCheckBox* m_notify;
    QSpinBox* m_showLast;
    QPlainTextEdit* m_log;
    QSystemTrayIcon* m_tray = nullptr;
    QString m_lastMessage;
};
