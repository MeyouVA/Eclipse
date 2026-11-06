// Eclipse - the objects scripts see besides the first six (see ScriptGlobals.h).
#include "ui/views/ScriptGlobals.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QJSEngine>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "actions/theme/AppSettings.h"
#include "canvas/FrameRender.h"
#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/ScriptView.h"

namespace {

Scene const& sceneOf(ScriptContext const& c)
{
    return c.document->scene();
}

QString kindName(int kind)
{
    static char const* const kNames[] = {"READ", "PEG", "CAMERA", "SOUND", "COLOR_CARD", "GROUP", "BONE", "QUADMAP", "NOTE"};
    return QLatin1String(kNames[std::clamp(kind, 0, 8)]);
}

QVariantList gAnswer;
bool gAnswerAccepted = true;
bool gAnswerSet = false;

}  // namespace

// ---- node ----

QString ScriptNode::pathOf(int l) const
{
    Scene const& scene = sceneOf(m_c);
    if (l < 0 || l >= scene.layers.size())
        return QString();
    QStringList names{scene.layers[l].name};
    for (int up = scene.parentIndex(l), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
        if (scene.layers[up].kind == Layer::Group)
            names.prepend(scene.layers[up].name);
    return QStringLiteral("Top/") + names.join(QLatin1Char('/'));
}

int ScriptNode::layerOf(QString const& path) const
{
    Scene const& scene = sceneOf(m_c);
    QString wanted = path.trimmed();
    if (!wanted.startsWith(QStringLiteral("Top/")))
        wanted = QStringLiteral("Top/") + wanted;
    for (int l = 0; l < scene.layers.size(); ++l)
        if (pathOf(l) == wanted)
            return l;
    // (A name on its own, when it is the only one so named.)
    int found = -1;
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].name == path.trimmed()) {
            if (found >= 0)
                return -1;
            found = l;
        }
    return found;
}

QStringList ScriptNode::subNodes(QString const& path) const
{
    Scene const& scene = sceneOf(m_c);
    bool const top = path.trimmed() == QLatin1String("Top") || path.trimmed().isEmpty();
    int const group = top ? -1 : layerOf(path);
    QStringList out;
    if (!top && (group < 0 || scene.layers[group].kind != Layer::Group))
        return out;
    // The layers in it: those whose nearest group is it (front first, as the Timeline lists them).
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        int nearest = -1;
        for (int up = scene.parentIndex(l), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
            if (scene.layers[up].kind == Layer::Group) {
                nearest = up;
                break;
            }
        if (nearest == group)
            out << pathOf(l);
    }
    return out;
}

QString ScriptNode::subNode(QString const& path, int index) const
{
    QStringList const all = subNodes(path);
    return index >= 0 && index < all.size() ? all[index] : QString();
}

QString ScriptNode::type(QString const& path) const
{
    int const l = layerOf(path);
    return l < 0 ? QString() : kindName(sceneOf(m_c).layers[l].kind);
}

QString ScriptNode::getName(QString const& path) const
{
    int const l = layerOf(path);
    return l < 0 ? QString() : sceneOf(m_c).layers[l].name;
}

QString ScriptNode::parentNode(QString const& path) const
{
    int const l = layerOf(path);
    if (l < 0)
        return QString();
    QString const full = pathOf(l);
    return full.left(full.lastIndexOf(QLatin1Char('/')));
}

bool ScriptNode::isGroup(QString const& path) const
{
    return path.trimmed() == QLatin1String("Top") || type(path) == QLatin1String("GROUP");
}

bool ScriptNode::getEnable(QString const& path) const
{
    int const l = layerOf(path);
    return l >= 0 && sceneOf(m_c).layers[l].visible;
}

void ScriptNode::setEnable(QString const& path, bool on)
{
    if (int const l = layerOf(path); l >= 0)
        m_c.document->setLayerVisible(l, on);
}

