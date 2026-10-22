// Eclipse - the Python Console.
#include "ui/views/PythonConsole.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/ScriptView.h"

namespace {

char const* const kBridge = R"PY(
import sys, json, io, traceback
_real_out, _in = sys.stdout, sys.stdin
def _send(message):
    _real_out.write(json.dumps(message) + "\n")
    _real_out.flush()
def _ask(message):
    _send(message)
    reply = json.loads(_in.readline())
    if "error" in reply:
        raise RuntimeError(reply["error"])
    return reply
class _Part:
    def __init__(self, path):
        object.__setattr__(self, "_path", path)
    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        path = self._path + "." + name
        reply = _ask({"get": path})
        if reply.get("callable") or reply.get("object"):
            return _Part(path)
        return reply.get("result")
    def __setattr__(self, name, value):
        _ask({"set": self._path + "." + name, "value": value})
    def __call__(self, *args):
        return _ask({"call": self._path, "args": list(args)}).get("result")
    def __repr__(self):
        return "<eclipse " + self._path + ">"
for _name in ("scene", "frame", "layers", "palette", "effects", "views"):
    globals()[_name] = _Part(_name)
class _Said(io.TextIOBase):
    def write(self, text):
        if text:
            _send({"out": text})
        return len(text)
sys.stdout = sys.stderr = _Said()
_space = {"scene": scene, "frame": frame, "layers": layers, "palette": palette, "effects": effects, "views": views, "__name__": "__console__"}
while True:
    line = _in.readline()
    if not line:
        break
    code = json.loads(line).get("run", "")
    try:
        try:
            shown = eval(compile(code, "<console>", "eval"), _space)
            if shown is not None:
                print(repr(shown))
        except SyntaxError:
            exec(compile(code, "<console>", "exec"), _space)
    except Exception:
        traceback.print_exc()
    _send({"done": True})
)PY";

}  // namespace

PythonConsole::PythonConsole(Document* document, Timeline* timeline, QWidget* parent) : QWidget(parent), m_document(document), m_timeline(timeline)
{
    m_output = new QPlainTextEdit(this);
    m_output->setObjectName(QStringLiteral("PythonOutput"));
    m_output->setReadOnly(true);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_output->setMaximumBlockCount(5000);
    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("PythonInput"));
    m_input->setFont(m_output->font());
    m_input->setPlaceholderText(tr("Python: e.g.  layers.count   or   layers.add(\"Sky\")   (Enter runs it; Up and Down go through what was run)"));
    m_input->installEventFilter(this);
    QPushButton* const clear = new QPushButton(tr("Clear"), this);
    clear->setObjectName(QStringLiteral("PythonClear"));
    connect(clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral(">>>"), this));
    row->addWidget(m_input, 1);
    row->addWidget(clear);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_output, 1);
    layout->addLayout(row);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        QString const code = m_input->text();
        if (code.trimmed().isEmpty() || m_busy)
            return;
        m_history.push_back(code);
        m_back = int(m_history.size());
        m_input->clear();
        say(QStringLiteral(">>> ") + code + QLatin1Char('\n'));
        run(code);
    });
    say(tr("Python Console: scene, frame, layers, palette, effects and views are as in the Script Editor.\n"));
}

PythonConsole::~PythonConsole()
{
    if (m_process) {
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(1000))
            m_process->kill();
    }
}

QString PythonConsole::python() const
{
    // The Python launcher first (it finds the newest Python), then the program by its usual names.
    for (char const* name : {"py", "python3", "python"}) {
        QString const found = QStandardPaths::findExecutable(QLatin1String(name));
        // (Windows' own stand-in that only offers to install Python from the Store is no Python.)
        if (!found.isEmpty() && !found.contains(QLatin1String("WindowsApps"), Qt::CaseInsensitive))
            return found;
    }
    return QString();
}

bool PythonConsole::start()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return true;
    QString const program = python();
    if (program.isEmpty()) {
        say(tr("No Python was found on this computer. Install Python (python.org) to use the console.\n"));
        return false;
    }
    if (!m_engine) {
        // What the names stand for: as the Script Editor has them, kept for as long as the console is open.
        m_engine = new QJSEngine(this);
        ScriptOutput* const out = new ScriptOutput(this);
        QObject* const parts[] = {new ScriptScene(m_document, this), new ScriptFrame(m_timeline, this), new ScriptLayers(m_document, m_timeline, m_engine, this),
                                  new ScriptPalette(m_document, this), new ScriptEffects(m_document, this), new ScriptViews(window(), this)};
        char const* const names[] = {"scene", "frame", "layers", "palette", "effects", "views"};
        for (int i = 0; i < 6; ++i) {
            QJSEngine::setObjectOwnership(parts[i], QJSEngine::CppOwnership);
            m_engine->globalObject().setProperty(QLatin1String(names[i]), m_engine->newQObject(parts[i]));
            m_parts << parts[i];
        }
        QJSEngine::setObjectOwnership(out, QJSEngine::CppOwnership);
        m_engine->globalObject().setProperty(QStringLiteral("__out"), m_engine->newQObject(out));
    }
    delete m_process;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PythonConsole::readReplies);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] { say(QString::fromUtf8(m_process->readAllStandardError())); });
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_busy) {
            m_busy = false;
            m_document->endMacro();
            emit finished();
        }
    });
    QStringList arguments;
    if (QFileInfo(program).baseName().compare(QLatin1String("py"), Qt::CaseInsensitive) == 0)
        arguments << QStringLiteral("-3");
    arguments << QStringLiteral("-u") << QStringLiteral("-c") << QString::fromUtf8(kBridge);
    m_process->start(program, arguments);
    if (!m_process->waitForStarted(5000)) {
        say(tr("Python could not be started (%1).\n").arg(program));
        return false;
    }
    return true;
}

