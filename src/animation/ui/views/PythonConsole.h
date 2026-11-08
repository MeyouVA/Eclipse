// Eclipse - the Python Console: lines of Python run on the scene, by the Python installed on the computer.
//
// Eclipse has no Python of its own: the console starts the computer's Python as a separate process, with a small bridge.
// In it, scene, frame, layers, palette, effects and views stand for what scripts call by those names; each use of them
// goes to Eclipse as a line of JSON, is done there by the script engine, and the answer comes back the same way.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QJSEngine;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class Timeline;

class ECLIPSE_UI_API PythonConsole : public QWidget {
    Q_OBJECT

public:
    PythonConsole(Document* document, Timeline* timeline, QWidget* parent = nullptr);
    ~PythonConsole() override;

    // The Python program that runs the lines (found on the computer); empty when there is none.
    QString python() const;
    // Runs a line (or several) of Python; what it prints comes into the console.  False when Python cannot be started.
    bool run(QString const& code);
    // Whether a line is still running.
    bool busy() const { return m_busy; }
    // All the console shows.
    QString output() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    // A line has finished running.
    void finished();

private:
    bool start();
    void readReplies();
    void answer(QByteArray const& line);
    void say(QString const& text);
    Document* m_document;
    Timeline* m_timeline;
    QProcess* m_process = nullptr;
    QJSEngine* m_engine = nullptr;
    QList<QObject*> m_parts;     // what the names stand for
    QByteArray m_pending;        // what has come from Python and is not yet a whole line
    bool m_busy = false;
    QLineEdit* m_input;
    QPlainTextEdit* m_output;
    QStringList m_history;
    int m_back = 0;
};