QString ScriptNode::getTextAttr(QString const& path, int frame, QString const& attribute) const
{
    int const l = layerOf(path);
    if (l < 0 || !m_c.layers)
        return QString();
    QString const key = attribute.trimmed().toLower();
    if (key == QLatin1String("name"))
        return sceneOf(m_c).layers[l].name;
    if (key == QLatin1String("enabled") || key == QLatin1String("visible"))
        return sceneOf(m_c).layers[l].visible ? QStringLiteral("Y") : QStringLiteral("N");
    double const v = m_c.layers->attribute(pathOf(l).mid(4) + QLatin1Char(':') + attribute, frame);
    return std::isnan(v) ? QString() : QString::number(v, 'g', 12);
}

bool ScriptNode::setTextAttr(QString const& path, QString const& attribute, int frame, QString const& value)
{
    int const l = layerOf(path);
    if (l < 0 || !m_c.layers)
        return false;
    QString const key = attribute.trimmed().toLower();
    if (key == QLatin1String("enabled") || key == QLatin1String("visible")) {
        m_c.document->setLayerVisible(l, value.startsWith(QLatin1Char('Y'), Qt::CaseInsensitive) || value == QLatin1String("1")
                                             || value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        return true;
    }
    bool ok = false;
    double const v = value.toDouble(&ok);
    return ok && m_c.layers->setAttribute(pathOf(l).mid(4) + QLatin1Char(':') + attribute, frame, v);
}

QString ScriptNode::add(QString const& parent, QString const& name, QString const& type)
{
    QString const kind = type.trimmed().toUpper();
    Scene const& scene = sceneOf(m_c);
    int const above = int(scene.layers.size()) - 1;
    int made = -1;
    if (kind == QLatin1String("PEG"))
        made = m_c.document->addPeg(above);
    else if (kind == QLatin1String("GROUP"))
        made = m_c.document->addGroup(above);
    else if (kind == QLatin1String("READ") || kind == QLatin1String("DRAWING") || kind == QLatin1String("IMAGE") || kind.isEmpty())
        made = m_c.document->addLayer(above);
    else {
        if (m_c.output)
            m_c.output->log(tr("Port creation requires a type -- 'image' or 'peg' is accepted."));
        return QString();
    }
    if (made < 0)
        return QString();
    if (!name.trimmed().isEmpty())
        m_c.document->renameLayer(made, name.trimmed());
    if (int const group = layerOf(parent); group >= 0 && parent.trimmed() != QLatin1String("Top"))
        m_c.document->setParent(made, group);
    return pathOf(made);
}

bool ScriptNode::deleteNode(QString const& path)
{
    int const l = layerOf(path);
    if (l < 0 || !m_c.layers)
        return false;
    m_c.layers->remove(l);
    return true;
}

bool ScriptNode::rename(QString const& path, QString const& name)
{
    int const l = layerOf(path);
    if (l < 0 || name.trimmed().isEmpty())
        return false;
    m_c.document->renameLayer(l, name.trimmed());
    return true;
}

bool ScriptNode::link(QString const& parent, int, QString const& child, int)
{
    int const p = layerOf(parent), c = layerOf(child);
    return p >= 0 && c >= 0 && p != c && m_c.document->setParent(c, p);
}

bool ScriptNode::unlink(QString const& child)
{
    int const c = layerOf(child);
    return c >= 0 && m_c.document->setParent(c, -1);
}

QString ScriptNode::srcNode(QString const& child, int) const
{
    int const c = layerOf(child);
    return c < 0 ? QString() : pathOf(sceneOf(m_c).parentIndex(c));
}

QStringList ScriptNode::getNodes(QStringList const& types) const
{
    QStringList out;
    Scene const& scene = sceneOf(m_c);
    for (int l = 0; l < scene.layers.size(); ++l)
        if (types.isEmpty() || types.contains(kindName(scene.layers[l].kind), Qt::CaseInsensitive))
            out << pathOf(l);
    return out;
}

// ---- column ----

QVector<int> ScriptColumn::columns() const
{
    QVector<int> out;
    Scene const& scene = sceneOf(m_c);
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].kind == Layer::DrawingLayer || scene.layers[l].kind == Layer::Sound)
            out << l;
    return out;
}

int ScriptColumn::layerNamed(QString const& name) const
{
    Scene const& scene = sceneOf(m_c);
    for (int l : columns())
        if (scene.layers[l].name == name)
            return l;
    return -1;
}

