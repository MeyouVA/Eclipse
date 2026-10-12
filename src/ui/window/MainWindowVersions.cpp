// Eclipse - the main window: saving a new version, and backing a scene up.
#include "ui/window/MainWindow.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/Xsheet.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

QString MainWindow::nextVersionPath(QString const& path)
{
    // name.ecl -> name_v2.ecl -> name_v3.ecl ..., past any that are there already.
    QFileInfo const info(path);
    QString stem = info.completeBaseName();
    int version = 1;
    static QRegularExpression const tail(QStringLiteral("_v(\\d+)$"));
    if (QRegularExpressionMatch const m = tail.match(stem); m.hasMatch()) {
        version = m.captured(1).toInt();
        stem.chop(int(m.capturedLength()));
    }
    QString next;
    do {
        ++version;
        next = info.dir().filePath(QStringLiteral("%1_v%2.%3").arg(stem).arg(version).arg(info.suffix()));
    } while (QFileInfo::exists(next));
    return next;
}

bool MainWindow::onActionSaveNewVersion()
{
    if (m_path.isEmpty())
        return onActionSaveAs();
    return saveTo(nextVersionPath(m_path));
}

bool MainWindow::backupScene(QString* madePath)
{
    // A copy of the scene as it is now, beside it; the scene being worked on stays the same file.
    if (m_path.isEmpty())
        return false;
    QFileInfo const info(m_path);
    QString const copy = info.dir().filePath(QStringLiteral("%1_backup_%2.%3")
                                                 .arg(info.completeBaseName(), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")), info.suffix()));
    QString error;
    if (!m_document->scene().save(copy, &error)) {
        QMessageBox::critical(this, tr("Eclipse"), tr("Could not save %1:\n%2").arg(QDir::toNativeSeparators(copy), error));
        return false;
    }
    if (madePath)
        *madePath = copy;
    statusBar()->showMessage(tr("Backup saved as %1").arg(QDir::toNativeSeparators(copy)), 5000);
    return true;
}

void MainWindow::onActionBackupScene()
{
    if (m_path.isEmpty()) {
        statusBar()->showMessage(tr("Save the scene first: a backup goes beside it."), 5000);
        return;
    }
    backupScene(nullptr);
}
