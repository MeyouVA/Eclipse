// Eclipse - Script Editor: scripts written, kept and run on the scene.
//
// A script is JavaScript.  It sees four things:
//   scene   - frameCount (may be set), width, height, frameRate; beginUndo(text) / endUndo()
//   frame   - current (from 1; may be set)
//   layers  - count, current (may be set); name(i), kind(i), indexOf(name), visible(i), setVisible(i, on),
//             add(name), addPeg(name), rename(i, name), remove(i),
//             pose(i, frame) -> {x, y, z, scaleX, scaleY, angle, skew, opacity}, setKeyframe(i, frame, {...}),
//             drawing(i, frame) -> the name of the drawing shown there ("" for none),
//             drawings(i) -> the names of the layer's drawings, expose(i, frame, name) shows that drawing there,
//             newDrawing(i, frame), hold(i, frame, length) (the exposure that frame is in lasts so many frames),
//             clear(i, first, last), renameDrawing(i, frame, name)
//   palette - count; name(k), colour(k) -> "#rrggbb", setColour(k, "#rrggbb"), add(name, "#rrggbb") -> its place
//   effects - count(i); type(i, e) -> "blur", "tint", "cutter", "glow", "shadow" or "particles"; amount(i, e),
//             setAmount(i, e, value); enabled(i, e), setEnabled(i, e, on); colour(i, e), setColour(i, e, "#rrggbb");
//             add(i, type) -> its place; remove(i, e)   (the effects of layer i, from 0)
//   log(text) (print is the same)
// Layers are counted from 0, from the one furthest back; frames from 1.
// Everything one run changes is one step to undo.
#pragma once

#include "EclipseExport.h"

#include <QJSValue>
#include <QKeySequence>
#include <QSyntaxHighlighter>

#include <functional>
#include <QObject>
#include <QWidget>

class Document;
class QJSEngine;
class QListWidget;
class QPlainTextEdit;
class Timeline;

// What a script calls `scene`.
class ECLIPSE_UI_API ScriptScene : public QObject {
    Q_OBJECT
    Q_PROPERTY(int frameCount READ frameCount WRITE setFrameCount)
    Q_PROPERTY(double width READ width)
    Q_PROPERTY(double height READ height)
    Q_PROPERTY(double frameRate READ frameRate)

public:
    ScriptScene(Document* document, QObject* parent) : QObject(parent), m_document(document) {}
    int frameCount() const;
    void setFrameCount(int count);
    double width() const;
    double height() const;
    double frameRate() const;
    Q_INVOKABLE void beginUndo(QString const& text);
    Q_INVOKABLE void endUndo();
    // Closes what a script left open.
    void endAll();

private:
    Document* m_document;
    int m_open = 0;
};

// What a script calls `frame`.
class ECLIPSE_UI_API ScriptFrame : public QObject {
    Q_OBJECT
    Q_PROPERTY(int current READ current WRITE setCurrent)

public:
    ScriptFrame(Timeline* timeline, QObject* parent) : QObject(parent), m_timeline(timeline) {}
    int current() const;
    void setCurrent(int frame);

private:
    Timeline* m_timeline;
};

// What a script calls `layers`.
class ECLIPSE_UI_API ScriptLayers : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count)
    Q_PROPERTY(int current READ current WRITE setCurrent)

public:
    ScriptLayers(Document* document, Timeline* timeline, QJSEngine* engine, QObject* parent)
        : QObject(parent), m_document(document), m_timeline(timeline), m_engine(engine) {}
    int count() const;
    int current() const;
    void setCurrent(int layer);
    Q_INVOKABLE QString name(int layer) const;
    Q_INVOKABLE QString kind(int layer) const;
    Q_INVOKABLE int indexOf(QString const& name) const;
    Q_INVOKABLE bool visible(int layer) const;
    Q_INVOKABLE void setVisible(int layer, bool on);
    Q_INVOKABLE int add(QString const& name);
    Q_INVOKABLE int addPeg(QString const& name);
    Q_INVOKABLE void rename(int layer, QString const& name);
    Q_INVOKABLE void remove(int layer);
    Q_INVOKABLE QJSValue pose(int layer, int frame) const;
    Q_INVOKABLE void setKeyframe(int layer, int frame, QJSValue const& values);
    Q_INVOKABLE QString drawing(int layer, int frame) const;
    Q_INVOKABLE QStringList drawings(int layer) const;
    Q_INVOKABLE bool expose(int layer, int frame, QString const& name);
    Q_INVOKABLE void newDrawing(int layer, int frame);
    Q_INVOKABLE void hold(int layer, int frame, int length);
    Q_INVOKABLE void clear(int layer, int first, int last);
    Q_INVOKABLE bool renameDrawing(int layer, int frame, QString const& name);

