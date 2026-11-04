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
#include <QSet>
#include <QWidget>
#include <functional>
#include <memory>

#include "model/scene/Scene.h"

class Document;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSlider;
class QLockFile;

class ECLIPSE_UI_API LibraryView : public QWidget {
    Q_OBJECT

public:
    explicit LibraryView(QWidget* parent = nullptr);

    static QString const kSuffix;       // "ecltpl"
    static QString const kMimeType;     // what a template dragged out of the view is carried as (its path)
    static QString const kSymbolMimeType;   // what a scene symbol dragged out of the view is carried as (its id)
    static QString const kSymbolsFolder;    // the folder row that lists the scene's symbols
    // Where templates go unless another folder is added: "Eclipse Library" in Documents.
    static QString defaultFolder();
    static int firstDrawnFrame(Scene const& scene);
    // A picture of a frame of a scene, to fit `size`.
    static QImage picture(Scene const& scene, int frame, QSize size);

    // The folders looked in.  The first is the default one and stays.
    QStringList folders() const { return m_folders; }
    void setFolders(QStringList const& folders);
    // Open Library: a folder added (false, with `why`, when it is one already open or a part of one).
    bool addFolder(QString const& folder, QString* why = nullptr);
    void removeFolder(int index);
    QString currentFolder() const;
    void setCurrentFolder(int index);
    bool setCurrentPath(QString const& folder);
    bool addSubFolder(QString const& name);
    bool renameFolder(QString const& name);
    bool deleteFolder(bool ask);
    void refreshFolders();
    bool setRightToModify(bool enabled,QString* error=nullptr);
    bool rightToModify() const;
    bool canModifyPath(QString const& path,QString* error=nullptr);
    QStringList shownFolders() const;

    // The templates of the current folder, as shown (after the search), and the one chosen.
    QStringList templates() const;
    QString currentTemplate() const;
    void setCurrentTemplate(QString const& path);
    QStringList otherItems() const;
    static bool isTemplate(QString const& path);
    // A picture file the Library lists (an image it can read).
    static bool isPictureFile(QString const& path);
    // Reads the folder again (a template was saved or deleted).
    void refresh();
    // How the templates are shown: 0 icons with their names, 1 icons alone, 2 a list.
    void setItemsView(int mode);
    // How big the pictures of the templates are, as a percentage (50..200).
    void setThumbnailScale(int percent);
    QSize thumbnailSize() const;
    // Deletes the file of the chosen template; with `ask`, after asking.
    bool deleteCurrent(bool ask);
    // The template chosen gets another name (its file is renamed); false when the name will not do or is taken.
    bool renameCurrent(QString const& name);
    void setSearch(QString const& text);
    void generateThumbnails();
    void deleteThumbnails();
    void setAutoThumbnails(bool enabled);
    // The scene's symbols, as a folder of the Library (shown above the folders on disk).
    void setDocument(Document* document);
    bool showingSymbols() const;
    void showSceneSymbols(int pick = 0);
    // A symbol's id from the path an item carries ("symbol:<id>"), or 0.
    static int symbolIdOf(QString const& path);
    // Library Cut / Copy / Paste: the item chosen kept to be pasted into another folder (a cut takes it away once pasted).
    // A name taken there is asked for again through `rename` (none: " copy" added).
    bool copyCurrent(bool cut);
    static bool hasClipboard();
    bool pasteHere(QString* error, std::function<QString(QString const&)> const& rename = {});
    // Optimize Template: the template chosen without the drawings and palettes it does not use; `report` says what went.
    bool optimizeCurrent(bool ask, QString* report);
    // Preview Template: the template chosen played in a window of its own.
    void previewCurrent();
    // The folder of files packed with a template (Walk.files beside Walk.ecltpl) taken along when it is renamed, moved,
    // copied or deleted, and the template pointed at its new place.  Such folders are not listed as library folders.
    static bool isPackFolder(QString const& folder);
    static bool carryPackFolder(QString const& from, QString const& to, bool move);
    // Version Swapper Mode: while on, a template clicked goes onto the scene's layers of the same names (versionSwapRequested).
    void setVersionSwapper(bool on);
    bool versionSwapper() const { return m_versionSwapper; }
    // The Library's own keys (Preferences > Keyboard Shortcuts, Library): looked up in `actions`.
    void setActions(class ActionManager* actions) { m_actions = actions; }

protected:
    bool eventFilter(QObject* watched,QEvent* event) override;

signals:
    // Insert pressed, or a template double-clicked.
    void insertRequested(QString const& path);
    // "As Symbol" pressed: the template is to come in as a symbol, on one layer.
    void insertAsSymbolRequested(QString const& path);
    // "Paste Special..." pressed: the template is to come in the way a window asks.
    void pasteSpecialRequested(QString const& path);
    // "New Template from Selection" pressed.
    void createRequested();
    // The scene's symbols: put one in, a new one, another name, a copy, delete one.
    void insertSymbolRequested(int id);
    void newSymbolRequested();
    void renameSymbolRequested(int id, QString const& name);
    void copySymbolRequested(int id);
    void deleteSymbolRequested(int id);
    // Edit Template, Set Scale Factor..., and a template clicked in Version Swapper Mode.
    void editTemplateRequested(QString const& path);
    void scaleFactorRequested(QString const& path);
    void versionSwapRequested(QString const& path);
    // Replace Drawing: a picture file of the Library put in place of the current drawing.
    void replaceDrawingRequested(QString const& path);

private:
    void showFolders();
    void showTemplate();
    void showFrame(int frame);
    void remember();
    void setupManagement();
    void showManagementMenu(bool folder,QPoint const& point);
    QString rootFor(QString const& path) const;
    static bool validEntryName(QString const& name);
    void fillSymbols();
    bool showSymbol(QString const& path);
    QString symbolSignature() const;

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
    int m_thumbnailPercent = 100;           // Preferences: how big the thumbnails are
    QHash<QString,std::shared_ptr<QLockFile>> m_locks;
    QSet<QString> m_readOnly;
    bool m_forceThumbnails=false,m_suppressThumbnails=false;
    Document* m_document = nullptr;
    QString m_lastFileFolder;       // the folder on disk chosen last (what new templates go into while symbols are shown)
    QString m_symbolSignature;
    bool m_versionSwapper = false;
    class ActionManager* m_actions = nullptr;      // what the symbols listed were, to list them again only when they change
};