QString ScriptColumn::getName(int index) const
{
    QVector<int> const all = columns();
    return index >= 0 && index < all.size() ? sceneOf(m_c).layers[all[index]].name : QString();
}

QString ScriptColumn::type(QString const& name) const
{
    int const l = layerNamed(name);
    return l < 0 ? QString() : sceneOf(m_c).layers[l].kind == Layer::Sound ? QStringLiteral("SOUND") : QStringLiteral("DRAWING");
}

QString ScriptColumn::getEntry(QString const& name, int, int frame) const
{
    int const l = layerNamed(name);
    return l < 0 || !m_c.layers ? QString() : m_c.layers->drawing(l, frame);
}

bool ScriptColumn::setEntry(QString const& name, int, int frame, QString const& value)
{
    int const l = layerNamed(name);
    if (l < 0 || !m_c.layers)
        return false;
    if (value.isEmpty()) {
        m_c.layers->clear(l, frame, frame);
        return true;
    }
    // (A drawing that is not there yet is made, with that name.)
    if (m_c.layers->expose(l, frame, value))
        return true;
    m_c.layers->newDrawing(l, frame);
    if (m_c.layers->renameDrawing(l, frame, value))
        return true;
    if (m_c.output)
        m_c.output->log(tr("Failed to set the value in the XSheet."));
    return false;
}

bool ScriptColumn::rename(QString const& oldName, QString const& newName)
{
    int const l = layerNamed(oldName);
    if (l < 0 || newName.trimmed().isEmpty())
        return false;
    m_c.document->renameLayer(l, newName.trimmed());
    return true;
}

bool ScriptColumn::add(QString const& name, QString const& type)
{
    if (!type.isEmpty() && type.compare(QStringLiteral("DRAWING"), Qt::CaseInsensitive) != 0)
        return false;
    int const made = m_c.document->addLayer(int(sceneOf(m_c).layers.size()) - 1);
    if (made < 0)
        return false;
    if (!name.trimmed().isEmpty())
        m_c.document->renameLayer(made, name.trimmed());
    return true;
}

// ---- Drawing ----

int ScriptDrawings::layerNamed(QString const& name) const
{
    Scene const& scene = sceneOf(m_c);
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].name == name && scene.layers[l].holdsDrawings())
            return l;
    return -1;
}

int ScriptDrawings::numberOf(QString const& column) const
{
    int const l = layerNamed(column);
    return l < 0 ? 0 : int(sceneOf(m_c).layers[l].drawings.size());
}

QString ScriptDrawings::name(QString const& column, int index) const
{
    int const l = layerNamed(column);
    if (l < 0 || index < 0 || index >= sceneOf(m_c).layers[l].drawings.size())
        return QString();
    return sceneOf(m_c).layers[l].drawingLabel(index);
}

bool ScriptDrawings::isExists(QString const& column, QString const& drawing) const
{
    for (int i = 0; i < numberOf(column); ++i)
        if (name(column, i) == drawing)
            return true;
    return false;
}

bool ScriptDrawings::create(QString const& column, QString const& drawing)
{
    int const l = layerNamed(column);
    if (l < 0 || drawing.trimmed().isEmpty() || isExists(column, drawing))
        return false;
    m_c.document->editScene(QObject::tr("Create Drawing"), [&](Scene& s) {
        Drawing d;
        d.name = drawing.trimmed();
        s.layers[l].drawings.push_back(d);
    });
    return true;
}

// ---- selection ----

QStringList ScriptSelection::selectedNodes() const
{
    QStringList out;
    if (!m_c.timeline)
        return out;
    for (int l : m_c.timeline->selectedLayers())
        out << m_node->pathOf(l);
    return out;
}

QString ScriptSelection::selectedNode(int index) const
{
    QStringList const all = selectedNodes();
    return index >= 0 && index < all.size() ? all[index] : QString();
}

void ScriptSelection::clearSelection()
{
    if (m_c.timeline)
        m_c.timeline->setCurrentCell(m_c.timeline->currentLayer(), m_c.timeline->currentFrame());
}

