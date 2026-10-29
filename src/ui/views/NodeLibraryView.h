// Eclipse - Node Library: the kinds of layer and effect there are, to add from.
#pragma once

#include "EclipseExport.h"

#include <QMap>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLineEdit;
class QListWidget;
class QMenu;

class ECLIPSE_UI_API NodeLibraryView : public QWidget {
    Q_OBJECT

public:
    explicit NodeLibraryView(QWidget* parent = nullptr);
    // What the library offers: its name, what `add` takes for it, what it does, its kind; and whether it is an effect.
    struct Item {
        QString name;
        QString what;
        QString about;
        QString category;
        bool effect = false;
    };
    static QVector<Item> items();
    // Adds what is named: a kind of layer ("InsertPeg" ...) or an effect ("effect:0" ...).
    void add(QString const& what);
    // What a drag out of the list carries: the name of the thing dragged (as `add` takes it).
    static char const* mimeType() { return "application/x-eclipse-node"; }
    // Categories of the user's own: their names, and what is in each (as `add` takes it); kept in the settings.
    QMap<QString, QStringList> userCategories() const { return m_user; }
    bool addCategory(QString const& name);
    bool renameCategory(QString const& from, QString const& to);
    bool removeCategory(QString const& name);
    // What is chosen in the list put in a category of the user's, or taken out of the one shown.
    bool putInCategory(QString const& what, QString const& category);
    bool takeFromCategory(QString const& what, QString const& category);
    QString currentCategory() const;
    QString currentWhat() const;
    void showCategory(QString const& name);
    // The commands of its own menu (Node Library Menu), added to the menu a right-click shows.
    void setSharedMenu(std::function<QMenu*(QWidget*)> make) { m_sharedMenu = std::move(make); }

public slots:
    void onActionNewCategory();
    void onActionRenameCategory();
    void onActionRemoveCategory();
    void onActionRefresh();
    void onActionRemoveNode();

signals:
    // A command of the program is asked for (the layers are added by their Insert commands).
    void actionRequested(QString const& actionId);
    // An effect (Effect::Type) is to be added to the current layer.
    void effectRequested(int type);

protected:
    void contextMenuEvent(class QContextMenuEvent* event) override;

private:
    void filter(QString const& text);
    void loadCategories();
    void saveCategories() const;
    void fillCategories(QString const& current);
    QMap<QString, QStringList> m_user;
    std::function<QMenu*(QWidget*)> m_sharedMenu;
    QLineEdit* m_search;
    QListWidget* m_list;
    QListWidget* m_categories = nullptr;
};
