// Eclipse - the Help menu: the help window and the help for what is under the pointer, What's This, the program's
// documents and places on the web, Check for Updates, Report an Issue, usage statistics and the About box.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include "ui/help/AboutDialog.h"
#include "ui/help/HelpCenter.h"
#include "ui/help/HelpView.h"
#include "ui/help/UpdateCheck.h"
#include "ui/help/UsageStats.h"

#include <QApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWhatsThis>

namespace {

QSettings preferences() { return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")); }

bool hasScreen()
{
    return QApplication::platformName() != QLatin1String("offscreen") && QApplication::platformName() != QLatin1String("minimal");
}

// What's This: the next click anywhere is told about instead of done (Escape leaves).
class WhatsThisMode : public QObject {
public:
    explicit WhatsThisMode(MainWindow* window) : QObject(window), m_window(window)
    {
        qApp->installEventFilter(this);
        QApplication::setOverrideCursor(Qt::WhatsThisCursor);
    }
    ~WhatsThisMode() override { QApplication::restoreOverrideCursor(); }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            deleteLater();
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            QPoint const at = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            QWidget* const widget = QApplication::widgetAt(at);
            qApp->removeEventFilter(this);
            QWhatsThis::showText(at, m_window->whatsThisFor(widget), widget);
            deleteLater();
            return true;
        }
        return event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick;
    }

private:
    MainWindow* m_window;
};

}  // namespace

void MainWindow::startupChecks()
{
    // Usage statistics: the commands run and the views shown, while they are kept.
    UsageStats::instance().startSession();
    connect(m_actions, &ActionManager::commandRun, this, [](QString const& id) { UsageStats::instance().command(id); });
    for (QDockWidget* const dock : findChildren<QDockWidget*>())
        connect(dock, &QDockWidget::visibilityChanged, this, [dock](bool shown) {
            if (shown)
                UsageStats::instance().screen(dock->objectName());
        });
    connect(qApp, &QApplication::aboutToQuit, this, [] { UsageStats::instance().endSession(); });
    // The update check of the day, a moment after the window is up.
    if (UpdateCheck::automatic() && UpdateCheck::dueToday())
        QTimer::singleShot(4000, this, [this] { checkForUpdates(QUrl(), true); });
}

void MainWindow::writeHelpPages()
{
    HelpCenter::writeCommandsPage(menuBar());
    m_preferencesForHelp = true;
    onActionPreferences();
    m_preferencesForHelp = false;
}

HelpView* MainWindow::showHelp(QString const& topic)
{
    // The pages of the commands and the preferences, as they are now.
    writeHelpPages();
    QString const page = topic.isEmpty() ? QStringLiteral("index.html") : topic;
    if (preferences().value(QStringLiteral("prefs/helpInBrowser"), false).toBool() && hasScreen()) {
        for (QString const& folder : HelpCenter::searchPaths()) {
            QString const file = QDir(folder).filePath(page.section(QLatin1Char('#'), 0, 0));
            if (QFileInfo::exists(file)) {
                QUrl url = QUrl::fromLocalFile(file);
                url.setFragment(page.section(QLatin1Char('#'), 1));
                QDesktopServices::openUrl(url);
                return nullptr;
            }
        }
    }
    QDialog* window = findChild<QDialog*>(QStringLiteral("HelpWindow"));
    HelpView* view = window ? window->findChild<HelpView*>() : nullptr;
    if (!window) {
        window = new QDialog(this);
        window->setObjectName(QStringLiteral("HelpWindow"));
        window->setWindowTitle(tr("Eclipse Help"));
        window->resize(980, 700);
        view = new HelpView(window);
        QVBoxLayout* const layout = new QVBoxLayout(window);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->addWidget(view);
    }
    if (topic.isEmpty())
        view->showContents();
    else
        view->showTopic(topic);
    window->show();
    window->raise();
    window->activateWindow();
    return view;
}

QString MainWindow::contextTopic(QWidget* widget) const
{
    if (!HelpCenter::pageExists(QStringLiteral("commands.html")))
        const_cast<MainWindow*>(this)->writeHelpPages();
    return HelpCenter::topicAt(widget);
}

QString MainWindow::whatsThisFor(QWidget* widget) const
{
    QString const topic = contextTopic(widget);
    const_cast<MainWindow*>(this)->m_whatsThisTopic = topic;
    QString name;
    for (QWidget* w = widget; w && name.isEmpty(); w = w->parentWidget())
        if (!w->toolTip().isEmpty())
            name = w->toolTip();
    QString text = topic.isEmpty() ? QString() : HelpCenter::summary(topic);
    if (text.isEmpty())
        text = name;
    if (text.isEmpty())
        return tr("There is no help about this here yet; Help > Eclipse Help has every page.");
    return text + (topic.isEmpty() ? QString() : QStringLiteral("\n\n") + tr("(F1 opens its page: %1)").arg(topic));
}

void MainWindow::onActionHelp()
{
    showHelp();
}

