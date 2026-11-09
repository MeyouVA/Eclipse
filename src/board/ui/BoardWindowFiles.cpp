#include "board/ui/BoardWindow.h"
#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "launcher/WelcomeDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>

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
    rememberProjectPath();
    selectPanel(0, 0, true);
    if (m_document.isLegacyProject())
        QTimer::singleShot(0, this, [this, path = m_document.path()] { offerLegacyConversion(path); });
    return true;
}

void BoardWindow::convertLegacyProject()
{
    QString const path = m_document.path();
    offerLegacyConversion(path);
}

void BoardWindow::offerLegacyConversion(QString const& path)
{
    if (m_document.path() != path || !m_document.isLegacyProject())
        return;
    QMessageBox prompt(QMessageBox::Question, tr("Convert Legacy Board"),
        tr("This board uses the older single-file layout. Convert it in its current folder? "
           "Eclipse will update the .ecb file in place and store its artwork in that folder."),
        QMessageBox::NoButton, this);
    prompt.setObjectName(QStringLiteral("BoardLegacyConversion"));
    auto* convert = prompt.addButton(tr("Convert Project"), QMessageBox::AcceptRole);
    auto* open = prompt.addButton(tr("Open Original"), QMessageBox::RejectRole);
    prompt.setDefaultButton(open);
    prompt.setEscapeButton(open);
    prompt.exec();
    if (prompt.clickedButton() == convert && m_document.path() == path && m_document.isLegacyProject() && saveChanges())
        statusBar()->showMessage(tr("Project converted in its current folder."), 5000);
}

void BoardWindow::rememberProjectPath()
{
    QString const absolute = m_document.path();
    setWindowFilePath(absolute);
    QSettings prefs = settings();
    QStringList recent = prefs.value(QStringLiteral("board/recentProjects")).toStringList();
    recent.removeIf([&absolute](QString const& item) { return item.compare(absolute, Qt::CaseInsensitive) == 0; });
    recent.prepend(absolute);
    while (recent.size() > 12)
        recent.removeLast();
    prefs.setValue(QStringLiteral("board/recentProjects"), recent);
}

bool BoardWindow::saveChanges()
{
    QString error;
    if (!m_document.save(&error)) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), error);
        return false;
    }
    updateProjectState();
    statusBar()->showMessage(tr("Project saved."), 2500);
    return true;
}

void BoardWindow::saveProject() { saveChanges(); }

void BoardWindow::closeProject()
{
    if (m_document.path().isEmpty() || !confirmDiscard())
        return;
    showCameraPreview(false);
    m_actions->setChecked(QStringLiteral("BoardCameraPreview"), false);
    m_document.close();
    setWindowFilePath(QString());
}

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
    auto const recent = settings().value(QStringLiteral("board/recentProjects")).toStringList();
    for (QString const& path : recent) {
        QFileInfo const info(path);
        if (info.isFile()) {
            auto* action = menu->addAction(info.completeBaseName().replace(QLatin1String("&"), QLatin1String("&&")));
            action->setToolTip(QDir::toNativeSeparators(path));
            connect(action, &QAction::triggered, this, [this, path] { openProject(path); });
        }
    }
    if (menu->isEmpty())
        menu->addAction(tr("No recent projects"))->setEnabled(false);
    menu->addSeparator();
    auto* clear = menu->addAction(tr("Clear Recent Projects"));
    clear->setEnabled(!recent.isEmpty());
    connect(clear, &QAction::triggered, this, [] { settings().remove(QStringLiteral("board/recentProjects")); });
}
