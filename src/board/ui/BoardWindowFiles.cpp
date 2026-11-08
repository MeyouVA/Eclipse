#include "board/ui/BoardWindow.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "launcher/WelcomeDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

bool BoardWindow::openProject(QString const& path)
{
    return confirmDiscard() && loadProject(path);
}

bool BoardWindow::loadProject(QString const& path)
{
    QString error;
    if (!m_document.open(path, &error)) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"),
                             tr("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    QString const absolute = m_document.path();
    setWindowFilePath(absolute);
    selectPanel(0, 0, true);

    QSettings prefs = settings();
    QStringList recent = prefs.value(QStringLiteral("board/recentProjects")).toStringList();
    recent.removeIf([&absolute](QString const& item) { return item.compare(absolute, Qt::CaseInsensitive) == 0; });
    recent.prepend(absolute);
    while (recent.size() > 12)
        recent.removeLast();
    prefs.setValue(QStringLiteral("board/recentProjects"), recent);
    return true;
}

bool BoardWindow::saveChanges()
{
    QString error;
    if (!m_document.save(&error)) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), error);
        return false;
    }
    statusBar()->showMessage(tr("Project saved."), 2500);
    return true;
}

void BoardWindow::saveProject() { saveChanges(); }

bool BoardWindow::confirmDiscard()
{
    if (m_document.undoStack()->isClean())
        return true;
    auto const choice = QMessageBox::warning(this, tr("Eclipse Storyboard"), tr("Save changes to the current project?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && saveChanges());
}

void BoardWindow::showWelcome()
{
    if (!confirmDiscard())
        return;
    WelcomeDialog welcome(ActionManager::defaultDirectory(), WelcomeDialog::Mode::Storyboard, this);
    if (welcome.exec() == QDialog::Accepted)
        loadProject(welcome.path());
}

void BoardWindow::chooseProject()
{
    QString const directory = windowFilePath().isEmpty()
        ? settings().value(QStringLiteral("board/welcome/directory")).toString()
        : QFileInfo(windowFilePath()).absolutePath();
    QString const path = QFileDialog::getOpenFileName(this, tr("Open Project"), directory, tr("Eclipse Storyboard (*.ecb)"));
    if (!path.isEmpty())
        openProject(path);
}

void BoardWindow::showRecent(QMenu* menu)
{
    menu->clear();
    for (QString const& path : settings().value(QStringLiteral("board/recentProjects")).toStringList()) {
        QFileInfo const info(path);
        if (info.isFile()) {
            auto* action = menu->addAction(info.completeBaseName().replace(QLatin1String("&"), QLatin1String("&&")));
            action->setToolTip(QDir::toNativeSeparators(path));
            connect(action, &QAction::triggered, this, [this, path] { openProject(path); });
        }
    }
    if (menu->isEmpty())
        menu->addAction(tr("No recent projects"))->setEnabled(false);
}
