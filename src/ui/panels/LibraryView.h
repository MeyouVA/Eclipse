// Eclipse - the Library: templates kept in folders, to be used in any scene.
//
// A template is a piece of a scene - some layers over some frames - saved as a file of its
// own (.ecltpl).  At the left are the folders the library looks in; at the right the
// templates of the folder chosen, each with a picture; above them the template chosen,
// larger, with a slider to look through its frames.
#pragma once

#include "EclipseExport.h"

#include <QHash>
#include <QImage>
#include <QWidget>

#include "model/scene/Scene.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSlider;

class ECLIPSE_UI_API LibraryView : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    static QString const kSuffix;       // "ecltpl"
    static QString const kMimeType;     // what a template dragged out of the view is carried as (its path)
    // Where templates go unless another folder is added: "Eclipse Library" in Documents.
    static QString defaultFolder();
    // The first frame on which the scene shows a drawing (0 when it never does): the frame
    // a template is pictured by, so that one that starts empty is not a blank square.
    static int firstDrawnFrame(Scene const& scene);
    // A picture of a frame of a scene, to fit `size`.
    static QImage picture(Scene const& scene, int frame, QSize size);

    // The folders looked in.  The first is the default one and stays.
    QStringList folders() const { return m_folders; }
    void setFolders(QStringList const& folders);
    bool addFolder(QString const& folder);
    void removeFolder(int index);
    QString currentFolder() const;
    void setCurrentFolder(int index);

    // The templates of the current folder, as shown (after the search), and the one chosen.
    QStringList templates() const;
    QString currentTemplate() const;
    void setCurrentTemplate(QString const& path);
    // Reads the folder again (a template was saved or deleted).
    void refresh();
    // Deletes the file of the chosen template; with `ask`, after asking.
    bool deleteCurrent(bool ask);
    // The template chosen gets another name (its file is renamed); false when the name will not do or is taken.
    bool renameCurrent(QString const& name);
    void setSearch(QString const& text);

signals:
    // Insert pressed, or a template double-clicked.
    void insertRequested(QString const& path);
    // "New Template from Selection" pressed.
    void createRequested();

private:
    void showFolders();
    void showTemplate();
    void showFrame(int frame);
    void remember();

    QStringList m_folders;
    QListWidget* m_folderList;
    QListWidget* m_items;
    QLineEdit* m_search;
    QLabel* m_preview;
    QLabel* m_about;
    QSlider* m_scrub;
    Scene m_shown;              // the template chosen, read
    QString m_shownPath;
    QHash<QString, QImage> m_thumbnails;    // by path and time of the file
};
