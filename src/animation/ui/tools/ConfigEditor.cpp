// Eclipse - the Configuration Editor (see ConfigEditor.h).
#include "animation/ui/tools/ConfigEditor.h"

#include "actions/manager/ActionManager.h"
#include "animation/ui/tools/CommandLine.h"
#include "animation/ui/tools/ServiceLauncher.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

bool hasScreen()
{
    return QApplication::platformName() != QLatin1String("offscreen") && QApplication::platformName() != QLatin1String("minimal");
}

}  // namespace

ConfigEditorWindow::ConfigEditorWindow(QWidget* parent) : QMainWindow(parent)
{
    setObjectName(QStringLiteral("ConfigEditorWindow"));
    setWindowTitle(tr("Configuration Editor"));
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("ConfigTabs"));

    // The files.
    QWidget* const files = new QWidget(m_tabs);
    QVBoxLayout* const layout = new QVBoxLayout(files);
    QSplitter* const split = new QSplitter(Qt::Horizontal, files);
    m_list = new QListWidget(split);
    m_list->setObjectName(QStringLiteral("ConfigFiles"));
    m_table = new QTableWidget(0, 3, split);
    m_table->setObjectName(QStringLiteral("ConfigTable"));
    m_table->setHorizontalHeaderLabels({tr("Section"), tr("Key"), tr("Value")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    split->addWidget(m_list);
    split->addWidget(m_table);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    QHBoxLayout* const buttons = new QHBoxLayout;
    auto button = [&](QString const& text, QString const& name, auto slot) {
        QPushButton* const b = new QPushButton(text, files);
        b->setObjectName(name);
        buttons->addWidget(b);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    button(tr("Open..."), QStringLiteral("ConfigOpen"), &ConfigEditorWindow::openOther);
    button(tr("Add Key..."), QStringLiteral("ConfigAddKey"), &ConfigEditorWindow::addKey);
    button(tr("Remove Key"), QStringLiteral("ConfigRemoveKey"), &ConfigEditorWindow::removeCurrentKey);
    buttons->addStretch(1);
    button(tr("Revert"), QStringLiteral("ConfigRevert"), [this] { reload(currentFile()); });
    button(tr("Save"), QStringLiteral("ConfigSave"), &ConfigEditorWindow::saveCurrent);
    button(tr("Save All"), QStringLiteral("ConfigSaveAll"), [this] { saveAll(); });
    layout->addLayout(buttons);
    m_tabs->addTab(files, tr("Files"));

    // Eclipse's preferences.
    m_preferences = new QTableWidget(0, 2, m_tabs);
    m_preferences->setObjectName(QStringLiteral("ConfigPreferences"));
    m_preferences->setHorizontalHeaderLabels({tr("Preference"), tr("Value")});
    m_preferences->horizontalHeader()->setStretchLastSection(true);
    m_tabs->addTab(m_preferences, tr("Eclipse Preferences"));
    setCentralWidget(m_tabs);
    statusBar();

    QMenu* const fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &ConfigEditorWindow::openOther);
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &ConfigEditorWindow::saveCurrent);
    fileMenu->addAction(tr("Save &All"), this, [this] { saveAll(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigEditorWindow::fileChangedOutside);
    connect(m_list, &QListWidget::currentRowChanged, this, &ConfigEditorWindow::showFile);
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (m_filling || column != 2)
            return;
        setValue(m_table->item(row, 0)->text(), m_table->item(row, 1)->text(), m_table->item(row, 2)->text());
    });
    connect(m_preferences, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (m_filling || column != 1)
            return;
        setPreference(m_preferences->item(row, 0)->text(), m_preferences->item(row, 1)->text());
    });

    if (!applicationFilesFound())
        tell(tr("Cannot find application files. Make sure this software is correctly installed."));
    for (QString const& path : defaultFiles())
        if (QFileInfo::exists(path))
            openFile(path);
    if (fileCount() > 0)
        m_list->setCurrentRow(0);
    fillPreferences();
    resize(900, 600);
}

QStringList ConfigEditorWindow::defaultFiles()
{
    QString const resources = ActionManager::defaultDirectory();
    return {resources + QStringLiteral("/manager.conf"), resources + QStringLiteral("/vectorize.conf"), resources + QStringLiteral("/version.txt"),
            ServiceLauncher::defaultConfigFile()};
}

