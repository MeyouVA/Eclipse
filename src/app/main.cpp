#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"

#include "ui/window/MainWindow.h"
#include "ui/window/WelcomeDialog.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Eclipse"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // The dark look, from the stylesheet and the icons in `resources`.
    QString error;
    if (!Theme::apply(app, ActionManager::defaultDirectory(), QStringLiteral("dark"), &error))
        QMessageBox::warning(nullptr, QStringLiteral("Eclipse"), error);

    // The icon of its windows (the program file has the same one built in).
    QApplication::setWindowIcon(QIcon(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png")));

    MainWindow window;
    window.restoreWorkspace();
    QStringList const args = QApplication::arguments();
    if (args.size() > 1) {
        window.show();
        window.openFile(args[1]);
    } else if (WelcomeDialog::shownAtStartup()) {
        window.onActionShowWelcome();  // shows the window once a scene is made or chosen
        if (!window.isVisible())
            return 0;  // the welcome window was closed: nothing was started
    } else {
        window.show();
    }
    return QApplication::exec();
}
