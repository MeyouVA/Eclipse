// Eclipse - the Script view: the editor, its highlighting, the saved scripts, running them.
#include "ui/views/ScriptView.h"
#include "ui/views/ScriptGlobals.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJSEngine>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QAction>
#include <QMenu>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "canvas/Canvas.h"
#include <QDockWidget>

namespace {
// One piece of a line: where it starts, how long it is, what it is.
struct Piece {
    int start;
    int length;
    char const* kind;
};

QVector<Piece> pieces(QString const& line, bool& comment)
{
    static QStringList const keywords = {
        QStringLiteral("var"), QStringLiteral("let"), QStringLiteral("const"), QStringLiteral("function"), QStringLiteral("return"),
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"), QStringLiteral("do"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("switch"), QStringLiteral("case"), QStringLiteral("default"),
        QStringLiteral("new"), QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null"), QStringLiteral("undefined"),
        QStringLiteral("typeof"), QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("this"), QStringLiteral("try"),
        QStringLiteral("catch"), QStringLiteral("finally"), QStringLiteral("throw")};
    static QStringList const eclipse = {QStringLiteral("scene"), QStringLiteral("frame"), QStringLiteral("layers"),
                                        QStringLiteral("palette"), QStringLiteral("effects"), QStringLiteral("views"), QStringLiteral("log"), QStringLiteral("print")};
    QVector<Piece> out;
    int i = 0;
    int const n = int(line.size());
    while (i < n) {
        if (comment) {
            int const end = int(line.indexOf(QStringLiteral("*/"), i));
            int const stop = end < 0 ? n : end + 2;
            out.push_back({i, stop - i, "comment"});
            comment = end < 0;
            i = stop;
            continue;
        }
        QChar const c = line[i];
        if (c == QLatin1Char('/') && i + 1 < n && line[i + 1] == QLatin1Char('/')) {
            out.push_back({i, n - i, "comment"});
            break;
        }
        if (c == QLatin1Char('/') && i + 1 < n && line[i + 1] == QLatin1Char('*')) {
            comment = true;
            int const end = int(line.indexOf(QStringLiteral("*/"), i + 2));
            int const stop = end < 0 ? n : end + 2;
            out.push_back({i, stop - i, "comment"});
            comment = end < 0;
            i = stop;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            int j = i + 1;
            while (j < n && line[j] != c)
                j += line[j] == QLatin1Char('\\') ? 2 : 1;
            int const stop = std::min(n, j + 1);
            out.push_back({i, stop - i, "string"});
            i = stop;
            continue;
        }
        if (c.isDigit()) {
            int j = i;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == QLatin1Char('.')))
                ++j;
            out.push_back({i, j - i, "number"});
            i = j;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$')) {
            int j = i;
            while (j < n && (line[j].isLetterOrNumber() || line[j] == QLatin1Char('_') || line[j] == QLatin1Char('$')))
                ++j;
            QString const word = line.mid(i, j - i);
            bool const member = i > 0 && line[i - 1] == QLatin1Char('.');
            if (keywords.contains(word))
                out.push_back({i, j - i, "keyword"});
            else if (!member && eclipse.contains(word))
                out.push_back({i, j - i, "eclipse"});
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}
}

ScriptHighlighter::ScriptHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

QString ScriptHighlighter::kindAt(QString const& line, int position, bool inComment)
{
    for (Piece const& p : pieces(line, inComment))
        if (position >= p.start && position < p.start + p.length)
            return QLatin1String(p.kind);
    return QString();
}

void ScriptHighlighter::highlightBlock(QString const& text)
{
    bool comment = previousBlockState() == 1;
    auto format = [](QColor colour, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(colour);
        if (bold)
            f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    };
    static QTextCharFormat const keyword = format(QColor(86, 156, 214), true);
    static QTextCharFormat const eclipse = format(QColor(78, 201, 176));
    static QTextCharFormat const number = format(QColor(181, 206, 168));
    static QTextCharFormat const string = format(QColor(214, 157, 133));
    static QTextCharFormat const remark = format(QColor(106, 153, 85), false, true);
    for (Piece const& p : pieces(text, comment)) {
        QByteArray const kind(p.kind);
        setFormat(p.start, p.length, kind == "keyword" ? keyword : kind == "eclipse" ? eclipse : kind == "number" ? number : kind == "string" ? string : remark);
    }
    setCurrentBlockState(comment ? 1 : 0);
}

QString ScriptView::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/scripts");
}