bool ScriptSelection::addNodeToSelection(QString const& path)
{
    int const l = m_node->layerOf(path);
    if (l < 0 || !m_c.timeline)
        return false;
    if (!m_c.timeline->selectedLayers().contains(l))
        m_c.timeline->toggleLayer(l);
    return true;
}

void ScriptSelection::selectAll()
{
    if (m_c.timeline)
        m_c.timeline->selectAll();
}

bool ScriptSelection::isSelectionRange() const
{
    return m_c.timeline && m_c.timeline->hasRange();
}

int ScriptSelection::startFrame() const
{
    return m_c.timeline ? m_c.timeline->selectionFirstFrame() + 1 : 1;
}

int ScriptSelection::numberOfFrames() const
{
    return m_c.timeline ? m_c.timeline->selectionLastFrame() - m_c.timeline->selectionFirstFrame() + 1 : 0;
}

// ---- MessageLog, MessageBox ----

namespace {
// (Into the window's Message Log too, when there is one.)
void toMessageLog(ScriptContext const& c, QString const& text)
{
    if (!c.window)
        return;
    if (QPlainTextEdit* const log = c.window->findChild<QPlainTextEdit*>(QStringLiteral("MessageLog")))
        log->appendPlainText(text);
}
}

void ScriptMessageLog::trace(QString const& text)
{
    if (m_c.output)
        m_c.output->log(text);
    toMessageLog(m_c, text);
}

void ScriptMessageLog::debug(QString const& text)
{
    if (!m_debug)
        return;
    if (m_c.output)
        m_c.output->log(text);
    toMessageLog(m_c, text);
}

void ScriptMessageLog::error(QString const& text)
{
    QString const said = tr("Error: %1").arg(text);
    if (m_c.output)
        m_c.output->log(said);
    toMessageLog(m_c, said);
}

void ScriptMessageLog::clearLog()
{
    if (m_c.output)
        m_c.output->lines.clear();
}

bool ScriptMessageBox::interactive()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen") && QGuiApplication::platformName() != QLatin1String("minimal");
}

bool ScriptMessageBox::show(int kind, QString const& text)
{
    if (!interactive()) {
        static char const* const kWhat[] = {QT_TR_NOOP("Information"), QT_TR_NOOP("Warning"), QT_TR_NOOP("Critical"), QT_TR_NOOP("Question")};
        if (m_c.output)
            m_c.output->log(QStringLiteral("%1: %2").arg(tr(kWhat[kind]), text));
        return kind == 3;
    }
    QString const title = QStringLiteral("Eclipse");
    switch (kind) {
    case 1: QMessageBox::warning(m_c.window, title, text); return true;
    case 2: QMessageBox::critical(m_c.window, title, text); return true;
    case 3: return QMessageBox::question(m_c.window, title, text) == QMessageBox::Yes;
    default: QMessageBox::information(m_c.window, title, text); return true;
    }
}

// ---- Action ----

QAction* ScriptAction::find(QString const& id) const
{
    if (!m_c.window)
        return nullptr;
    for (QAction* a : m_c.window->findChildren<QAction*>(id))
        return a;
    return nullptr;
}

bool ScriptAction::perform(QString const& id)
{
    QAction* const a = find(id);
    if (!a || !a->isEnabled())
        return false;
    a->trigger();
    return true;
}

bool ScriptAction::validate(QString const& id) const
{
    QAction const* const a = find(id);
    return a && a->isEnabled();
}

QStringList ScriptAction::ids() const
{
    QStringList out;
    if (m_c.window)
        for (QAction const* a : m_c.window->findChildren<QAction*>())
            if (!a->objectName().isEmpty() && !a->objectName().contains(QLatin1Char(':')) && !out.contains(a->objectName()))
                out << a->objectName();
    out.sort();
    return out;
}

// ---- preferences ----

namespace {
QString prefKey(QString const& name)
{
    return name.contains(QLatin1Char('/')) ? name : QStringLiteral("prefs/") + name;
}
}

