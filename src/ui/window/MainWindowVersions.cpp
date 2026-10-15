// Eclipse - the main window: saving a new version, and backing a scene up.
#include "ui/window/MainWindow.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

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
    if (!m_document->wholeScene().save(copy, &error)) {
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

QStringList MainWindow::sceneVersions() const
{
    // name.ecl, name_v2.ecl, name_v3.ecl ...: the first and those after it, in order.
    QStringList versions;
    if (m_path.isEmpty())
        return versions;
    QFileInfo const info(m_path);
    QString stem = info.completeBaseName();
    static QRegularExpression const tail(QStringLiteral("_v(\\d+)$"));
    if (QRegularExpressionMatch const m = tail.match(stem); m.hasMatch())
        stem.chop(int(m.capturedLength()));
    QDir const dir = info.dir();
    QMap<int, QString> found;
    if (QFileInfo::exists(dir.filePath(stem + QLatin1Char('.') + info.suffix())))
        found.insert(1, dir.filePath(stem + QLatin1Char('.') + info.suffix()));
    static QRegularExpression const name(QStringLiteral("^(.*)_v(\\d+)$"));
    for (QFileInfo const& f : dir.entryInfoList({stem + QStringLiteral("_v*.") + info.suffix()}, QDir::Files)) {
        QRegularExpressionMatch const m = name.match(f.completeBaseName());
        if (m.hasMatch() && m.captured(1) == stem)
            found.insert(m.captured(2).toInt(), f.absoluteFilePath());
    }
    for (QString const& path : std::as_const(found))
        versions << path;
    return versions;
}

void MainWindow::onActionManageVersions()
{
    if (m_path.isEmpty()) {
        QMessageBox::information(this, tr("Manage Versions"), tr("Save the scene first: its versions are kept beside it."));
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ManageVersions"));
    dialog.setWindowTitle(tr("Manage Versions"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("VersionList"));
    auto fill = [&, this] {
        list->clear();
        for (QString const& path : sceneVersions()) {
            QFileInfo const f(path);
            QListWidgetItem* const item = new QListWidgetItem(tr("%1   (saved %2)%3").arg(f.fileName(), QLocale().toString(f.lastModified(), QLocale::ShortFormat),
                                                                                    QFileInfo(path) == QFileInfo(m_path) ? tr("   - open now") : QString()),
                                                              list);
            item->setData(Qt::UserRole, path);
        }
    };
    fill();
    auto current = [list] { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString(); };
    QPushButton* const open = new QPushButton(tr("Open Version"), &dialog);
    open->setObjectName(QStringLiteral("VersionOpen"));
    QPushButton* const remove = new QPushButton(tr("Delete Version"), &dialog);
    remove->setObjectName(QStringLiteral("VersionDelete"));
    connect(open, &QPushButton::clicked, &dialog, [&, this] {
        QString const path = current();
        if (path.isEmpty() || !confirmDiscard())
            return;
        dialog.accept();
        openFile(path);
    });
    connect(list, &QListWidget::itemDoubleClicked, open, &QPushButton::click);
    connect(remove, &QPushButton::clicked, &dialog, [&, this] {
        QString const path = current();
        if (path.isEmpty())
            return;
        if (QFileInfo(path) == QFileInfo(m_path)) {
            QMessageBox::information(&dialog, tr("Delete Version"), tr("This version is the one open: open another first."));
            return;
        }
        if (QMessageBox::question(&dialog, tr("Delete Version"), tr("Really delete version %1 of this scene? It goes to the Recycle Bin.").arg(QFileInfo(path).fileName()))
            != QMessageBox::Yes)
            return;
        QFile::moveToTrash(path);
        fill();
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->addButton(open, QDialogButtonBox::ActionRole);
    buttons->addButton(remove, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("The versions of %1 (File > Save as New Version makes a new one):").arg(QFileInfo(m_path).completeBaseName()), &dialog));
    layout->addWidget(list, 1);
    layout->addWidget(buttons);
    dialog.resize(480, 300);
    dialog.exec();
}

void MainWindow::onActionMarkCurrentFrame()
{
    int const f = frame();
    m_document->addMarker(f, 1, tr("Frame %1").arg(f + 1), QColor(240, 200, 60));
}

QStringList MainWindow::backupFiles() const
{
    // The copies kept beside the scene: name.ecl~ (as it was before the last save) and name_backup_<date>.ecl (Backup Scene).
    QStringList found;
    if (m_path.isEmpty())
        return found;
    QFileInfo const info(m_path);
    if (QFileInfo::exists(m_path + QLatin1Char('~')))
        found << m_path + QLatin1Char('~');
    for (QFileInfo const& f : info.dir().entryInfoList({info.completeBaseName() + QStringLiteral("_backup_*.") + info.suffix()}, QDir::Files))
        found << f.absoluteFilePath();
    return found;
}

void MainWindow::onActionDeleteBackupFiles()
{
    QStringList const backups = backupFiles();
    if (backups.isEmpty()) {
        QMessageBox::information(this, tr("Delete Backup Files"), tr("This scene has no backup files."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete Backup Files"),
                              tr("Move the %n backup file(s) of this scene to the Recycle Bin?", nullptr, int(backups.size())))
        != QMessageBox::Yes)
        return;
    for (QString const& file : backups)
        QFile::moveToTrash(file);
}
