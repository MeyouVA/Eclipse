// Eclipse - the rest of what a script sees, beside scene, frame, layers, palette, effects and views (ScriptView.h):
//
//   node        - the layers by path ("Top/Group/Layer"): root(), subNodes(path), numberOfSubNodes(path), subNode(path, i), type(path),
//                 getName(path), parentNode(path), getEnable / setEnable(path, on), getTextAttr / setTextAttr(path, frame, "position.x", value),
//                 add(parent, name, type) -> its path, deleteNode(path), rename(path, name), link(parent, port, child, port), unlink(child),
//                 srcNode(child, port), getNodes(["READ", "PEG"...]), equals(a, b), isGroup(path)
//   column      - the drawing and sound layers by name: numberOf(), getName(i), type(name) ("DRAWING", "SOUND"), getEntry(name, 1, frame),
//                 setEntry(name, 1, frame, drawing), getDrawingName(name, frame), rename(old, new), add(name, "DRAWING")
//   Drawing     - numberOf(column), name(column, i), isExists(column, name), create(column, name) (a new empty drawing)
//   selection   - selectedNodes(), numberOfNodesSelected(), selectedNode(i), clearSelection(), addNodeToSelection(path), selectAll(),
//                 isSelectionRange(), startFrame(), numberOfFrames()
//   MessageLog  - trace(text), debug(text) (only with setDebug(true)), error(text), isDebug(), setDebug(on), clearLog()
//   MessageBox  - information(text), warning(text), critical(text), question(text) -> true for Yes
//   Action      - perform(id) runs a menu command by its id, validate(id) whether there is one that can be run, ids()
//   preferences - getBool / getInt / getDouble / getString / getColor(name, default), setBool / setInt / setDouble / setString / setColor
//   specialFolders - userScripts, temp, app, config, home, resource
//   about       - getVersionInfoStr(), productName, getApplicationPath(), isWindowsArch(), isMacArch(), isLinuxArch()
//   System      - getenv(name), println(text), processOneEvent()
//   render      - setResolution(w, h), renderFrame(frame, file) -> whether the picture was written, frameImage...
//   File, Dir, Process   - classes (new File(path): open / read / readLines / write / writeLine / close / exists / remove / size;
//                 new Dir(path): exists, mkdir, mkdirs, entryList(filter), rename, rmdir; new Process(program, args): launchAndRead(), exitCode)
//   Dialog, LineEdit, NumberEdit, SpinBox, CheckBox, ComboBox, Label, GroupBox - a dialog put together by the script; exec() shows it
//                 and the controls' values are what was entered.
//   Point2d, Point3d, Vector2d, Vector3d, Matrix4x4, ColorRGBA - the usual helpers
//   include(file) - runs another script (by name or file) in this one.
#pragma once

#include "EclipseExport.h"

#include <QJSValue>
#include <QJSValueIterator>
#include <QObject>
#include <QSize>
#include <QStringList>

class Document;
class QWidget;
class QJSEngine;
class Timeline;
class ScriptLayers;
class ScriptOutput;

// Where the objects get their parts from.
struct ScriptContext {
    Document* document = nullptr;
    Timeline* timeline = nullptr;
    QWidget* window = nullptr;
    QJSEngine* engine = nullptr;
    ScriptOutput* output = nullptr;
    ScriptLayers* layers = nullptr;
    QString folder;         // where include() looks first
};