bool ScriptPreferences::getBool(QString const& name, bool fallback) const { return settings().value(prefKey(name), fallback).toBool(); }
int ScriptPreferences::getInt(QString const& name, int fallback) const { return settings().value(prefKey(name), fallback).toInt(); }
double ScriptPreferences::getDouble(QString const& name, double fallback) const { return settings().value(prefKey(name), fallback).toDouble(); }
QString ScriptPreferences::getString(QString const& name, QString const& fallback) const { return settings().value(prefKey(name), fallback).toString(); }
QString ScriptPreferences::getColor(QString const& name, QString const& fallback) const
{
    QVariant const v = settings().value(prefKey(name));
    QColor const c = v.canConvert<QColor>() ? v.value<QColor>() : QColor(v.toString());
    return c.isValid() ? c.name(QColor::HexArgb) : fallback;
}
void ScriptPreferences::setBool(QString const& name, bool value) { settings().setValue(prefKey(name), value); }
void ScriptPreferences::setInt(QString const& name, int value) { settings().setValue(prefKey(name), value); }
void ScriptPreferences::setDouble(QString const& name, double value) { settings().setValue(prefKey(name), value); }
void ScriptPreferences::setString(QString const& name, QString const& value) { settings().setValue(prefKey(name), value); }
void ScriptPreferences::setColor(QString const& name, QString const& value) { settings().setValue(prefKey(name), QColor(value)); }

// ---- specialFolders, about ----

QString ScriptFolders::temp() const { return QStandardPaths::writableLocation(QStandardPaths::TempLocation); }
QString ScriptFolders::app() const { return QCoreApplication::applicationDirPath(); }
QString ScriptFolders::config() const { return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation); }
QString ScriptFolders::home() const { return QDir::homePath(); }
QString ScriptFolders::resource() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/resources"); }

QString ScriptAbout::getVersionInfoStr() const
{
    QString const version = QCoreApplication::applicationVersion();
    return QStringLiteral("Eclipse %1").arg(version.isEmpty() ? QStringLiteral("0.2.0") : version);
}

QString ScriptAbout::getApplicationPath() const { return QCoreApplication::applicationFilePath(); }

bool ScriptAbout::isWindowsArch() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool ScriptAbout::isMacArch() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

bool ScriptAbout::isLinuxArch() const
{
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

// ---- System, files, processes ----

QString ScriptSystem::getenv(QString const& name) const { return QProcessEnvironment::systemEnvironment().value(name); }

void ScriptSystem::println(QString const& text)
{
    if (m_c.output)
        m_c.output->log(text);
}

void ScriptSystem::processOneEvent() { QCoreApplication::processEvents(); }

QString ScriptSystem::readText(QString const& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool ScriptSystem::writeText(QString const& path, QString const& text, bool append)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::Truncate)))
        return false;
    return f.write(text.toUtf8()) >= 0;
}

bool ScriptSystem::exists(QString const& path) const { return QFileInfo::exists(path); }
bool ScriptSystem::isDir(QString const& path) const { return QFileInfo(path).isDir(); }
bool ScriptSystem::removeFile(QString const& path) { return QFile::remove(path); }
double ScriptSystem::fileSize(QString const& path) const { return double(QFileInfo(path).size()); }
bool ScriptSystem::makeDir(QString const& path, bool parents) { return parents ? QDir().mkpath(path) : QDir().mkdir(path); }
bool ScriptSystem::removeDir(QString const& path) { return QDir().rmdir(path); }
bool ScriptSystem::renamePath(QString const& from, QString const& to) { return QDir().rename(from, to); }

QStringList ScriptSystem::entries(QString const& path, QString const& filter) const
{
    QDir const dir(path);
    return dir.entryList(filter.isEmpty() ? QStringList() : filter.split(QLatin1Char(';'), Qt::SkipEmptyParts), QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
}

QJSValue ScriptSystem::run(QString const& program, QStringList const& arguments, int timeout)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    QJSValue out = m_c.engine->newObject();
    if (!process.waitForStarted(5000)) {
        out.setProperty(QStringLiteral("exitCode"), -1);
        out.setProperty(QStringLiteral("output"), QString());
        out.setProperty(QStringLiteral("error"), process.errorString());
        return out;
    }
    if (!process.waitForFinished(timeout > 0 ? timeout : 30000))
        process.kill();
    out.setProperty(QStringLiteral("exitCode"), process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1);
    out.setProperty(QStringLiteral("output"), QString::fromLocal8Bit(process.readAll()));
    out.setProperty(QStringLiteral("error"), QString());
    return out;
}