bool PythonConsole::run(QString const& code)
{
    if (m_busy || !start())
        return false;
    m_busy = true;
    // All a line changes is one step to undo.
    m_document->beginMacro(tr("Python"));
    m_process->write(QJsonDocument(QJsonObject{{"run", code}}).toJson(QJsonDocument::Compact) + "\n");
    return true;
}

void PythonConsole::readReplies()
{
    m_pending += m_process->readAllStandardOutput();
    for (qsizetype end = m_pending.indexOf('\n'); end >= 0; end = m_pending.indexOf('\n')) {
        QByteArray const line = m_pending.left(end).trimmed();
        m_pending.remove(0, end + 1);
        if (!line.isEmpty())
            answer(line);
    }
}

void PythonConsole::answer(QByteArray const& line)
{
    QJsonObject const message = QJsonDocument::fromJson(line).object();
    if (message.isEmpty()) {
        say(QString::fromUtf8(line) + QLatin1Char('\n'));     // something printed past the bridge
        return;
    }
    if (message.contains(QLatin1String("out"))) {
        say(message.value(QLatin1String("out")).toString());
        return;
    }
    if (message.contains(QLatin1String("done"))) {
        m_busy = false;
        static_cast<ScriptScene*>(m_parts[0])->endAll();
        m_document->endMacro();
        // (A line that changed nothing leaves no step behind.)
        if (QUndoStack* const stack = m_document->undoStack(); stack->index() > 0 && stack->command(stack->index() - 1)->childCount() == 0
            && stack->command(stack->index() - 1)->text() == tr("Python"))
            stack->undo();
        emit finished();
        return;
    }
    // A question from Python: a name's value, a call, or a value to set.
    QJsonObject reply;
    QString const path = message.value(QLatin1String(message.contains(QLatin1String("call")) ? "call" : message.contains(QLatin1String("set")) ? "set" : "get")).toString();
    // (Only names of the parts: a path is words and dots.)
    static QRegularExpression const word(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*$"));
    if (!word.match(path).hasMatch()) {
        reply[QLatin1String("error")] = tr("Not a name of Eclipse: %1").arg(path);
    } else {
        qsizetype const dot = path.lastIndexOf(QLatin1Char('.'));
        QJSValue owner = dot > 0 ? m_engine->evaluate(path.left(dot)) : m_engine->globalObject();
        QString const last = dot > 0 ? path.mid(dot + 1) : path;
        QJSValue result;
        if (message.contains(QLatin1String("call"))) {
            QJSValueList arguments;
            for (QJsonValue const& a : message.value(QLatin1String("args")).toArray())
                arguments << m_engine->toScriptValue(a.toVariant());
            result = owner.property(last).callWithInstance(owner, arguments);
        } else if (message.contains(QLatin1String("set"))) {
            owner.setProperty(last, m_engine->toScriptValue(message.value(QLatin1String("value")).toVariant()));
            result = QJSValue(QJSValue::UndefinedValue);
        } else {
            result = owner.property(last);
            if (result.isCallable())
                reply[QLatin1String("callable")] = true;
            else if (result.isQObject())
                reply[QLatin1String("object")] = true;
        }
        if (result.isError())
            reply[QLatin1String("error")] = result.toString();
        else if (owner.isUndefined() || (!message.contains(QLatin1String("set")) && !message.contains(QLatin1String("call")) && result.isUndefined()))
            reply[QLatin1String("error")] = tr("Eclipse has nothing called %1").arg(path);
        else if (!reply.contains(QLatin1String("callable")) && !reply.contains(QLatin1String("object")))
            reply[QLatin1String("result")] = QJsonValue::fromVariant(result.toVariant());
    }
    m_process->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
}

void PythonConsole::say(QString const& text)
{
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->moveCursor(QTextCursor::End);
}

bool PythonConsole::eventFilter(QObject* watched, QEvent* event)
{
    // Up and Down in the line: what was run before, and back.
    if (watched == m_input && event->type() == QEvent::KeyPress && !m_history.isEmpty()) {
        int const key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            m_back = std::clamp(m_back + (key == Qt::Key_Up ? -1 : 1), 0, int(m_history.size()));
            m_input->setText(m_back < m_history.size() ? m_history[m_back] : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString PythonConsole::output() const
{
    return m_output->toPlainText();
}
