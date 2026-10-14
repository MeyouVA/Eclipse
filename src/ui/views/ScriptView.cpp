// Eclipse - Script Editor.
#include "ui/views/ScriptView.h"

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
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

// ---- scene --------------------------------------------------------------------------------

int ScriptScene::frameCount() const
{
    return m_document->scene().frameCount();
}

void ScriptScene::setFrameCount(int count)
{
    if (count >= 1 && count != m_document->scene().frameCount())
        m_document->setFrameCount(count);
}

double ScriptScene::width() const
{
    return m_document->scene().width;
}

double ScriptScene::height() const
{
    return m_document->scene().height;
}

double ScriptScene::frameRate() const
{
    return m_document->scene().fps;
}

void ScriptScene::beginUndo(QString const& text)
{
    m_document->beginMacro(text.isEmpty() ? tr("Script") : text);
    ++m_open;
}

void ScriptScene::endUndo()
{
    if (m_open > 0) {
        m_document->endMacro();
        --m_open;
    }
}

void ScriptScene::endAll()
{
    while (m_open > 0)
        endUndo();
}

// ---- frame --------------------------------------------------------------------------------

int ScriptFrame::current() const
{
    return m_timeline->currentFrame() + 1;
}

void ScriptFrame::setCurrent(int frame)
{
    m_timeline->setCurrentFrame(frame - 1);
}

// ---- layers -------------------------------------------------------------------------------

bool ScriptLayers::has(int layer) const
{
    return layer >= 0 && layer < m_document->scene().layers.size();
}

int ScriptLayers::count() const
{
    return int(m_document->scene().layers.size());
}

int ScriptLayers::current() const
{
    return m_timeline->currentLayer();
}

void ScriptLayers::setCurrent(int layer)
{
    if (has(layer))
        m_timeline->setCurrentCell(layer, m_timeline->currentFrame());
}

QString ScriptLayers::name(int layer) const
{
    return has(layer) ? m_document->scene().layers[layer].name : QString();
}

QString ScriptLayers::kind(int layer) const
{
    static char const* const kKinds[] = {"drawing", "peg", "camera", "sound", "colour-card", "group", "bone", "quadmap"};
    return has(layer) ? QLatin1String(kKinds[m_document->scene().layers[layer].kind]) : QString();
}

int ScriptLayers::indexOf(QString const& name) const
{
    Scene const& scene = m_document->scene();
    for (int l = 0; l < scene.layers.size(); ++l)
        if (scene.layers[l].name == name)
            return l;
    return -1;
}

bool ScriptLayers::visible(int layer) const
{
    return has(layer) && m_document->scene().layers[layer].visible;
}

void ScriptLayers::setVisible(int layer, bool on)
{
    if (has(layer))
        m_document->setLayerVisible(layer, on);
}

int ScriptLayers::add(QString const& name)
{
    int const at = m_document->addLayer(count() - 1);
    if (has(at) && !name.trimmed().isEmpty())
        m_document->renameLayer(at, name.trimmed());
    return at;
}

int ScriptLayers::addPeg(QString const& name)
{
    int const at = m_document->addPeg(count() - 1);
    if (has(at) && !name.trimmed().isEmpty())
        m_document->renameLayer(at, name.trimmed());
    return at;
}

void ScriptLayers::rename(int layer, QString const& name)
{
    if (has(layer) && !name.trimmed().isEmpty())
        m_document->renameLayer(layer, name.trimmed());
}

void ScriptLayers::remove(int layer)
{
    if (has(layer) && count() > 1)
        m_document->removeLayer(layer);
}

QJSValue ScriptLayers::pose(int layer, int frame) const
{
    QJSValue out = m_engine->newObject();
    if (!has(layer))
        return out;
    Pose const p = m_document->scene().layers[layer].poseAt(frame - 1);
    out.setProperty(QStringLiteral("x"), p.x);
    out.setProperty(QStringLiteral("y"), p.y);
    out.setProperty(QStringLiteral("z"), p.z);
    out.setProperty(QStringLiteral("scaleX"), p.scaleX);
    out.setProperty(QStringLiteral("scaleY"), p.scaleY);
    out.setProperty(QStringLiteral("angle"), p.rotation);
    out.setProperty(QStringLiteral("skew"), p.skew);
    out.setProperty(QStringLiteral("opacity"), p.opacity);
    return out;
}

void ScriptLayers::setKeyframe(int layer, int frame, QJSValue const& values)
{
    if (!has(layer) || frame < 1 || frame > m_document->scene().frameCount())
        return;
    Pose p = m_document->scene().layers[layer].poseAt(frame - 1);
    auto take = [&values](char const* name, double& into) {
        QJSValue const v = values.property(QLatin1String(name));
        if (v.isNumber())
            into = v.toNumber();
    };
    take("x", p.x);
    take("y", p.y);
    take("z", p.z);
    take("scaleX", p.scaleX);
    take("scaleY", p.scaleY);
    take("angle", p.rotation);
    take("skew", p.skew);
    take("opacity", p.opacity);
    m_document->setKeyframe(layer, frame - 1, p, tr("Set Keyframe"));
}