QString ScriptSystem::scriptFile(QString const& name) const
{
    QStringList const tries = {name, name + QStringLiteral(".js")};
    for (QString const& t : tries) {
        if (QFileInfo(t).isAbsolute() && QFileInfo(t).isFile())
            return t;
        if (!m_c.folder.isEmpty() && QFileInfo(QDir(m_c.folder).filePath(t)).isFile())
            return QDir(m_c.folder).filePath(t);
    }
    return QString();
}

// ---- render ----

int ScriptRender::resolutionX() const { return m_size.isValid() ? m_size.width() : sceneOf(m_c).width; }
int ScriptRender::resolutionY() const { return m_size.isValid() ? m_size.height() : sceneOf(m_c).height; }

bool ScriptRender::renderFrame(int frame, QString const& file)
{
    Scene const& scene = sceneOf(m_c);
    if (frame < 1 || frame > scene.frameCount())
        return false;
    QImage const image = FrameRender::frame(scene, frame - 1, QSize(resolutionX(), resolutionY()));
    QDir().mkpath(QFileInfo(file).absolutePath());
    return image.save(file);
}

bool ScriptRender::renderScene(int first, int last, QString const& folder, QString const& name)
{
    bool all = first >= 1 && last >= first;
    for (int f = first; all && f <= last; ++f)
        all = renderFrame(f, QDir(folder).filePath(QStringLiteral("%1-%2.png").arg(name).arg(f, 4, 10, QLatin1Char('0'))));
    return all;
}

// ---- Dialog ----

void ScriptUi::answerNext(QVariantList const& values, bool accepted)
{
    gAnswer = values;
    gAnswerAccepted = accepted;
    gAnswerSet = true;
}