ScriptView::ScriptView(Document* document, Timeline* timeline, QWidget* parent)
    : QWidget(parent), m_document(document), m_timeline(timeline), m_folder(defaultFolder())
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ScriptList"));
    m_text = new QPlainTextEdit(this);
    m_text->setObjectName(QStringLiteral("ScriptText"));
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    new ScriptHighlighter(m_text->document());
    m_text->setTabStopDistance(m_text->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    m_text->setPlaceholderText(tr("// A script.  For instance:\nfor (var i = 0; i < layers.count; ++i)\n    log(i + \": \" + layers.name(i) + \" (\" + layers.kind(i) + \")\");"));
    m_output = new QPlainTextEdit(this);
    m_output->setObjectName(QStringLiteral("ScriptOutput"));
    m_output->setReadOnly(true);
    m_output->setFont(m_text->font());
    m_output->setMaximumBlockCount(2000);
    auto button = [this](char const* name, QString const& text, QString const& tip) {
        QPushButton* const b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        return b;
    };
    QPushButton* const add = button("ScriptNew", tr("New"), tr("A new script, by a name of your choosing."));
    QPushButton* const save = button("ScriptSave", tr("Save"), tr("Keeps the script as it is written now."));
    QPushButton* const remove = button("ScriptDelete", tr("Delete"), tr("Throws the script away."));
    QPushButton* const run = button("ScriptRun", tr("Run"), tr("Runs the script on the scene. Everything it changes is one step to undo."));
    // The script's own shortcut, and a button for it on the Scripts toolbar.
    m_shortcut = new QKeySequenceEdit(this);
    m_shortcut->setObjectName(QStringLiteral("ScriptShortcut"));
    m_shortcut->setMaximumSequenceLength(1);
    m_shortcut->setClearButtonEnabled(true);
    m_shortcut->setToolTip(tr("A shortcut that runs this script from anywhere in Eclipse."));
    m_shortcut->setMaximumWidth(140);
    m_toolbar = new QCheckBox(tr("On toolbar"), this);
    m_toolbar->setObjectName(QStringLiteral("ScriptOnToolbar"));
    m_toolbar->setToolTip(tr("A button for this script on the Scripts toolbar."));
    connect(m_shortcut, &QKeySequenceEdit::editingFinished, this, [this] {
        if (!m_name.isEmpty())
            setShortcutOf(m_name, m_shortcut->keySequence());
    });
    connect(m_shortcut, &QKeySequenceEdit::keySequenceChanged, this, [this](QKeySequence const& key) {
        if (key.isEmpty() && !m_name.isEmpty() && !shortcutOf(m_name).isEmpty())
            setShortcutOf(m_name, key);     // (cleared with its button)
    });
    connect(m_toolbar, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_name.isEmpty())
            setOnToolbar(m_name, on);
    });
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(add);
    buttons->addWidget(save);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(new QLabel(tr("Shortcut:"), this));
    buttons->addWidget(m_shortcut);
    buttons->addWidget(m_toolbar);
    // The toolbar button's place, icon and tooltip, and the external editor.
    QPushButton* const more = button("ScriptMore", tr("More"), tr("The script's toolbar button, and editing it in another program."));
    QMenu* const menu = new QMenu(more);
    auto item = [&](char const* id, QString const& text, QString const& tip, auto slot) {
        QAction* const a = menu->addAction(text);
        a->setObjectName(QLatin1String(id));
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    item("ScriptMoveFront", tr("Move Script to front of toolbar."), tr("Its button first on the Scripts toolbar."), [this] { moveOnToolbar(m_name, true); });
    item("ScriptMoveBack", tr("Move Script to back of toolbar."), tr("Its button last on the Scripts toolbar."), [this] { moveOnToolbar(m_name, false); });
    item("ScriptIcon", tr("Customize Icon..."), tr("Customize the icon of the selected script, for use in all application toolbars."), [this] {
        if (m_name.isEmpty())
            return;
        QString const file = QFileDialog::getOpenFileName(this, tr("Script Icon"), QString(), tr("Pictures (*.png *.svg *.ico *.jpg *.bmp)"));
        if (!file.isEmpty())
            setIconOf(m_name, file);
    });
    item("ScriptTooltip", tr("Customize Tooltip..."), tr("Customize the tooltip of the selected script, for use in all application toolbars."), [this] {
        if (m_name.isEmpty())
            return;
        bool ok = false;
        QString const text = QInputDialog::getText(this, tr("Script Tooltip"), tr("Tooltip:"), QLineEdit::Normal, tooltipOf(m_name), &ok);
        if (ok)
            setTooltipOf(m_name, text);
    });
    item("ScriptRunFunction", tr("Run Function..."), tr("One function of the script run (the script is run first, so what it sets up is there)."), [this] {
        QStringList const functions = functionsOf(m_name);
        if (functions.isEmpty()) {
            m_output->appendPlainText(tr("The script has no functions."));
            return;
        }
        bool ok = false;
        QString const function = QInputDialog::getItem(this, tr("Run Function"), tr("Functions"), functions, 0, false, &ok);
        if (!ok)
            return;
        QString const said = runFunction(m_name, function);
        m_output->appendPlainText(said.isEmpty() ? tr("%1: done.").arg(function) : said);
    });
    menu->addSeparator();
    item("ScriptSetExternalEditor", tr("Set External Editor..."), tr("The program scripts are edited in outside Eclipse."), [this] {
        QString const program = QFileDialog::getOpenFileName(this, tr("External Editor Application:"), externalEditor());
        if (!program.isEmpty())
            setExternalEditor(program);
    });
    item("ScriptEditExternally", tr("Edit in External Editor"), tr("The script opened in the external editor; what is saved there comes back here."), [this] {
        if (!editExternally())
            m_output->appendPlainText(externalEditor().isEmpty() ? tr("Set an external editor first (More > Set External Editor...).")
                                                                 : tr("The external editor could not be started."));
    });
    more->setMenu(menu);
    buttons->addWidget(more);
    buttons->addWidget(run);
    QSplitter* const upDown = new QSplitter(Qt::Vertical, this);
    upDown->addWidget(m_text);
    upDown->addWidget(m_output);
    upDown->setStretchFactor(0, 3);
    upDown->setStretchFactor(1, 1);
    QSplitter* const across = new QSplitter(Qt::Horizontal, this);
    across->addWidget(m_list);
    across->addWidget(upDown);
    across->setStretchFactor(0, 1);
    across->setStretchFactor(1, 4);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(buttons);
    layout->addWidget(across, 1);
    connect(add, &QPushButton::clicked, this, &ScriptView::newScript);
    connect(save, &QPushButton::clicked, this, &ScriptView::saveScript);
    connect(remove, &QPushButton::clicked, this, &ScriptView::deleteScript);
    connect(run, &QPushButton::clicked, this, &ScriptView::runScript);
    connect(m_list, &QListWidget::currentTextChanged, this, [this](QString const& name) {
        if (!name.isEmpty() && name != m_name)
            open(name);
    });
    refreshList();
}