QString ScriptLayers::drawing(int layer, int frame) const
{
    if (!has(layer))
        return QString();
    Layer const& l = m_document->scene().layers[layer];
    return l.drawingLabel(l.drawingAt(frame - 1));
}

QStringList ScriptLayers::drawings(int layer) const
{
    QStringList names;
    if (!has(layer))
        return names;
    Layer const& l = m_document->scene().layers[layer];
    for (int i = 0; i < l.drawings.size(); ++i)
        names << l.drawingLabel(i);
    return names;
}

bool ScriptLayers::expose(int layer, int frame, QString const& name)
{
    if (!has(layer) || frame < 1 || frame > m_document->scene().frameCount())
        return false;
    Layer const& l = m_document->scene().layers[layer];
    for (int i = 0; i < l.drawings.size(); ++i) {
        if (l.drawingLabel(i) == name) {
            m_document->substituteDrawing(layer, frame - 1, i);
            return true;
        }
    }
    return false;
}

void ScriptLayers::newDrawing(int layer, int frame)
{
    if (has(layer) && frame >= 1 && frame <= m_document->scene().frameCount())
        m_document->newDrawing(layer, frame - 1);
}

void ScriptLayers::hold(int layer, int frame, int length)
{
    if (has(layer) && frame >= 1 && frame <= m_document->scene().frameCount() && length >= 1)
        m_document->setExposure(layer, frame - 1, length);
}

void ScriptLayers::clear(int layer, int first, int last)
{
    if (has(layer) && first >= 1 && last >= first)
        m_document->clearCells({layer}, first - 1, std::min(last, m_document->scene().frameCount()) - 1, tr("Clear Cells"));
}

bool ScriptLayers::renameDrawing(int layer, int frame, QString const& name)
{
    return has(layer) && frame >= 1 && !name.trimmed().isEmpty() && m_document->renameDrawing(layer, frame - 1, name.trimmed());
}

// ---- palette ------------------------------------------------------------------------------

int ScriptPalette::count() const
{
    return int(m_document->scene().palette.size());
}

QString ScriptPalette::name(int colour) const
{
    return colour >= 0 && colour < count() ? m_document->scene().palette[colour].name : QString();
}

QString ScriptPalette::colour(int colour) const
{
    return colour >= 0 && colour < count() ? m_document->scene().palette[colour].color.name() : QString();
}

void ScriptPalette::setColour(int colour, QString const& value)
{
    QColor const c(value);
    if (colour >= 0 && colour < count() && c.isValid())
        m_document->editSwatch(m_document->scene().palette[colour].id, m_document->scene().palette[colour].name, c);
}

int ScriptPalette::add(QString const& name, QString const& value)
{
    QColor const c(value);
    if (!c.isValid())
        return -1;
    int const id = m_document->addSwatch(name.trimmed().isEmpty() ? tr("Colour") : name.trimmed(), c);
    Scene const& scene = m_document->scene();
    for (int k = 0; k < scene.palette.size(); ++k)
        if (scene.palette[k].id == id)
            return k;
    return -1;
}

// ---- the view -----------------------------------------------------------------------------

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
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(add);
    buttons->addWidget(save);
    buttons->addWidget(remove);
    buttons->addStretch(1);
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
    QSignalBlocker const quiet(m_list);
    m_list->clear();
    m_list->addItems(names());
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text() == m_name)
            m_list->setCurrentRow(i);
}

bool ScriptView::open(QString const& name)
{
    QFile file(pathOf(name));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    m_name = name;
    m_text->setPlainText(QString::fromUtf8(file.readAll()));
    refreshList();
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
    file.write(m_text->toPlainText().toUtf8());
    file.close();
    refreshList();
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
    for (QObject* o : {static_cast<QObject*>(&output), static_cast<QObject*>(&scene), static_cast<QObject*>(&frame), static_cast<QObject*>(&layers), static_cast<QObject*>(&palette)})
        QJSEngine::setObjectOwnership(o, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("__out"), engine.newQObject(&output));
    engine.globalObject().setProperty(QStringLiteral("scene"), engine.newQObject(&scene));
    engine.globalObject().setProperty(QStringLiteral("frame"), engine.newQObject(&frame));
    engine.globalObject().setProperty(QStringLiteral("layers"), engine.newQObject(&layers));
    engine.globalObject().setProperty(QStringLiteral("palette"), engine.newQObject(&palette));
    engine.evaluate(QStringLiteral("function log(t) { __out.log(String(t)); } var print = log;"));

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
