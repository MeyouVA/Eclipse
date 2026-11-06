// Eclipse - questions asked the same way everywhere: a yes / no that can be told not to be asked again (Never Ask Again:
// the answer kept, Preferences > General asks them all again), and the question for files that are there already, asked
// file by file until Overwrite All or Skip All answers the rest.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QStringList>

class QWidget;

namespace Ask {

// Yes (true) or no; `key` names the question for Never Ask Again (empty: always asked). Where nothing can be shown
// (no screen), the answer kept, else `fallback`.
ECLIPSE_UI_API bool confirm(QWidget* parent, QString const& key, QString const& title, QString const& text, bool fallback = true);
// The answer kept for a question (-1: it is asked), and all of them forgotten (asked again).
ECLIPSE_UI_API int kept(QString const& key);
ECLIPSE_UI_API void keep(QString const& key, bool answer);
ECLIPSE_UI_API QStringList keptQuestions();
ECLIPSE_UI_API void askAgain();

class ECLIPSE_UI_API Overwrite {
public:
    enum Answer { Replace, Skip, Cancel };
    Overwrite(QWidget* parent, QString const& title) : m_parent(parent), m_title(title) {}
    // What to do with a file that is there already: asked (Overwrite, Overwrite All, Skip, Skip All, Cancel) until an
    // "All" answers for the rest. Without a screen, overwritten (as when nothing was asked).
    Answer ask(QString const& file);
    int asked() const { return m_asked; }
    // For checks: the buttons pressed in turn ("overwrite", "overwriteAll", "skip", "skipAll", "cancel").
    void setAnswers(QStringList const& answers) { m_answers = answers; }

private:
    QWidget* m_parent;
    QString m_title;
    int m_all = -1;     // Replace or Skip, once an "All" was pressed
    int m_asked = 0;
    QStringList m_answers;
};

}  // namespace Ask