private:
    bool has(int layer) const;
    Document* m_document;
    Timeline* m_timeline;
    QJSEngine* m_engine;
};

// What a script calls `palette`: the colours of the scene, all palettes together, in order.
class ECLIPSE_UI_API ScriptPalette : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count)

public:
    ScriptPalette(Document* document, QObject* parent) : QObject(parent), m_document(document) {}
    int count() const;
    Q_INVOKABLE QString name(int colour) const;
    Q_INVOKABLE QString colour(int colour) const;
    Q_INVOKABLE void setColour(int colour, QString const& value);
    Q_INVOKABLE int add(QString const& name, QString const& value);

private:
    Document* m_document;
};

// Colours a script as it is written: words of the language, the names Eclipse gives a script, numbers, strings and comments.
class ECLIPSE_UI_API ScriptHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit ScriptHighlighter(QTextDocument* document);
    // What kind of thing the text at `position` of a line is coloured as: "keyword", "eclipse", "number", "string",
    // "comment" or "" (plain) - for the tests.
    static QString kindAt(QString const& line, int position, bool inComment = false);

protected:
    void highlightBlock(QString const& text) override;
};

// What a script calls `effects`: the effects of a layer.
class ECLIPSE_UI_API ScriptEffects : public QObject {
    Q_OBJECT

public:
    ScriptEffects(Document* document, QObject* parent) : QObject(parent), m_document(document) {}
    Q_INVOKABLE int count(int layer) const;
    Q_INVOKABLE QString type(int layer, int effect) const;
    Q_INVOKABLE double amount(int layer, int effect) const;
    Q_INVOKABLE void setAmount(int layer, int effect, double value);
    Q_INVOKABLE bool enabled(int layer, int effect) const;
    Q_INVOKABLE void setEnabled(int layer, int effect, bool on);
    Q_INVOKABLE QString colour(int layer, int effect) const;
    Q_INVOKABLE void setColour(int layer, int effect, QString const& value);
    Q_INVOKABLE int add(int layer, QString const& type);
    Q_INVOKABLE void remove(int layer, int effect);

private:
    bool has(int layer, int effect) const;
    void change(int layer, std::function<void(QVector<struct Effect>&)> const& how);
    Document* m_document;
};

// Takes what a script logs.
class ECLIPSE_UI_API ScriptOutput : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    Q_INVOKABLE void log(QString const& text) { lines << text; }
    QStringList lines;
};

class ECLIPSE_UI_API ScriptView : public QWidget {
    Q_OBJECT

public:
    ScriptView(Document* document, Timeline* timeline, QWidget* parent = nullptr);

    // Where the scripts are kept (one .js file each).  The user's own folder to begin with.
    void setFolder(QString const& folder);
    QString folder() const { return m_folder; }
    static QString defaultFolder();

    // Runs the script; what it logged, and the error should it have stopped on one (with its line).
    // All it changed is one step to undo, called `name`.
    QString run(QString const& code, QString const& name = QString());

    // The script shown, by name (without .js); false when there is none of that name.
    bool open(QString const& name);
    QString currentName() const { return m_name; }
    QStringList names() const;
    // Opens the script kept under that name and runs it; what run() says, or a message when there is no such script.
    QString runSaved(QString const& name);
    // A script's own shortcut, and whether it has a button on the Scripts toolbar (kept in the settings, by name).
    QKeySequence shortcutOf(QString const& name) const;
    void setShortcutOf(QString const& name, QKeySequence const& key);
    bool onToolbar(QString const& name) const;
    void setOnToolbar(QString const& name, bool on);

signals:
    // Scripts were saved, thrown away, or given another shortcut or toolbar button.
    void scriptsChanged();

public slots:
    void newScript();
    void saveScript();
    void deleteScript();
    void runScript();

private:
    void refreshList();
    QString pathOf(QString const& name) const;
    Document* m_document;
    Timeline* m_timeline;
    QString m_folder;
    QString m_name;
    QListWidget* m_list;
    QPlainTextEdit* m_text;
    QPlainTextEdit* m_output;
    class QKeySequenceEdit* m_shortcut = nullptr;
    class QCheckBox* m_toolbar = nullptr;
    void showCommands();     // the shortcut and toolbar boxes, for the script shown
};
