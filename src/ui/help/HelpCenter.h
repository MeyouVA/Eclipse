// Eclipse - the help: the pages kept in resources/help (in the interface's language when there is a folder of them for it),
// two pages made from the program itself (every command of the menus, every preference), a topic for each place in the
// program (resources/help/topics.xml, then the commands and preferences by their ids), and a search through all of them.
#pragma once

#include "EclipseExport.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class QAction;
class QMenuBar;
class QWidget;

namespace HelpCenter {

// The folder of the pages: resources/help/<language> when it is there and help is not asked for in English
// (Preferences > General), else resources/help.
ECLIPSE_UI_API QString folder();
ECLIPSE_UI_API QString englishFolder();
// Where the pages made from the program are written.
ECLIPSE_UI_API QString generatedFolder();
// The folders a page is looked for in, in order.
ECLIPSE_UI_API QStringList searchPaths();
// Whether a page is there (a name, with or without #part).
ECLIPSE_UI_API bool pageExists(QString const& page);

// commands.html: every command of the menu bar by menu - its name, keys and what it does - each under its own id.
ECLIPSE_UI_API QString writeCommandsPage(QMenuBar* bar);
// preferences.html: every page of the Preferences dialog given (its tabs' widgets), each preference under its own name.
ECLIPSE_UI_API QString writePreferencesPage(QWidget* preferencesDialog);

// The topics of places in the program: id -> page#part (from topics.xml).
ECLIPSE_UI_API QHash<QString, QString> topics();
// The page of an id: topics.xml first, then the commands page, then the preferences page; empty when there is none.
ECLIPSE_UI_API QString topicFor(QString const& id);
// The topic of a place in a window: the action under the pointer in an open menu, else the widget and those round it,
// by their names. Empty when none has one. `found` gets the id it was found by.
ECLIPSE_UI_API QString topicAt(QWidget* widget, QString* found = nullptr);
ECLIPSE_UI_API QString topicOfAction(QAction* action);
// A line or two about a topic: the words of its part of the page.
ECLIPSE_UI_API QString summary(QString const& topic);

// The program's places on the web (resources/links.conf, [links]): website, documentation, releases, issues, tutorials,
// updates (the feed Check for Updates reads). Empty when it has none.
ECLIPSE_UI_API QString link(QString const& name);

struct Hit {
    QString page;       // page#part
    QString title;
    QString text;       // the words round the first match
    int score = 0;
};
// Every page (and part of a page) with all the words, the best first.
ECLIPSE_UI_API QVector<Hit> search(QString const& words);
// The pages the contents lists (index.html's links): page, title.
ECLIPSE_UI_API QVector<QPair<QString, QString>> contents();

}  // namespace HelpCenter
