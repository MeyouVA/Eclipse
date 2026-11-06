// Eclipse - the Configuration Editor (eclipse-config): the configuration files of the programs beside Eclipse
// (manager.conf, vectorize.conf, version.txt, the Service Launcher's services.conf, and any other opened) as tables of
// section, key and value, changed and saved (Save, Save All) with their notes kept; a file changed outside while it is
// open is read again (asked first when it was changed here too: File Changed). A second page shows Eclipse's own
// preferences, to look at and change.
#pragma once

#include "EclipseExport.h"

#include <QMainWindow>
#include <QVector>

#include "ui/tools/ConfigFile.h"

class QFileSystemWatcher;
class QListWidget;
class QTableWidget;
class QTabWidget;

class ECLIPSE_UI_API ConfigEditorWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ConfigEditorWindow(QWidget* parent = nullptr);

    // The files it opens with; and whether the programs' files are there at all.
    static QStringList defaultFiles();
    static bool applicationFilesFound();

    int openFile(QString const& path);
    int fileCount() const { return int(m_files.size()); }
    QString filePath(int index) const;
    bool isModified(int index) const;
    int currentFile() const;
    void setCurrentFile(int index);
    ConfigFile const& file(int index) const { return m_files[index].file; }

    // A value of the file shown changed (as the table does it), a key added or taken away.
    void setValue(QString const& section, QString const& key, QString const& value);
    void removeKey(QString const& section, QString const& key);

    bool saveFile(int index, QString* error = nullptr);
    // Every changed file saved; how many could not be.
    int saveAll();
    void reload(int index);

    // A file changed outside: read again - asking first when it was changed here too (the answer `answer` gives where
    // no question can be shown: true reads it again).
    void fileChangedOutside(QString const& path);
    void setAnswerWithoutScreen(bool readAgain) { m_answer = readAgain; }

    // Eclipse's preferences (prefs/...): what they are, and one changed.
    QStringList preferenceKeys() const;
    void setPreference(QString const& key, QString const& value);
    QString lastMessage() const { return m_lastMessage; }

public slots:
    void openOther();
    void saveCurrent();
    void addKey();
    void removeCurrentKey();

private:
    struct Open {
        ConfigFile file;
        QString saved;      // its text as last read or saved
    };
    void showFile(int index);
    void fillTable();
    void fillPreferences();
    void refreshNames();
    void tell(QString const& message);
    QVector<Open> m_files;
    QListWidget* m_list;
    QTableWidget* m_table;
    QTableWidget* m_preferences;
    QTabWidget* m_tabs;
    QFileSystemWatcher* m_watcher;
    bool m_filling = false;
    bool m_answer = true;
    QString m_lastMessage;
};
