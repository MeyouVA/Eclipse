// Eclipse - the help's window: the contents, a search through every page, the page read (back, forward, contents), and
// the topic asked for ("Help topic requested: ..."; "Help file not found: ..." when its page is not there).
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;

class ECLIPSE_UI_API HelpView : public QWidget {
    Q_OBJECT

public:
    explicit HelpView(QWidget* parent = nullptr);

    QTextBrowser* pages() const { return m_pages; }
    // A topic shown (page#part); false, with the page saying so, when the page is not there.
    bool showTopic(QString const& topic);
    void showContents();
    // The search's results listed (how many); one of them shown.
    int searchFor(QString const& words);
    void showResult(int row);
    QString currentTopic() const { return m_topic; }
    QString message() const;

private:
    void fillContents();
    QTextBrowser* m_pages;
    QListWidget* m_contents;
    QLineEdit* m_search;
    QListWidget* m_results;
    QLabel* m_status;
    QStringList m_resultPages;
    QString m_topic;
};
