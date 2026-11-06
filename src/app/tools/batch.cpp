// Eclipse - eclipse-batch: scenes rendered and scripts run on them without a window (see ui/tools/BatchRun.h).
#include <QApplication>

#include "ui/tools/BatchRun.h"
#include "ui/tools/CommandLine.h"

int main(int argc, char** argv)
{
    // No window is ever shown: the scripts' views and dialogs are made off the screen.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen;windows");    // (no window; the usual platform if offscreen is not there)
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("eclipse-batch"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    BatchRun::Options options;
    QString error;
    if (!BatchRun::parse(QApplication::arguments().mid(1), options, &error)) {
        CommandLine::printError(error);
        CommandLine::printError(QApplication::translate("BatchRun", "eclipse-batch -help shows how it is used."));
        return 2;
    }
    return BatchRun::run(options, [&options](QString const& line, bool isError) {
        if (isError)
            CommandLine::printError(line);
        else if (!options.quiet || line.startsWith(QStringLiteral("Options file used")))
            CommandLine::print(line);
    });
}