class ECLIPSE_UI_API ScriptNode : public QObject {
    Q_OBJECT

public:
    ScriptNode(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE QString root() const { return QStringLiteral("Top"); }
    Q_INVOKABLE QStringList subNodes(QString const& path) const;
    Q_INVOKABLE int numberOfSubNodes(QString const& path) const { return int(subNodes(path).size()); }
    Q_INVOKABLE QString subNode(QString const& path, int index) const;
    Q_INVOKABLE QString type(QString const& path) const;
    Q_INVOKABLE QString getName(QString const& path) const;
    Q_INVOKABLE QString parentNode(QString const& path) const;
    Q_INVOKABLE bool isGroup(QString const& path) const;
    Q_INVOKABLE bool getEnable(QString const& path) const;
    Q_INVOKABLE void setEnable(QString const& path, bool on);
    Q_INVOKABLE QString getTextAttr(QString const& path, int frame, QString const& attribute) const;
    Q_INVOKABLE bool setTextAttr(QString const& path, QString const& attribute, int frame, QString const& value);
    Q_INVOKABLE QString add(QString const& parent, QString const& name, QString const& type);
    Q_INVOKABLE bool deleteNode(QString const& path);
    Q_INVOKABLE bool rename(QString const& path, QString const& name);
    Q_INVOKABLE bool link(QString const& parent, int parentPort, QString const& child, int childPort);
    Q_INVOKABLE bool unlink(QString const& child);
    Q_INVOKABLE QString srcNode(QString const& child, int port) const;
    Q_INVOKABLE QStringList getNodes(QStringList const& types) const;
    Q_INVOKABLE bool equals(QString const& a, QString const& b) const { return layerOf(a) >= 0 && layerOf(a) == layerOf(b); }
    // The path of a layer (from 0, furthest back first), and the layer of a path (-1: none).
    Q_INVOKABLE QString pathOf(int layer) const;
    Q_INVOKABLE int layerOf(QString const& path) const;

private:
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptColumn : public QObject {
    Q_OBJECT

public:
    ScriptColumn(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE int numberOf() const { return int(columns().size()); }
    Q_INVOKABLE QString getName(int index) const;
    Q_INVOKABLE QString type(QString const& name) const;
    Q_INVOKABLE QString getEntry(QString const& name, int subColumn, int frame) const;
    Q_INVOKABLE bool setEntry(QString const& name, int subColumn, int frame, QString const& value);
    Q_INVOKABLE QString getDrawingName(QString const& name, int frame) const { return getEntry(name, 1, frame); }
    Q_INVOKABLE bool rename(QString const& oldName, QString const& newName);
    Q_INVOKABLE bool add(QString const& name, QString const& type);

private:
    QVector<int> columns() const;
    int layerNamed(QString const& name) const;
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptDrawings : public QObject {
    Q_OBJECT

public:
    ScriptDrawings(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE int numberOf(QString const& column) const;
    Q_INVOKABLE QString name(QString const& column, int index) const;
    Q_INVOKABLE bool isExists(QString const& column, QString const& name) const;
    Q_INVOKABLE bool create(QString const& column, QString const& name);

private:
    int layerNamed(QString const& name) const;
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptSelection : public QObject {
    Q_OBJECT

public:
    ScriptSelection(ScriptContext const& c, ScriptNode* node, QObject* parent) : QObject(parent), m_c(c), m_node(node) {}
    Q_INVOKABLE QStringList selectedNodes() const;
    Q_INVOKABLE int numberOfNodesSelected() const { return int(selectedNodes().size()); }
    Q_INVOKABLE QString selectedNode(int index) const;
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool addNodeToSelection(QString const& path);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool isSelectionRange() const;
    Q_INVOKABLE int startFrame() const;
    Q_INVOKABLE int numberOfFrames() const;

private:
    ScriptContext m_c;
    ScriptNode* m_node;
};

class ECLIPSE_UI_API ScriptMessageLog : public QObject {
    Q_OBJECT

public:
    ScriptMessageLog(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE void trace(QString const& text);
    Q_INVOKABLE void debug(QString const& text);
    Q_INVOKABLE void error(QString const& text);
    Q_INVOKABLE bool isDebug() const { return m_debug; }
    Q_INVOKABLE void setDebug(bool on) { m_debug = on; }
    Q_INVOKABLE void clearLog();

private:
    ScriptContext m_c;
    bool m_debug = false;
};

class ECLIPSE_UI_API ScriptMessageBox : public QObject {
    Q_OBJECT

public:
    ScriptMessageBox(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE void information(QString const& text) { show(0, text); }
    Q_INVOKABLE void warning(QString const& text) { show(1, text); }
    Q_INVOKABLE void critical(QString const& text) { show(2, text); }
    Q_INVOKABLE bool question(QString const& text) { return show(3, text); }
    // Whether dialogs are shown at all (not where there is no screen, as in tests: what they say goes to the output).
    static bool interactive();

private:
    bool show(int kind, QString const& text);
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptAction : public QObject {
    Q_OBJECT

public:
    ScriptAction(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE bool perform(QString const& id);
    Q_INVOKABLE bool validate(QString const& id) const;
    Q_INVOKABLE QStringList ids() const;

private:
    class QAction* find(QString const& id) const;
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptPreferences : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    Q_INVOKABLE bool getBool(QString const& name, bool fallback) const;
    Q_INVOKABLE int getInt(QString const& name, int fallback) const;
    Q_INVOKABLE double getDouble(QString const& name, double fallback) const;
    Q_INVOKABLE QString getString(QString const& name, QString const& fallback) const;
    Q_INVOKABLE QString getColor(QString const& name, QString const& fallback) const;
    Q_INVOKABLE void setBool(QString const& name, bool value);
    Q_INVOKABLE void setInt(QString const& name, int value);
    Q_INVOKABLE void setDouble(QString const& name, double value);
    Q_INVOKABLE void setString(QString const& name, QString const& value);
    Q_INVOKABLE void setColor(QString const& name, QString const& value);
};

class ECLIPSE_UI_API ScriptFolders : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userScripts READ userScripts)
    Q_PROPERTY(QString temp READ temp)
    Q_PROPERTY(QString app READ app)
    Q_PROPERTY(QString config READ config)
    Q_PROPERTY(QString home READ home)
    Q_PROPERTY(QString resource READ resource)

public:
    ScriptFolders(QString scripts, QObject* parent) : QObject(parent), m_scripts(std::move(scripts)) {}
    QString userScripts() const { return m_scripts; }
    QString temp() const;
    QString app() const;
    QString config() const;
    QString home() const;
    QString resource() const;

private:
    QString m_scripts;
};

// AnimationIO: keyframes from and to ATOM files.
//   AnimationIO.importAnimation(path [, {fields: true, scale: 1}]), AnimationIO.exportAnimation(path [, {fields, scale, overwrite,
//   selectedOnly}]) - true when done; what went wrong goes to the output.
class ECLIPSE_UI_API ScriptAnimationIO : public QObject {
    Q_OBJECT

public:
    ScriptAnimationIO(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE bool importAnimation(QJSValue const& path = QJSValue(), QJSValue const& options = QJSValue(), QJSValue const& extra = QJSValue());
    Q_INVOKABLE bool exportAnimation(QJSValue const& path = QJSValue(), QJSValue const& options = QJSValue(), QJSValue const& extra = QJSValue());

private:
    bool run(bool exporting, QJSValue const& path, QJSValue const& options, QJSValue const& extra);
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptAbout : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString productName READ productName)

public:
    using QObject::QObject;
    QString productName() const { return QStringLiteral("Eclipse"); }
    Q_INVOKABLE QString getVersionInfoStr() const;
    Q_INVOKABLE QString getApplicationPath() const;
    Q_INVOKABLE bool isWindowsArch() const;
    Q_INVOKABLE bool isMacArch() const;
    Q_INVOKABLE bool isLinuxArch() const;
};

// System, and what File, Dir and Process (made in the script) do underneath.
class ECLIPSE_UI_API ScriptSystem : public QObject {
    Q_OBJECT

public:
    ScriptSystem(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE QString getenv(QString const& name) const;
    Q_INVOKABLE void println(QString const& text);
    Q_INVOKABLE void processOneEvent();
    Q_INVOKABLE QString readText(QString const& path) const;
    Q_INVOKABLE bool writeText(QString const& path, QString const& text, bool append);
    Q_INVOKABLE bool exists(QString const& path) const;
    Q_INVOKABLE bool isDir(QString const& path) const;
    Q_INVOKABLE bool removeFile(QString const& path);
    Q_INVOKABLE double fileSize(QString const& path) const;
    Q_INVOKABLE bool makeDir(QString const& path, bool parents);
    Q_INVOKABLE bool removeDir(QString const& path);
    Q_INVOKABLE bool renamePath(QString const& from, QString const& to);
    Q_INVOKABLE QStringList entries(QString const& path, QString const& filter) const;
    // Runs a program and waits (up to `timeout` ms); the exit code and what it printed.
    Q_INVOKABLE QJSValue run(QString const& program, QStringList const& arguments, int timeout);
    // include(): the file of a script (its name in the scripts folder, or a path), or "" when there is none.
    Q_INVOKABLE QString scriptFile(QString const& name) const;

private:
    ScriptContext m_c;
};

class ECLIPSE_UI_API ScriptRender : public QObject {
    Q_OBJECT

public:
    ScriptRender(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    Q_INVOKABLE void setResolution(int width, int height) { m_size = QSize(width, height); }
    Q_INVOKABLE int resolutionX() const;
    Q_INVOKABLE int resolutionY() const;
    Q_INVOKABLE bool renderFrame(int frame, QString const& file);
    Q_INVOKABLE bool renderScene(int first, int last, QString const& folder, QString const& name);

private:
    ScriptContext m_c;
    QSize m_size;
};

// What Dialog.exec() shows: the controls a script put in it, described as an object.
class ECLIPSE_UI_API ScriptUi : public QObject {
    Q_OBJECT

public:
    ScriptUi(ScriptContext const& c, QObject* parent) : QObject(parent), m_c(c) {}
    // `dialog`: {title, okButtonText, cancelButtonText, controls: [{kind, label, value, minimum, maximum, decimals, items, group}]};
    // the values entered, in order, or null when it was cancelled.  Without a screen, the values as they are.
    Q_INVOKABLE QJSValue exec(QJSValue const& dialog);
    // For tests: what the next exec() answers (the values; or none: cancelled).
    static void answerNext(QVariantList const& values, bool accepted);

private:
    ScriptContext m_c;
};

// Puts every object above into `engine` (and the classes made in JavaScript); the objects are owned by `owner`.
ECLIPSE_UI_API void installScriptGlobals(ScriptContext const& context, QObject* owner);
// The names it adds (for the Python Console).
ECLIPSE_UI_API QStringList scriptGlobalNames();
// Help > Scripting Interface Documentation: what each object has.
ECLIPSE_UI_API QString scriptingReference();