bool ConfigEditorWindow::applicationFilesFound()
{
    return QFileInfo(ActionManager::defaultDirectory()).isDir() && QFileInfo::exists(ActionManager::defaultDirectory() + QStringLiteral("/menus.xml"));
}

int ConfigEditorWindow::openFile(QString const& path)
{
    QString const whole = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < fileCount(); ++i)
        if (filePath(i).compare(whole, Qt::CaseInsensitive) == 0) {
            setCurrentFile(i);
            return i;
        }
    Open open;
    QString error;
    if (!open.file.load(whole, &error)) {
        tell(tr("Could not read %1: %2").arg(QDir::toNativeSeparators(whole), error));
        return -1;
    }
    open.saved = open.file.text();
    m_files.push_back(open);
    m_watcher->addPath(whole);
    m_list->addItem(QFileInfo(whole).fileName());
    m_list->item(m_list->count() - 1)->setToolTip(QDir::toNativeSeparators(whole));
    return fileCount() - 1;
}

QString ConfigEditorWindow::filePath(int index) const
{
    return index >= 0 && index < fileCount() ? m_files[index].file.path() : QString();
}

bool ConfigEditorWindow::isModified(int index) const
{
    return index >= 0 && index < fileCount() && m_files[index].file.text() != m_files[index].saved;
}

int ConfigEditorWindow::currentFile() const
{
    return m_list->currentRow();
}

void ConfigEditorWindow::setCurrentFile(int index)
{
    m_list->setCurrentRow(index);
}

void ConfigEditorWindow::showFile(int index)
{
    Q_UNUSED(index);
    fillTable();
}

void ConfigEditorWindow::fillTable()
{
    m_filling = true;
    m_table->setRowCount(0);
    int const index = currentFile();
    if (index >= 0 && index < fileCount()) {
        ConfigFile const& conf = m_files[index].file;
        for (QString const& section : conf.sections())
            for (QString const& key : conf.keys(section)) {
                int const row = m_table->rowCount();
                m_table->insertRow(row);
                QTableWidgetItem* const s = new QTableWidgetItem(section);
                QTableWidgetItem* const k = new QTableWidgetItem(key);
                s->setFlags(s->flags() & ~Qt::ItemIsEditable);
                k->setFlags(k->flags() & ~Qt::ItemIsEditable);
                QString const note = conf.note(section, key);
                for (QTableWidgetItem* item : {s, k})
                    item->setToolTip(note);
                m_table->setItem(row, 0, s);
                m_table->setItem(row, 1, k);
                m_table->setItem(row, 2, new QTableWidgetItem(conf.value(section, key)));
            }
    }
    m_filling = false;
}

void ConfigEditorWindow::refreshNames()
{
    for (int i = 0; i < fileCount() && i < m_list->count(); ++i)
        m_list->item(i)->setText(QFileInfo(filePath(i)).fileName() + (isModified(i) ? QStringLiteral(" *") : QString()));
}

void ConfigEditorWindow::setValue(QString const& section, QString const& key, QString const& value)
{
    int const index = currentFile();
    if (index < 0)
        return;
    m_files[index].file.setValue(section, key, value);
    refreshNames();
    if (!m_filling)
        fillTable();
}

void ConfigEditorWindow::removeKey(QString const& section, QString const& key)
{
    int const index = currentFile();
    if (index < 0)
        return;
    m_files[index].file.remove(section, key);
    refreshNames();
    fillTable();
}

bool ConfigEditorWindow::saveFile(int index, QString* error)
{
    if (index < 0 || index >= fileCount())
        return false;
    Open& open = m_files[index];
    // Not read again as a change from outside.
    m_watcher->removePath(open.file.path());
    QString why;
    bool const ok = open.file.save(QString(), &why);
    m_watcher->addPath(open.file.path());
    if (!ok) {
        if (error)
            *error = why;
        tell(tr("Could not save %1: %2").arg(QDir::toNativeSeparators(open.file.path()), why));
        return false;
    }
    open.saved = open.file.text();
    refreshNames();
    tell(tr("Saved %1").arg(QDir::toNativeSeparators(open.file.path())));
    return true;
}

int ConfigEditorWindow::saveAll()
{
    int failed = 0;
    for (int i = 0; i < fileCount(); ++i)
        if (isModified(i) && !saveFile(i))
            ++failed;
    if (failed == 0)
        tell(tr("All saved"));
    return failed;
}

