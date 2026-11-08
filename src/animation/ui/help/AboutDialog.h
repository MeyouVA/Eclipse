// Eclipse - what the program says about itself: the About box (its version, when it was built, the Qt it runs on, its
// licence, the programs it uses, its places on the web), a window for its documents (Read Me, License, Credits, Latest
// News), and Report an Issue (a report of the version and the computer with what happened, copied, saved, or taken to
// the issue page).
#pragma once

#include "EclipseExport.h"

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QTextBrowser;

namespace AboutText {

// Where the documents are: resources/about (README.md, LICENSE, credits.md, the third-party notices) beside the program.
ECLIPSE_UI_API QString folder();
// A document's text ("readme", "license", "credits", "notices"); empty when it is not there.
ECLIPSE_UI_API QString document(QString const& name);
// The programs Eclipse uses: name, what for, their licence (from credits.md).
ECLIPSE_UI_API QStringList credits();
// The version and the computer, as a report has them.
ECLIPSE_UI_API QString systemReport();

}  // namespace AboutText

// A link to a place on the web, opened in the browser (shown only when links.conf has it).
ECLIPSE_UI_API QLabel* hyperlinkLabel(QString const& text, QString const& linkName, QWidget* parent);

class ECLIPSE_UI_API AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

// A document shown: Markdown or plain text.
class ECLIPSE_UI_API DocumentDialog : public QDialog {
    Q_OBJECT

public:
    DocumentDialog(QString const& title, QString const& text, bool markdown, QWidget* parent = nullptr);
    QTextBrowser* browser() const { return m_browser; }

private:
    QTextBrowser* m_browser;
};

class ECLIPSE_UI_API ReportIssueDialog : public QDialog {
    Q_OBJECT

public:
    ReportIssueDialog(QStringList const& recentMessages, QWidget* parent = nullptr);
    // The whole report: what happened, the steps, then the version, the computer and the last messages.
    QString report() const;
    void setWhatHappened(QString const& text);
    bool saveTo(QString const& path) const;

public slots:
    void copyReport();
    void openIssuePage();

private:
    QPlainTextEdit* m_what;
    QPlainTextEdit* m_steps;
    QStringList m_messages;
};