void ScriptView::setFolder(QString const& folder)
{
    m_folder = folder;
    m_name.clear();
    m_text->clear();
    refreshList();
}

QString ScriptView::pathOf(QString const& name) const
{
    return m_folder + QLatin1Char('/') + name + QStringLiteral(".js");
}

QStringList ScriptView::names() const
{
    QStringList found;
    for (QString const& file : QDir(m_folder).entryList({QStringLiteral("*.js")}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        found << file.left(file.size() - 3);
    return found;
}

void ScriptView::refreshList()
{
    {
        QSignalBlocker const quiet(m_list);
        m_list->clear();
        m_list->addItems(names());
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->text() == m_name)
                m_list->setCurrentRow(i);
    }
    showCommands();
    emit scriptsChanged();
}

void ScriptView::showCommands()
{
    QSignalBlocker const quietKey(m_shortcut), quietBox(m_toolbar);
    bool const any = !m_name.isEmpty();
    m_shortcut->setEnabled(any);
    m_toolbar->setEnabled(any);
    m_shortcut->setKeySequence(any ? shortcutOf(m_name) : QKeySequence());
    m_toolbar->setChecked(any && onToolbar(m_name));
}

QKeySequence ScriptView::shortcutOf(QString const& name) const
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    s.beginGroup(QStringLiteral("scriptShortcuts"));
    return QKeySequence(s.value(name).toString(), QKeySequence::PortableText);
}

void ScriptView::setShortcutOf(QString const& name, QKeySequence const& key)
{
    {
        QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
        s.beginGroup(QStringLiteral("scriptShortcuts"));
        if (key.isEmpty())
            s.remove(name);
        else
            s.setValue(name, key.toString(QKeySequence::PortableText));
    }
    showCommands();
    emit scriptsChanged();
}

bool ScriptView::onToolbar(QString const& name) const
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("scripts/toolbar")).toStringList().contains(name);
}

void ScriptView::setOnToolbar(QString const& name, bool on)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QStringList shown = s.value(QStringLiteral("scripts/toolbar")).toStringList();
    shown.removeAll(name);
    if (on)
        shown << name;
    s.setValue(QStringLiteral("scripts/toolbar"), shown);
    showCommands();
    emit scriptsChanged();
}