void ConfigEditorWindow::reload(int index)
{
    if (index < 0 || index >= fileCount())
        return;
    Open& open = m_files[index];
    QString const path = open.file.path();
    open.file.load(path);
    open.saved = open.file.text();
    refreshNames();
    if (index == currentFile())
        fillTable();
}

void ConfigEditorWindow::fileChangedOutside(QString const& path)
{
    // (A file saved by replacing it drops out of the watcher: watched again.)
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
    for (int i = 0; i < fileCount(); ++i) {
        if (filePath(i).compare(QFileInfo(path).absoluteFilePath(), Qt::CaseInsensitive) != 0)
            continue;
        ConfigFile now(path);
        if (now.text() == m_files[i].saved)
            return;
        bool readAgain = true;
        if (isModified(i)) {
            readAgain = !hasScreen() ? m_answer
                                  : QMessageBox::question(this, tr("File Changed"),
                                                          tr("%1 was changed outside the Configuration Editor. Read it again, losing the changes made here?")
                                                              .arg(QDir::toNativeSeparators(path)))
                                        == QMessageBox::Yes;
        }
        if (readAgain) {
            reload(i);
            tell(tr("File Changed: %1 was read again.").arg(QFileInfo(path).fileName()));
        } else {
            tell(tr("File Changed: %1 was changed outside; the changes made here are kept.").arg(QFileInfo(path).fileName()));
        }
        return;
    }
}

QStringList ConfigEditorWindow::preferenceKeys() const
{
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    settings.beginGroup(QStringLiteral("prefs"));
    QStringList keys = settings.allKeys();
    keys.sort(Qt::CaseInsensitive);
    return keys;
}

void ConfigEditorWindow::fillPreferences()
{
    m_filling = true;
    m_preferences->setRowCount(0);
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    for (QString const& key : preferenceKeys()) {
        QVariant const value = settings.value(QStringLiteral("prefs/") + key);
        // Only what can be written as text.
        if (!value.canConvert<QString>() || value.typeId() == QMetaType::QByteArray)
            continue;
        int const row = m_preferences->rowCount();
        m_preferences->insertRow(row);
        QTableWidgetItem* const name = new QTableWidgetItem(key);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_preferences->setItem(row, 0, name);
        m_preferences->setItem(row, 1, new QTableWidgetItem(value.toString()));
    }
    m_filling = false;
}

void ConfigEditorWindow::setPreference(QString const& key, QString const& value)
{
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QVariant const before = settings.value(QStringLiteral("prefs/") + key);
    QVariant now = value;
    // The kind it had kept: a number stays a number, a yes/no a yes/no.
    if (before.typeId() == QMetaType::Bool)
        now = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value == QLatin1String("1");
    else if (before.typeId() == QMetaType::Int)
        now = value.toInt();
    else if (before.typeId() == QMetaType::Double)
        now = value.toDouble();
    settings.setValue(QStringLiteral("prefs/") + key, now);
    tell(tr("%1 is now %2 (Eclipse takes it when it starts again)").arg(key, value));
}

void ConfigEditorWindow::openOther()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Open Configuration File"), ActionManager::defaultDirectory(),
                                                      tr("Configuration files (*.conf *.ini *.cfg *.txt);;All Files (*)"));
    if (path.isEmpty())
        return;
    int const index = openFile(path);
    if (index >= 0)
        setCurrentFile(index);
}

void ConfigEditorWindow::saveCurrent()
{
    saveFile(currentFile());
}

void ConfigEditorWindow::addKey()
{
    int const index = currentFile();
    if (index < 0)
        return;
    QString const section = QInputDialog::getText(this, tr("Add Key"), tr("Section (empty: none):"));
    QString const key = QInputDialog::getText(this, tr("Add Key"), tr("Key:"));
    if (key.trimmed().isEmpty())
        return;
    setValue(section.trimmed(), key.trimmed(), QString());
}

void ConfigEditorWindow::removeCurrentKey()
{
    int const row = m_table->currentRow();
    if (row < 0)
        return;
    removeKey(m_table->item(row, 0)->text(), m_table->item(row, 1)->text());
}

void ConfigEditorWindow::tell(QString const& message)
{
    m_lastMessage = message;
    statusBar()->showMessage(message, 8000);
}
