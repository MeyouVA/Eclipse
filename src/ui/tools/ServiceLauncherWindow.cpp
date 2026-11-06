// Eclipse - the Service Launcher's window (see ServiceLauncherWindow.h).
#include "ui/tools/ServiceLauncherWindow.h"

#include "ui/tools/ServiceLauncher.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QVBoxLayout>

ServiceLauncherWindow::ServiceLauncherWindow(ServiceLauncher* launcher, QWidget* parent) : QMainWindow(parent), m_launcher(launcher)
{
    setObjectName(QStringLiteral("ServiceLauncherWindow"));
    setWindowTitle(tr("Service Launcher"));
    QWidget* const central = new QWidget(this);
    QVBoxLayout* const outer = new QVBoxLayout(central);
    QSplitter* const split = new QSplitter(Qt::Horizontal, central);

    // The services.
    QWidget* const left = new QWidget(split);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(left);
    m_list->setObjectName(QStringLiteral("ServiceList"));
    leftLayout->addWidget(m_list);
    QHBoxLayout* const listButtons = new QHBoxLayout;
    auto button = [&](QString const& text, QString const& name, QLayout* in, auto slot) {
        QPushButton* const b = new QPushButton(text, central);
        b->setObjectName(name);
        in->addWidget(b);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    button(tr("Add"), QStringLiteral("ServiceAdd"), listButtons, &ServiceLauncherWindow::addService);
    button(tr("Remove"), QStringLiteral("ServiceRemove"), listButtons, &ServiceLauncherWindow::removeService);
    button(tr("Start"), QStringLiteral("ServiceStart"), listButtons, &ServiceLauncherWindow::startService);
    button(tr("Stop"), QStringLiteral("ServiceStop"), listButtons, &ServiceLauncherWindow::stopService);
    leftLayout->addLayout(listButtons);
    button(tr("Stop All Processes"), QStringLiteral("ServiceStopAll"), leftLayout, &ServiceLauncherWindow::stopAllProcesses);

    // The parameters of the one picked.
    QWidget* const right = new QWidget(split);
    QVBoxLayout* const rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    QGroupBox* const parameters = new QGroupBox(tr("Application parameters"), right);
    QFormLayout* const form = new QFormLayout(parameters);
    m_name = new QLineEdit(parameters);
    m_name->setObjectName(QStringLiteral("ServiceName"));
    form->addRow(tr("Name :"), m_name);
    m_program = new QLineEdit(parameters);
    m_program->setObjectName(QStringLiteral("ServiceProgram"));
    form->addRow(tr("Program :"), m_program);
    m_parameters = new QLineEdit(parameters);
    m_parameters->setObjectName(QStringLiteral("ServiceParameters"));
    form->addRow(tr("Other Parameters :"), m_parameters);
    m_launchOnStart = new QCheckBox(tr("Launch when Service Launcher starts"), parameters);
    m_launchOnStart->setObjectName(QStringLiteral("ServiceLaunchOnStart"));
    form->addRow(m_launchOnStart);
    m_keepAlive = new QCheckBox(tr("Keep Alive"), parameters);
    m_keepAlive->setObjectName(QStringLiteral("ServiceKeepAlive"));
    m_keepAlive->setToolTip(tr("Started again when it stops."));
    form->addRow(m_keepAlive);
    m_waitFor = new QSpinBox(parameters);
    m_waitFor->setObjectName(QStringLiteral("ServiceWaitFor"));
    m_waitFor->setRange(0, 3600);
    m_waitFor->setSuffix(tr(" seconds"));
    form->addRow(tr("Wait for :"), m_waitFor);
    m_crashLimit = new QSpinBox(parameters);
    m_crashLimit->setObjectName(QStringLiteral("ServiceCrashLimit"));
    m_crashLimit->setRange(1, 1000);
    m_crashLimit->setToolTip(tr("Not started again once it stops this many times within its crash window."));
    form->addRow(tr("Stop relaunching after :"), m_crashLimit);
    m_onError = new QComboBox(parameters);
    m_onError->setObjectName(QStringLiteral("ServiceOnError"));
    m_onError->addItems({tr("Notify me on errors"), tr("Display window"), tr("Do nothing")});
    form->addRow(tr("On error :"), m_onError);
    QPushButton* const update = button(tr("Update Parameters"), QStringLiteral("ServiceUpdate"), form, [this] {
        QString error;
        if (!updateParameters(&error))
            tell(error, false);
    });
    Q_UNUSED(update);
    rightLayout->addWidget(parameters);

    QGroupBox* const where = new QGroupBox(tr("Launcher"), right);
    QFormLayout* const whereForm = new QFormLayout(where);
    QHBoxLayout* const locationRow = new QHBoxLayout;
    m_location = new QLineEdit(m_launcher->location(), where);
    m_location->setObjectName(QStringLiteral("ServiceLocation"));
    locationRow->addWidget(m_location, 1);
    QPushButton* const browse = button(tr("Open Directory"), QStringLiteral("ServiceOpenDirectory"), locationRow, &ServiceLauncherWindow::openDirectory);
    Q_UNUSED(browse);
    QPushButton* const reset = button(tr("Reset"), QStringLiteral("ServiceResetLocation"), locationRow, &ServiceLauncherWindow::resetLocation);
    reset->setToolTip(tr("Reset the Services Location to the default value"));
    whereForm->addRow(tr("Services location :"), locationRow);
    connect(m_location, &QLineEdit::editingFinished, this, [this] {
        QString error;
        if (!applyLocation(&error))
            tell(error, false);
    });
    m_launchOnLogin = new QCheckBox(tr("Launch on login"), where);
    m_launchOnLogin->setObjectName(QStringLiteral("ServiceLaunchOnLogin"));
    m_launchOnLogin->setChecked(ServiceLauncher::launchOnLogin());
    connect(m_launchOnLogin, &QCheckBox::toggled, this, [](bool on) { ServiceLauncher::setLaunchOnLogin(on); });
    whereForm->addRow(m_launchOnLogin);
    m_notify = new QCheckBox(tr("Notify me on errors"), where);
    m_notify->setObjectName(QStringLiteral("ServiceNotify"));
    m_notify->setChecked(m_launcher->notifyOnErrors());
    connect(m_notify, &QCheckBox::toggled, this, [this](bool on) {
        m_launcher->setNotifyOnErrors(on);
        m_launcher->save();
    });
    whereForm->addRow(m_notify);
    rightLayout->addWidget(where);
    rightLayout->addStretch(1);
    split->addWidget(left);
    split->addWidget(right);
    outer->addWidget(split, 1);

    // The log of the one picked.
    QHBoxLayout* const logRow = new QHBoxLayout;
    logRow->addWidget(new QLabel(tr("Show last"), central));
    m_showLast = new QSpinBox(central);
    m_showLast->setObjectName(QStringLiteral("ServiceShowLast"));
    m_showLast->setRange(10, 100000);
    m_showLast->setValue(m_launcher->showLast());
    m_showLast->setSuffix(tr(" lines"));
    connect(m_showLast, &QSpinBox::valueChanged, this, [this](int lines) {
        m_launcher->setShowLast(lines);
        m_launcher->save();
        showLog();
    });
    logRow->addWidget(m_showLast);
    logRow->addStretch(1);
    button(tr("Clear Window"), QStringLiteral("ServiceClearWindow"), logRow, &ServiceLauncherWindow::clearWindow);
    button(tr("View Complete Log"), QStringLiteral("ServiceViewLog"), logRow, &ServiceLauncherWindow::viewCompleteLog);
    outer->addLayout(logRow);
    m_log = new QPlainTextEdit(central);
    m_log->setObjectName(QStringLiteral("ServiceLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(100000);
    outer->addWidget(m_log, 1);
    setCentralWidget(central);
    statusBar();

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        showService(row);
        showLog();
    });
    connect(m_launcher, &ServiceLauncher::stateChanged, this, [this](int index) {
        if (QListWidgetItem* const item = m_list->item(index))
            item->setText(QStringLiteral("%1 - %2").arg(m_launcher->services()[index].name, m_launcher->state(index)));
    });
    connect(m_launcher, &ServiceLauncher::output, this, [this](int index, QString const& line) {
        if (index == current())
            m_log->appendPlainText(line);
    });
    connect(m_launcher, &ServiceLauncher::problem, this, [this](int, QString const& message, bool show) { tell(message, show); });
    if (QSystemTrayIcon::isSystemTrayAvailable() && QApplication::platformName() != QLatin1String("offscreen")) {
        m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
        m_tray->setToolTip(tr("Service Launcher"));
        connect(m_tray, &QSystemTrayIcon::activated, this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        QMenu* const menu = new QMenu(this);
        menu->addAction(tr("Display window"), this, [this] {
            showNormal();
            raise();
        });
        menu->addAction(tr("Stop All Processes and Quit"), this, [this] {
            m_launcher->stopAll();
            QApplication::quit();
        });
        m_tray->setContextMenu(menu);
        m_tray->show();
        QApplication::setQuitOnLastWindowClosed(false);
    }
    refreshList();
    resize(820, 620);
}

int ServiceLauncherWindow::current() const
{
    return m_list->currentRow();
}

void ServiceLauncherWindow::setCurrent(int index)
{
    m_list->setCurrentRow(index);
}

void ServiceLauncherWindow::refreshList()
{
    int const keep = std::max(0, current());
    m_list->clear();
    for (int i = 0; i < m_launcher->count(); ++i)
        m_list->addItem(QStringLiteral("%1 - %2").arg(m_launcher->services()[i].name, m_launcher->state(i)));
    if (m_launcher->count() > 0)
        m_list->setCurrentRow(std::min(keep, m_launcher->count() - 1));
    else
        showService(-1);
}

void ServiceLauncherWindow::showService(int index)
{
    bool const any = index >= 0 && index < m_launcher->count();
    ServiceLauncher::Service const s = any ? m_launcher->services()[index] : ServiceLauncher::Service();
    m_name->setText(s.name);
    m_program->setText(s.program);
    m_parameters->setText(s.parameters);
    m_launchOnStart->setChecked(s.launchOnStart);
    m_keepAlive->setChecked(s.keepAlive);
    m_waitFor->setValue(s.waitFor);
    m_crashLimit->setValue(s.crashLimit);
    m_onError->setCurrentIndex(s.onError);
    for (QWidget* w : {static_cast<QWidget*>(m_name), static_cast<QWidget*>(m_program), static_cast<QWidget*>(m_parameters), static_cast<QWidget*>(m_launchOnStart),
                       static_cast<QWidget*>(m_keepAlive), static_cast<QWidget*>(m_waitFor), static_cast<QWidget*>(m_crashLimit), static_cast<QWidget*>(m_onError)})
        w->setEnabled(any);
}

void ServiceLauncherWindow::showLog()
{
    m_log->setPlainText(m_launcher->log(current(), m_launcher->showLast()).join(QLatin1Char('\n')));
}

bool ServiceLauncherWindow::updateParameters(QString* error)
{
    int const index = current();
    if (index < 0)
        return false;
    ServiceLauncher::Service s = m_launcher->services()[index];
    s.name = m_name->text().trimmed();
    s.program = m_program->text().trimmed();
    s.parameters = m_parameters->text().trimmed();
    s.launchOnStart = m_launchOnStart->isChecked();
    s.keepAlive = m_keepAlive->isChecked();
    s.waitFor = m_waitFor->value();
    s.crashLimit = m_crashLimit->value();
    s.onError = m_onError->currentIndex();
    if (s.program.isEmpty()) {
        if (error)
            *error = tr("Please enter a valid path.");
        return false;
    }
    if (!m_launcher->updateService(index, s, error))
        return false;
    m_launcher->save();
    if (QString const why = m_launcher->programProblem(index); !why.isEmpty())
        tell(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(m_launcher->programPath(index)), why), false);
    else
        statusBar()->showMessage(tr("The parameters of %1 were updated.").arg(s.name), 4000);
    return true;
}

bool ServiceLauncherWindow::applyLocation(QString* error)
{
    if (!m_launcher->setLocation(m_location->text().trimmed(), error))
        return false;
    m_launcher->save();
    return true;
}

void ServiceLauncherWindow::addService()
{
    ServiceLauncher::Service s = ServiceLauncher::batchService();
    s.launchOnStart = false;
    m_launcher->addService(s);
    m_launcher->save();
    refreshList();
    m_list->setCurrentRow(m_launcher->count() - 1);
}

void ServiceLauncherWindow::removeService()
{
    int const index = current();
    if (index < 0)
        return;
    m_launcher->removeService(index);
    m_launcher->save();
    refreshList();
}

void ServiceLauncherWindow::startService()
{
    QString error;
    if (!m_launcher->start(current(), &error) && !error.isEmpty())
        tell(error, false);
}

void ServiceLauncherWindow::stopService()
{
    QString error;
    if (!m_launcher->stop(current(), 5000, &error))
        tell(error, false);
}

void ServiceLauncherWindow::stopAllProcesses()
{
    statusBar()->showMessage(tr("Shutting Down..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int const failed = m_launcher->stopAll();
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(failed ? tr("%n service(s) could not be shut down.", nullptr, failed) : tr("All the services were shut down."), 5000);
}

void ServiceLauncherWindow::clearWindow()
{
    m_launcher->clearLog(current());
    m_log->clear();
}

void ServiceLauncherWindow::viewCompleteLog()
{
    QString const file = m_launcher->logFile(current());
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        tell(tr("There is no log yet."), false);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void ServiceLauncherWindow::resetLocation()
{
    m_launcher->resetLocation();
    m_location->setText(QDir::toNativeSeparators(m_launcher->location()));
    m_launcher->save();
}

void ServiceLauncherWindow::openDirectory()
{
    QString const folder = QFileDialog::getExistingDirectory(this, tr("Services location"), m_launcher->location());
    if (folder.isEmpty())
        return;
    m_location->setText(QDir::toNativeSeparators(folder));
    QString error;
    if (!applyLocation(&error))
        tell(error, false);
}

void ServiceLauncherWindow::tell(QString const& message, bool showWindow)
{
    m_lastMessage = message;
    statusBar()->showMessage(message, 10000);
    if (showWindow) {
        showNormal();
        raise();
        activateWindow();
        if (QApplication::platformName() != QLatin1String("offscreen"))
            QMessageBox::warning(this, tr("Service Launcher"), message);
    } else if (m_tray && isHidden()) {
        m_tray->showMessage(tr("Service Launcher"), message, QSystemTrayIcon::Warning);
    }
}

void ServiceLauncherWindow::closeEvent(QCloseEvent* event)
{
    // Closing the window with services running leaves them to it - in the tray, when there is one.
    bool running = false;
    for (int i = 0; i < m_launcher->count(); ++i)
        running = running || m_launcher->isRunning(i);
    if (running && m_tray) {
        hide();
        m_tray->showMessage(tr("Service Launcher"), tr("Still running: the services are kept going. Stop All Processes to end them."));
        event->ignore();
        return;
    }
    m_launcher->stopAll();
    event->accept();
    QApplication::quit();
}
