// Eclipse - eclipse-services: the Service Launcher (see ui/tools/ServiceLauncher.h).
#include <QApplication>
#include <QIcon>
#include <QSettings>

#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"
#include "ui/tools/ServiceLauncher.h"
#include "ui/tools/ServiceLauncherWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Service Launcher"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QSettings const prefs(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    Theme::apply(app, ActionManager::defaultDirectory(), prefs.value(QStringLiteral("prefs/darkStyle"), true).toBool() ? QStringLiteral("dark") : QStringLiteral("light"), nullptr);
    QApplication::setWindowIcon(QIcon(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png")));
    ServiceLauncher launcher;
    launcher.load();
    ServiceLauncherWindow window(&launcher);
    // Started at login: kept in the tray until it is wanted.
    if (!QApplication::arguments().contains(QStringLiteral("-minimized")))
        window.show();
    launcher.startAll();
    int const result = QApplication::exec();
    launcher.stopAll();
    return result;
}