void MainWindow::onActionContextHelp()
{
    // What What's This last told of, else what is under the pointer, else what has the keyboard.
    QString topic = m_whatsThisTopic;
    m_whatsThisTopic.clear();
    if (topic.isEmpty()) {
        QWidget* widget = QApplication::widgetAt(QCursor::pos());
        if (!widget)
            widget = QApplication::focusWidget();
        topic = contextTopic(widget);
    }
    showHelp(topic);
}

void MainWindow::onActionWhatsThis()
{
    new WhatsThisMode(this);
    statusBar()->showMessage(tr("What's This: click anything to be told what it is (Escape to leave)."), 5000);
}

void MainWindow::onActionGettingStarted()
{
    showHelp(QStringLiteral("start.html"));
}

void MainWindow::onActionOnlineHelp()
{
    QString const url = HelpCenter::link(QStringLiteral("documentation"));
    if (url.isEmpty()) {
        showHelp();
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::onActionEclipseOnWeb()
{
    QString const url = HelpCenter::link(QStringLiteral("website"));
    if (url.isEmpty())
        statusBar()->showMessage(tr("No website is set (resources/links.conf)."), 5000);
    else
        QDesktopServices::openUrl(QUrl(url));
}

QString MainWindow::latestNews() const
{
    QString news;
    if (!m_newsVersion.isEmpty())
        news += tr("# Eclipse %1 is out\n\n%2\n\n").arg(m_newsVersion, m_newsNotes);
    return news;
}

void MainWindow::onActionShowLatestNews()
{
    if (m_newsVersion.isEmpty()) {
        showHelp(QStringLiteral("news.html"));
        return;
    }
    DocumentDialog dialog(tr("Latest News"), latestNews(), true, this);
    dialog.exec();
}

bool MainWindow::checkForUpdates(QUrl const& feed, bool quiet, QString* message)
{
    QUrl const url = feed.isEmpty() ? UpdateCheck::feed() : feed;
    UpdateCheck::Release release;
    QString error;
    bool ok = false;
    {
        QProgressDialog progress(tr("Checking for updates..."), tr("Cancel"), 0, 0, this);
        progress.setObjectName(QStringLiteral("CheckUpdatesProgress"));
        progress.setWindowTitle(tr("Check for Updates"));
        progress.setMinimumDuration(quiet ? 100000 : 500);
        ok = UpdateCheck::fetch(url, release, &error, 15000, [&progress] { return progress.wasCanceled(); });
    }
    UpdateCheck::noteChecked();
    auto say = [&](QString const& text) {
        if (message)
            *message = text;
        statusBar()->showMessage(text, 8000);
    };
    if (!ok) {
        say(tr("Check for Updates : %1").arg(error));
        if (!quiet && hasScreen())
            QMessageBox::warning(this, tr("Check for Updates"), error);
        return false;
    }
    if (UpdateCheck::compareVersions(release.version, UpdateCheck::currentVersion()) <= 0) {
        say(tr("Check for Updates : No update is available."));
        if (!quiet && hasScreen())
            QMessageBox::information(this, tr("Check for Updates"), tr("Check for Updates : No update is available.") + QStringLiteral("\n")
                                                                       + tr("Eclipse %1 is the latest version.").arg(UpdateCheck::currentVersion()));
        return false;
    }
    m_newsVersion = release.version;
    m_newsNotes = release.notes;
    say(tr("Eclipse %1 is available (this is %2).").arg(release.version, UpdateCheck::currentVersion()));
    if (hasScreen()) {
        UpdateAvailableDialog dialog(release, this);
        dialog.exec();
        if (!dialog.downloaded().isEmpty())
            say(tr("Downloaded to %1").arg(QDir::toNativeSeparators(dialog.downloaded())));
        else if (!dialog.error().isEmpty())
            QMessageBox::warning(this, tr("Download Update"), dialog.error());
    }
    return true;
}

void MainWindow::onActionCheckForUpdates()
{
    checkForUpdates();
}

void MainWindow::onActionShowReadMe()
{
    DocumentDialog dialog(tr("Read Me"), AboutText::document(QStringLiteral("readme")), true, this);
    dialog.exec();
}

void MainWindow::onActionShowLicense()
{
    DocumentDialog dialog(tr("License"), AboutText::document(QStringLiteral("license")), false, this);
    dialog.exec();
}

void MainWindow::onActionShowCredits()
{
    QString text = AboutText::document(QStringLiteral("credits"));
    if (QString const notices = AboutText::document(QStringLiteral("notices")); !notices.isEmpty())
        text += QStringLiteral("\n\n---\n\n") + notices;
    DocumentDialog dialog(tr("Credits"), text, true, this);
    dialog.exec();
}

void MainWindow::onActionReportIssue()
{
    QStringList messages;
    if (QPlainTextEdit* const log = findChild<QPlainTextEdit*>(QStringLiteral("MessageLog")))
        messages = log->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    ReportIssueDialog dialog(messages, this);
    dialog.exec();
}

void MainWindow::onActionUsageStatistics()
{
    UsageStatsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onActionUsageTester()
{
    if (!m_debugMode) {
        statusBar()->showMessage(tr("The Usage Statistics Tester is for Debug Mode (Help > Debug Mode)."), 5000);
        return;
    }
    UsageTesterDialog dialog(this);
    dialog.exec();
}

void MainWindow::onActionAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}
