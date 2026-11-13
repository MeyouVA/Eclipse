#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QSplashScreen>

#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"

#include "animation/ui/window/MainWindow.h"
#include "board/ui/BoardWindow.h"
#include "launcher/WelcomeDialog.h"

int main(int argc, char** argv)
{
    // (Preferences: the size of everything, before the application starts.)
    {
        QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
        int const scale = s.value(QStringLiteral("prefs/interfaceScale"), 100).toInt();
        if (scale != 100 && scale >= 50 && scale <= 300 && !qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(scale / 100.0));
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Eclipse"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    // The dark look, from the stylesheet and the icons in `resources`.
    QString error;
    QSettings const prefs(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    // The language of the interface (Preferences > General), before anything with words in it is made.
    MainWindow::installLanguage(prefs.value(QStringLiteral("prefs/language")).toString());
    QString const look = prefs.value(QStringLiteral("prefs/darkStyle"), true).toBool() ? QStringLiteral("dark") : QStringLiteral("light");
    if (!Theme::apply(app, ActionManager::defaultDirectory(), look, &error))
        QMessageBox::warning(nullptr, QStringLiteral("Eclipse"), error);
    // The user's own additions to the look (Preferences > Style Editor), after it.
    app.setProperty("eclipseBaseStyle", app.styleSheet());
    if (QString const extra = prefs.value(QStringLiteral("prefs/styleExtra")).toString(); !extra.isEmpty())
        app.setStyleSheet(app.styleSheet() + QLatin1Char('\n') + extra);

    // The icon of its windows (the program file has the same one built in).
    QApplication::setWindowIcon(QIcon(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png")));

    // Preferences > General: Eclipse's picture while it starts.
    QSplashScreen* splash = nullptr;
    if (prefs.value(QStringLiteral("prefs/welcomeImage"), false).toBool()) {
        QPixmap picture(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png"));
        splash = new QSplashScreen(picture.isNull() ? picture : picture.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        splash->showMessage(QStringLiteral("Eclipse %1").arg(QApplication::applicationVersion()), Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        splash->show();
        QApplication::processEvents();
    }

    delete splash;

    QString path;
    QString helpTopic;
    Resolution resolution;
    auto choice = WelcomeDialog::Nothing;
    bool storyboard = false;
    QStringList const args = QApplication::arguments();
    if (args.size() > 1) {
        path = args[1];
        storyboard = QFileInfo(path).suffix().compare(QLatin1String("ecb"), Qt::CaseInsensitive) == 0;
        choice = storyboard ? WelcomeDialog::OpenBoardProject : WelcomeDialog::OpenScene;
    } else if (prefs.value(QStringLiteral("welcome/hide"), false).toBool()) {
        storyboard = prefs.value(QStringLiteral("welcome/lastApp")).toString() == QLatin1String("board");
    } else {
        WelcomeDialog welcome(ActionManager::defaultDirectory(), WelcomeDialog::Mode::Launcher);
        if (welcome.exec() != QDialog::Accepted)
            return 0;
        choice = welcome.choice();
        path = welcome.path();
        helpTopic = welcome.helpTopic();
        resolution = welcome.resolution();
        storyboard = choice == WelcomeDialog::OpenBoardProject;
    }

    if (storyboard) {
        BoardWindow window;
        if (!path.isEmpty() && !window.openProject(path))
            return 1;
        QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("welcome/lastApp"), QStringLiteral("board"));
        window.show();
        return QApplication::exec();
    }

    MainWindow window;
    window.restoreWorkspace();
    window.startupChecks();
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("welcome/lastApp"), QStringLiteral("animation"));
    window.show();
    if (args.size() == 1)
        QApplication::processEvents();
    if (choice == WelcomeDialog::OpenScene) {
        if (!window.openFile(path) && args.size() > 1)
            return 1;
    } else if (choice == WelcomeDialog::CreateScene)
        window.createScene(path, resolution.width, resolution.height, resolution.fps, resolution.fov);
    else if (choice == WelcomeDialog::ShowHelp)
        window.showHelp(helpTopic == QLatin1String("index.html") ? QString() : helpTopic);
    return QApplication::exec();
}