QString ScriptView::runSaved(QString const& name)
{
    if (!open(name))
        return tr("There is no script called %1.").arg(name);
    QFile file(pathOf(name));
    if (!file.open(QIODevice::ReadOnly))
        return tr("The script %1 could not be read.").arg(name);
    QString const said = run(QString::fromUtf8(file.readAll()), name);
    m_output->appendPlainText(said.isEmpty() ? tr("%1: done.").arg(name) : said);
    return said;
}

bool ScriptView::open(QString const& name)
{
    QFile file(pathOf(name));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    m_name = name;
    m_loaded = QString::fromUtf8(file.readAll());
    m_text->setPlainText(m_loaded);
    refreshList();
    watch();
    return true;
}

void ScriptView::newScript()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Script"), tr("Name:"), QLineEdit::Normal, tr("Script"), &ok).trimmed();
    name.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    if (!ok || name.isEmpty())
        return;
    if (QFile::exists(pathOf(name))) {
        open(name);
        return;
    }
    m_name = name;
    m_text->clear();
    saveScript();
}

void ScriptView::saveScript()
{
    if (m_name.isEmpty()) {
        newScript();
        return;
    }
    QDir().mkpath(m_folder);
    QFile file(pathOf(m_name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_output->appendPlainText(tr("The script could not be saved to %1.").arg(QDir::toNativeSeparators(pathOf(m_name))));
        return;
    }
    m_loaded = m_text->toPlainText();
    file.write(m_loaded.toUtf8());
    file.close();
    refreshList();
    watch();
}

void ScriptView::deleteScript()
{
    if (m_name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete Script"), tr("Delete the script \"%1\"?").arg(m_name)) != QMessageBox::Yes)
        return;
    QFile::remove(pathOf(m_name));
    m_name.clear();
    m_text->clear();
    refreshList();
}

void ScriptView::runScript()
{
    if (!m_name.isEmpty())
        saveScript();
    QString const said = run(m_text->toPlainText(), m_name);
    m_output->appendPlainText(said.isEmpty() ? tr("Done.") : said);
}

QString ScriptView::run(QString const& code, QString const& name)
{
    QJSEngine engine;
    ScriptOutput output;
    ScriptScene scene(m_document, nullptr);
    ScriptFrame frame(m_timeline, nullptr);
    ScriptLayers layers(m_document, m_timeline, &engine, nullptr);
    ScriptPalette palette(m_document, nullptr);
    ScriptEffects effects(m_document, nullptr);
    ScriptViews views(window(), nullptr);
    for (QObject* o : {static_cast<QObject*>(&output), static_cast<QObject*>(&scene), static_cast<QObject*>(&frame), static_cast<QObject*>(&layers),
                       static_cast<QObject*>(&palette), static_cast<QObject*>(&effects), static_cast<QObject*>(&views)})
        QJSEngine::setObjectOwnership(o, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("__out"), engine.newQObject(&output));
    engine.globalObject().setProperty(QStringLiteral("scene"), engine.newQObject(&scene));
    engine.globalObject().setProperty(QStringLiteral("frame"), engine.newQObject(&frame));
    engine.globalObject().setProperty(QStringLiteral("layers"), engine.newQObject(&layers));
    engine.globalObject().setProperty(QStringLiteral("palette"), engine.newQObject(&palette));
    engine.globalObject().setProperty(QStringLiteral("effects"), engine.newQObject(&effects));
    engine.globalObject().setProperty(QStringLiteral("views"), engine.newQObject(&views));
    engine.evaluate(QStringLiteral("function log(t) { __out.log(String(t)); } var print = log;"));
    QObject parts;
    ScriptContext context;
    context.document = m_document;
    context.timeline = m_timeline;
    context.window = window();
    context.engine = &engine;
    context.output = &output;
    context.layers = &layers;
    context.folder = m_folder;
    installScriptGlobals(context, &parts);

    // A script that never ends is stopped after a while.
    std::atomic<bool> finished{false};
    std::thread watch([&engine, &finished] {
        for (int waited = 0; waited < 200 && !finished; ++waited)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!finished)
            engine.setInterrupted(true);
    });
    m_document->beginMacro(name.isEmpty() ? tr("Script") : tr("Script: %1").arg(name));
    QJSValue const result = engine.evaluate(code, name.isEmpty() ? QStringLiteral("script") : name + QStringLiteral(".js"));
    finished = true;
    watch.join();
    scene.endAll();
    m_document->endMacro();
    // (A script that changed nothing leaves no step behind.)
    if (QUndoStack* const stack = m_document->undoStack(); stack->index() > 0 && stack->command(stack->index() - 1)->childCount() == 0)
        stack->undo();

    QStringList said = output.lines;
    if (engine.isInterrupted())
        said << tr("Stopped: the script ran for too long.");
    else if (result.isError())
        said << tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString());
    return said.join(QLatin1Char('\n'));
}

