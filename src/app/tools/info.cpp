// Eclipse - eclipse-info: what files are, the version, the computer and the state of the work (see ui/tools/ToolInfo.h).
#include <QGuiApplication>

#include "ui/tools/CommandLine.h"
#include "ui/tools/ToolInfo.h"

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen;windows");    // (no window; the usual platform if offscreen is not there)
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("eclipse-info"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    return ToolInfo::run(QGuiApplication::arguments().mid(1), [](QString const& line, bool isError) {
        if (isError)
            CommandLine::printError(line);
        else
            CommandLine::print(line);
    });
}
