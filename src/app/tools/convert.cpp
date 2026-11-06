// Eclipse - eclipse-convert: the file converters (see ui/tools/FileConvert.h).
#include <QGuiApplication>

#include "ui/tools/CommandLine.h"
#include "ui/tools/FileConvert.h"

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen;windows");    // (no window; the usual platform if offscreen is not there)
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("eclipse-convert"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    return FileConvert::run(QGuiApplication::arguments().mid(1), [](QString const& line, bool isError) {
        if (isError)
            CommandLine::printError(line);
        else
            CommandLine::print(line);
    });
}
