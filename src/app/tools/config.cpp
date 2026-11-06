// Eclipse - eclipse-config: the Configuration Editor (see ui/tools/ConfigEditor.h).
#include <QApplication>
#include <QIcon>
#include <QSettings>

#include "actions/manager/ActionManager.h"
#include "actions/theme/Theme.h"
#include "ui/tools/ConfigEditor.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Configuration Editor"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QSettings const prefs(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    Theme::apply(app, ActionManager::defaultDirectory(), prefs.value(QStringLiteral("prefs/darkStyle"), true).toBool() ? QStringLiteral("dark") : QStringLiteral("light"), nullptr);
    QApplication::setWindowIcon(QIcon(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png")));
    ConfigEditorWindow window;
    // Files named on the command line are opened too.
    for (QString const& file : QApplication::arguments().mid(1))
        if (int const index = window.openFile(file); index >= 0)
            window.setCurrentFile(index);
    window.show();
    return QApplication::exec();
}