QStringList ScriptView::toolbarOrder() const
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("scripts/toolbar")).toStringList();
}

void ScriptView::moveOnToolbar(QString const& name, bool toFront)
{
    if (name.isEmpty())
        return;
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QStringList shown = s.value(QStringLiteral("scripts/toolbar")).toStringList();
    if (!shown.removeAll(name))
        return;
    if (toFront)
        shown.prepend(name);
    else
        shown << name;
    s.setValue(QStringLiteral("scripts/toolbar"), shown);
    emit scriptsChanged();
}

QString ScriptView::iconOf(QString const& name) const
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("scriptIcons/") + name).toString();
}

void ScriptView::setIconOf(QString const& name, QString const& file)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    if (file.isEmpty())
        s.remove(QStringLiteral("scriptIcons/") + name);
    else
        s.setValue(QStringLiteral("scriptIcons/") + name, file);
    emit scriptsChanged();
}

QString ScriptView::tooltipOf(QString const& name) const
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("scriptTooltips/") + name).toString();
}

void ScriptView::setTooltipOf(QString const& name, QString const& text)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    if (text.trimmed().isEmpty())
        s.remove(QStringLiteral("scriptTooltips/") + name);
    else
        s.setValue(QStringLiteral("scriptTooltips/") + name, text.trimmed());
    emit scriptsChanged();
}

QString ScriptView::externalEditor() const
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("scripts/externalEditor")).toString();
}

void ScriptView::setExternalEditor(QString const& program)
{
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("scripts/externalEditor"), program);
}

bool ScriptView::editExternally()
{
    if (m_name.isEmpty() || externalEditor().isEmpty())
        return false;
    saveScript();
    return QProcess::startDetached(externalEditor(), {QDir::toNativeSeparators(pathOf(m_name))});
}

QString ScriptView::scriptText() const
{
    return m_text->toPlainText();
}

void ScriptView::setScriptText(QString const& text)
{
    m_text->setPlainText(text);
}

void ScriptView::watch()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { takeExternalChange(); });
    }
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_name.isEmpty() && QFile::exists(pathOf(m_name)))
        m_watcher->addPath(pathOf(m_name));
}

void ScriptView::takeExternalChange()
{
    if (m_name.isEmpty())
        return;
    QFile file(pathOf(m_name));
    if (!file.open(QIODevice::ReadOnly))
        return;
    QString const onDisk = QString::fromUtf8(file.readAll());
    file.close();
    watch();    // (some editors write a new file in place of the old)
    if (onDisk == m_loaded)
        return;
    bool const changedHere = m_text->toPlainText() != m_loaded;
    if (changedHere && ScriptMessageBox::interactive()
        && QMessageBox::question(this, tr("Script modified in external editor"),
                                 tr("The currently selected script was modified in an external editor. Are you sure you want to overwrite it?"))
               == QMessageBox::Yes) {
        // (What is here kept, and written over what the other program saved.)
        saveScript();
        return;
    }
    m_loaded = onDisk;
    m_text->setPlainText(onDisk);
    m_output->appendPlainText(tr("Script modified in external editor: %1 read again.").arg(m_name));
}

QStringList ScriptView::functionsOf(QString const& name) const
{
    QFile file(pathOf(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList found;
    static QRegularExpression const declared(QStringLiteral(R"(^function\s+([A-Za-z_$][\w$]*)\s*\()"), QRegularExpression::MultilineOption);
    for (auto it = declared.globalMatch(QString::fromUtf8(file.readAll())); it.hasNext();)
        found << it.next().captured(1);
    return found;
}

QString ScriptView::runFunction(QString const& name, QString const& function)
{
    QFile file(pathOf(name));
    if (!file.open(QIODevice::ReadOnly))
        return tr("There is no script called %1.").arg(name);
    if (!functionsOf(name).contains(function))
        return tr("The script %1 has no function %2.").arg(name, function);
    return run(QString::fromUtf8(file.readAll()) + QStringLiteral("\n") + function + QStringLiteral("();\n"), name);
}