QJSValue ScriptUi::exec(QJSValue const& dialog)
{
    QJSValue const controls = dialog.property(QStringLiteral("controls"));
    int const count = controls.property(QStringLiteral("length")).toInt();
    QJSValue values = m_c.engine->newArray(uint(count));
    auto start = [&](int i) { return controls.property(quint32(i)).property(QStringLiteral("value")); };
    if (gAnswerSet) {
        gAnswerSet = false;
        if (!gAnswerAccepted)
            return QJSValue(QJSValue::NullValue);
        for (int i = 0; i < count; ++i)
            values.setProperty(quint32(i), i < gAnswer.size() ? m_c.engine->toScriptValue(gAnswer[i]) : start(i));
        return values;
    }
    if (!ScriptMessageBox::interactive()) {
        for (int i = 0; i < count; ++i)
            values.setProperty(quint32(i), start(i));
        return values;
    }
    QDialog box(m_c.window);
    box.setObjectName(QStringLiteral("ScriptDialog"));
    box.setWindowTitle(dialog.property(QStringLiteral("title")).toString());
    QVBoxLayout* const layout = new QVBoxLayout(&box);
    QFormLayout* form = new QFormLayout;
    layout->addLayout(form);
    QVector<std::function<QJSValue()>> readers;
    for (int i = 0; i < count; ++i) {
        QJSValue const c = controls.property(quint32(i));
        QString const kind = c.property(QStringLiteral("kind")).toString();
        QString const label = c.property(QStringLiteral("label")).toString();
        QJSValue const value = c.property(QStringLiteral("value"));
        if (kind == QLatin1String("GroupBox")) {
            QGroupBox* const group = new QGroupBox(label, &box);
            layout->addWidget(group);
            form = new QFormLayout(group);
            readers << [] { return QJSValue(); };
        } else if (kind == QLatin1String("Label")) {
            form->addRow(new QLabel(label, &box));
            readers << [] { return QJSValue(); };
        } else if (kind == QLatin1String("CheckBox")) {
            QCheckBox* const check = new QCheckBox(label, &box);
            check->setChecked(value.toBool());
            form->addRow(check);
            readers << [check] { return QJSValue(check->isChecked()); };
        } else if (kind == QLatin1String("NumberEdit") || kind == QLatin1String("SpinBox")) {
            double const low = c.property(QStringLiteral("minimum")).isNumber() ? c.property(QStringLiteral("minimum")).toNumber() : -1e9;
            double const high = c.property(QStringLiteral("maximum")).isNumber() ? c.property(QStringLiteral("maximum")).toNumber() : 1e9;
            if (kind == QLatin1String("SpinBox")) {
                QSpinBox* const spin = new QSpinBox(&box);
                spin->setRange(int(std::max(low, -2e9)), int(std::min(high, 2e9)));
                spin->setValue(value.toInt());
                form->addRow(label, spin);
                readers << [spin] { return QJSValue(spin->value()); };
            } else {
                QDoubleSpinBox* const number = new QDoubleSpinBox(&box);
                number->setDecimals(c.property(QStringLiteral("decimals")).isNumber() ? c.property(QStringLiteral("decimals")).toInt() : 2);
                number->setRange(low, high);
                number->setValue(value.toNumber());
                form->addRow(label, number);
                readers << [number] { return QJSValue(number->value()); };
            }
        } else if (kind == QLatin1String("ComboBox")) {
            QComboBox* const combo = new QComboBox(&box);
            QJSValue const items = c.property(QStringLiteral("items"));
            for (int k = 0; k < items.property(QStringLiteral("length")).toInt(); ++k)
                combo->addItem(items.property(quint32(k)).toString());
            combo->setCurrentText(value.toString());
            combo->setEditable(c.property(QStringLiteral("editable")).toBool());
            form->addRow(label, combo);
            readers << [combo] { return QJSValue(combo->currentText()); };
        } else {
            QLineEdit* const line = new QLineEdit(value.toString(), &box);
            form->addRow(label, line);
            readers << [line] { return QJSValue(line->text()); };
        }
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &box);
    if (QString const ok = dialog.property(QStringLiteral("okButtonText")).toString(); !ok.isEmpty() && ok != QLatin1String("undefined"))
        buttons->button(QDialogButtonBox::Ok)->setText(ok);
    if (QString const cancel = dialog.property(QStringLiteral("cancelButtonText")).toString(); !cancel.isEmpty() && cancel != QLatin1String("undefined"))
        buttons->button(QDialogButtonBox::Cancel)->setText(cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    layout->addWidget(buttons);
    if (box.exec() != QDialog::Accepted)
        return QJSValue(QJSValue::NullValue);
    for (int i = 0; i < count; ++i)
        values.setProperty(quint32(i), readers[i]());
    return values;
}

// ---- putting it together ----

QStringList scriptGlobalNames()
{
    return {QStringLiteral("node"), QStringLiteral("column"), QStringLiteral("Drawing"), QStringLiteral("selection"), QStringLiteral("MessageLog"),
            QStringLiteral("MessageBox"), QStringLiteral("Action"), QStringLiteral("preferences"), QStringLiteral("specialFolders"), QStringLiteral("about"),
            QStringLiteral("System"), QStringLiteral("render"), QStringLiteral("AnimationIO")};
}

extern char const* const kScriptPrelude[];

void installScriptGlobals(ScriptContext const& c, QObject* owner)
{
    QJSEngine& engine = *c.engine;
    ScriptNode* const node = new ScriptNode(c, owner);
    QVector<QPair<QString, QObject*>> const parts = {
        {QStringLiteral("node"), node},
        {QStringLiteral("column"), new ScriptColumn(c, owner)},
        {QStringLiteral("Drawing"), new ScriptDrawings(c, owner)},
        {QStringLiteral("selection"), new ScriptSelection(c, node, owner)},
        {QStringLiteral("MessageLog"), new ScriptMessageLog(c, owner)},
        {QStringLiteral("MessageBox"), new ScriptMessageBox(c, owner)},
        {QStringLiteral("Action"), new ScriptAction(c, owner)},
        {QStringLiteral("preferences"), new ScriptPreferences(owner)},
        {QStringLiteral("specialFolders"), new ScriptFolders(c.folder, owner)},
        {QStringLiteral("about"), new ScriptAbout(owner)},
        {QStringLiteral("System"), new ScriptSystem(c, owner)},
        {QStringLiteral("render"), new ScriptRender(c, owner)},
        {QStringLiteral("AnimationIO"), new ScriptAnimationIO(c, owner)},
        {QStringLiteral("__ui"), new ScriptUi(c, owner)}};
    for (auto const& [name, object] : parts) {
        QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(name, engine.newQObject(object));
    }
    QString prelude;
    for (int i = 0; kScriptPrelude[i]; ++i)
        prelude += QString::fromUtf8(kScriptPrelude[i]);
    engine.evaluate(prelude, QStringLiteral("prelude.js"));
}

QString scriptingReference()
{
    return QObject::tr(R"(Eclipse scripting (JavaScript; the Python Console reaches the same objects)

Layers are counted from 0 (the one furthest back first); frames from 1. All one run changes is one step to undo.

scene          frameCount (may be set), width, height, frameRate; beginUndo(text), endUndo()
frame          current (may be set)
layers         count, current; name, kind, indexOf, visible, setVisible, add, addPeg, rename, remove, pose, setKeyframe,
               drawing, drawings, expose, newDrawing, hold, clear, renameDrawing, addDriver, driverCount, driver, clearDrivers,
               attribute(path, frame), setAttribute(path, frame, value), attributes(layer)
palette        count; name, colour, setColour, add
effects        count, type, amount, setAmount, enabled, setEnabled, colour, setColour, add, remove
views          list, isOpen, open, close; drawingView, zoom
node           root, subNodes, numberOfSubNodes, subNode, type, getName, parentNode, isGroup, getEnable, setEnable,
               getTextAttr(path, frame, attr), setTextAttr(path, attr, frame, value), add(parent, name, type), deleteNode, rename,
               link(parent, port, child, port), unlink, srcNode, getNodes(types), equals, pathOf(layer), layerOf(path)
column         numberOf, getName, type, getEntry(name, 1, frame), setEntry(name, 1, frame, drawing), getDrawingName, rename, add
Drawing        numberOf(column), name(column, i), isExists(column, name), create(column, name)
selection      selectedNodes, numberOfNodesSelected, selectedNode, clearSelection, addNodeToSelection, selectAll,
               isSelectionRange, startFrame, numberOfFrames
AnimationIO    importAnimation(path, {fields, scale}), exportAnimation(path, {fields, scale, overwrite, selectedOnly}) - ATOM files
MessageLog     trace, debug, error, isDebug, setDebug, clearLog
MessageBox     information, warning, critical, question
Action         perform(id), validate(id), ids()
preferences    getBool, getInt, getDouble, getString, getColor, setBool, setInt, setDouble, setString, setColor
specialFolders userScripts, temp, app, config, home, resource
about          productName, getVersionInfoStr, getApplicationPath, isWindowsArch, isMacArch, isLinuxArch
System         getenv, println, processOneEvent
render         setResolution, resolutionX, resolutionY, renderFrame(frame, file), renderScene(first, last, folder, name)
File           new File(path): open(FileAccess.ReadOnly | WriteOnly | Append), read, readLine, readLines, write, writeLine, close,
               exists, remove, size
Dir            new Dir(path): exists, mkdir, mkdirs, rmdir, rename, entryList(filter), fileExists
Process        new Process(program, args): launch, launchAndRead, readStdout, exitCode
Dialog         new Dialog(): title, okButtonText, cancelButtonText, add(control), newTab(name), exec()
               controls: LineEdit (text), NumberEdit (value, decimals, minimum, maximum), SpinBox, CheckBox (checked, text),
               ComboBox (items, currentItem, editable), Label (text), GroupBox (title, add); each has a label
Input          getText(label, text), getNumber(label, value, decimals, minimum, maximum), getItem(label, items, current)
Point2d, Point3d, Vector2d, Vector3d, Matrix4x4, ColorRGBA, include(script), log(text) / print(text)
)");
}
