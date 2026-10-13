// Eclipse - the action manager.
//
// Menus, toolbars and keyboard shortcuts are not written in the program: they are read from
// resources/menus.xml, toolbars.xml and shortcuts.xml.  Each command names a slot and the
// responders to try it on; a responder is an object registered here under a name (the main
// window, a view), or "focus" for whichever registered view has the keyboard focus.
// So one Delete command does the right thing in the timeline and in the camera view.
#pragma once

#include "EclipseExport.h"

#include <QHash>
#include <QIcon>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QAction;
class QActionGroup;
class QMenu;
class QMenuBar;
class QToolBar;
class QWidget;
class QXmlStreamReader;

class ECLIPSE_ACTIONS_API ActionManager : public QObject {
    Q_OBJECT

public:
    struct Shortcut {
        QString id;
        QString text;
        QString category;
        QKeySequence defaultKey;
        QKeySequence key;
    };

    explicit ActionManager(QObject* parent = nullptr);

    // Reads the three files from `directory`.  False, with a message, when one is missing
    // or malformed.
    bool load(QString const& directory, QString* error);
    // Where the files are looked for: the `resources` folder next to the program.
    static QString defaultDirectory();

    void registerResponder(QString const& name, QObject* responder);

    // ---- building ------------------------------------------------------------------------
    void populateMenuBar(QString const& menuId, QMenuBar* bar);
    // A menu of its own (a right-click menu); null when there is no menu with that id.
    QMenu* createMenu(QString const& menuId, QWidget* parent);
    QToolBar* createToolbar(QString const& toolbarId, QWidget* parent);
    QStringList toolbarIds() const { return m_toolbarOrder; }

    // ---- what a toolbar shows ------------------------------------------------------------
    // A toolbar is a list of entries: the id of a command, the id of a group of tools
    // sharing one button, or kSeparatorEntry.  The user may change the list; it is kept
    // in the settings.
    static QString const kSeparatorEntry;
    QString toolbarText(QString const& toolbarId) const;
    bool toolbarCustomizable(QString const& toolbarId) const;
    QStringList toolbarEntries(QString const& toolbarId) const;         // as shown now
    QStringList toolbarDefaultEntries(QString const& toolbarId) const;  // as the file has it
    QStringList toolbarAvailable(QString const& toolbarId) const;       // everything it could show
    QString toolbarEntryText(QString const& toolbarId, QString const& entry) const;
    QIcon toolbarEntryIcon(QString const& toolbarId, QString const& entry) const;
    // Shows these entries, on every toolbar made from this definition, and remembers them.
    void setToolbarEntries(QString const& toolbarId, QStringList const& entries);
    // Opens the dialog that edits the list.
    void customizeToolbar(QString const& toolbarId, QWidget* parent);
    // Makes the shortcut of every command work in `window`, whether or not the command is
    // in a menu that is showing.
    void installShortcuts(QWidget* window);

    // ---- commands ------------------------------------------------------------------------
    QAction* action(QString const& id) const { return m_actions.value(id); }
    // For state that changes behind the back of the command (a tool chosen in code).
    void setChecked(QString const& id, bool checked);
    // Runs the command as if it had been picked from a menu.
    void trigger(QString const& id);

    // ---- shortcuts -----------------------------------------------------------------------
    QVector<Shortcut> const& shortcuts() const { return m_shortcuts; }
    QStringList shortcutCategories() const { return m_categories; }
    // Changes a key, applies it to the commands that use it and remembers it for next time.
    void setShortcut(QString const& id, QKeySequence const& key);

signals:
    // A command was run (picked from a menu, a toolbar or by its key): which, and how long it took.
    void commandRun(QString const& id, qint64 milliseconds);

private:
    struct Node {
        enum Type { Menu, Item, Separator, Placeholder, Submenu, MultiButton, Toolbar };
        Type type = Item;
        QHash<QString, QString> attr;
        QVector<Node> children;
        QString value(char const* name) const { return attr.value(QLatin1String(name)); }
    };

    bool readFile(QString const& path, QString const& root, Node& out, QString* error);
    static Node readNode(QXmlStreamReader& xml);
    bool loadShortcuts(QString const& path, QString* error);
    void defineItems(Node const& node);
    QAction* actionFor(Node const& item);
    void fillMenu(QMenu* menu, Node const& node);
    void applyShortcut(QAction* action, QString const& shortcutId);
    Node const* toolbarChild(QString const& toolbarId, QString const& entry) const;
    void populateToolbar(QToolBar* bar);

    // The first of the comma-separated `responders` that has `slot`.
    QObject* resolve(QString const& responders, QByteArray const& slot) const;
    void invoke(Node const& item, bool checked, QMenu* menu);

    QHash<QString, QPointer<QObject>> m_responders;
    QHash<QString, Node> m_menus;      // top-level menus by id
    QHash<QString, Node> m_toolbars;
    QStringList m_toolbarOrder;
    QHash<QString, Node> m_items;      // every command, by id: the element that has the slot
    QHash<QString, QAction*> m_actions;
    QHash<QString, QString> m_actionShortcut;  // command id -> shortcut id
    QHash<QString, QActionGroup*> m_groups;
    QVector<Shortcut> m_shortcuts;
    QString m_directory;
    QList<QPointer<QToolBar>> m_liveToolbars;
    QStringList m_categories;
};
