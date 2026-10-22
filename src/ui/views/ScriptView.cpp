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
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/canvas/Canvas.h"
#include <QDockWidget>

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
    // (As the scene has it: values that follow other layers' too.)
    Pose const p = m_document->scene().poseOf(layer, frame - 1);
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

static char const* const kDriverValues[Driver::ValueCount] = {"x", "y", "scaleX", "scaleY", "angle", "opacity", "z", "skew", "fov"};

static int driverValue(QString const& name)
{
    for (int v = 0; v < Driver::ValueCount; ++v)
        if (name == QLatin1String(kDriverValues[v]))
            return v;
    return -1;
}

bool ScriptLayers::addDriver(int layer, QString const& value, int source, QString const& sourceValue, double factor, double offset)
{
    int const v = driverValue(value), sv = driverValue(sourceValue);
    if (!has(layer) || !has(source) || source == layer || v < 0 || sv < 0)
        return false;
    Driver d;
    d.value = v;
    d.source = m_document->scene().layers[source].id;
    d.sourceValue = sv;
    d.factor = factor;
    d.offset = offset;
    m_document->editLayers({layer}, tr("Attribute Controls"), [d](Layer& l) {
        // One driver to a value: a new one takes the place of the one there was.
        l.drivers.removeIf([&d](Driver const& old) { return old.value == d.value; });
        l.drivers.push_back(d);
    });
    return true;
}

int ScriptLayers::driverCount(int layer) const
{
    return has(layer) ? int(m_document->scene().layers[layer].drivers.size()) : 0;
}

QJSValue ScriptLayers::driver(int layer, int index) const
{
    QJSValue out = m_engine->newObject();
    if (!has(layer) || index < 0 || index >= m_document->scene().layers[layer].drivers.size())
        return out;
    Driver const& d = m_document->scene().layers[layer].drivers[index];
    out.setProperty(QStringLiteral("value"), QString::fromLatin1(kDriverValues[std::clamp(d.value, 0, int(Driver::ValueCount) - 1)]));
    out.setProperty(QStringLiteral("source"), m_document->scene().layerIndex(d.source));
    out.setProperty(QStringLiteral("sourceValue"), QString::fromLatin1(kDriverValues[std::clamp(d.sourceValue, 0, int(Driver::ValueCount) - 1)]));
    out.setProperty(QStringLiteral("factor"), d.factor);
    out.setProperty(QStringLiteral("offset"), d.offset);
    return out;
}

void ScriptLayers::clearDrivers(int layer)
{
    if (has(layer) && !m_document->scene().layers[layer].drivers.isEmpty())
        m_document->editLayers({layer}, tr("Attribute Controls"), [](Layer& l) { l.drivers.clear(); });
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

// ---- effects ------------------------------------------------------------------------------

static char const* const kEffectNames[] = {"blur", "tint", "cutter", "glow", "shadow", "particles", "transparency", "directionalBlur", "hueSaturation", "brightnessContrast",
                                           "grain", "radialBlur", "colourScale", "negate", "median", "ripple", "tone", "highlight", "bloom",
                                           "pixelate", "turbulence", "matteGenerator", "blackWhite", "matteResize", "matteBlur", "gradient"};

bool ScriptEffects::has(int layer, int effect) const
{
    Scene const& scene = m_document->scene();
    return layer >= 0 && layer < scene.layers.size() && effect >= 0 && effect < scene.layers[layer].effects.size();
}

void ScriptEffects::change(int layer, std::function<void(QVector<Effect>&)> const& how)
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return;
    QVector<Effect> effects = scene.layers[layer].effects;
    how(effects);
    if (!(effects == scene.layers[layer].effects))
        m_document->setEffects(layer, effects, tr("Effects"));
}

int ScriptEffects::count(int layer) const
{
    Scene const& scene = m_document->scene();
    return layer >= 0 && layer < scene.layers.size() ? int(scene.layers[layer].effects.size()) : 0;
}

QString ScriptEffects::type(int layer, int effect) const
{
    return has(layer, effect) ? QLatin1String(kEffectNames[m_document->scene().layers[layer].effects[effect].type]) : QString();
}

double ScriptEffects::amount(int layer, int effect) const
{
    return has(layer, effect) ? m_document->scene().layers[layer].effects[effect].amount : 0.0;
}

void ScriptEffects::setAmount(int layer, int effect, double value)
{
    if (has(layer, effect))
        change(layer, [effect, value](QVector<Effect>& e) { e[effect].amount = std::max(0.0, value); });
}

bool ScriptEffects::enabled(int layer, int effect) const
{
    return has(layer, effect) && m_document->scene().layers[layer].effects[effect].enabled;
}

void ScriptEffects::setEnabled(int layer, int effect, bool on)
{
    if (has(layer, effect))
        change(layer, [effect, on](QVector<Effect>& e) { e[effect].enabled = on; });
}

QString ScriptEffects::colour(int layer, int effect) const
{
    return has(layer, effect) ? m_document->scene().layers[layer].effects[effect].color.name() : QString();
}

void ScriptEffects::setColour(int layer, int effect, QString const& value)
{
    QColor const c(value);
    if (has(layer, effect) && c.isValid())
        change(layer, [effect, c](QVector<Effect>& e) { e[effect].color = c; });
}

int ScriptEffects::add(int layer, QString const& type)
{
    int kind = -1;
    for (int k = 0; k < int(std::size(kEffectNames)); ++k)
        if (type.compare(QLatin1String(kEffectNames[k]), Qt::CaseInsensitive) == 0)
            kind = k;
    if (kind < 0 || layer < 0 || layer >= m_document->scene().layers.size())
        return -1;
    change(layer, [kind](QVector<Effect>& e) {
        Effect made = Effect::made(Effect::Type(kind));
        if (kind < Effect::Transparency)
            made.amount = kind == Effect::Tint ? 0.5 : kind == Effect::Particles ? 1.0 : 4.0;
        e.push_back(made);
    });
    return count(layer) - 1;
}

void ScriptEffects::remove(int layer, int effect)
{
    if (has(layer, effect))
        change(layer, [effect](QVector<Effect>& e) { e.removeAt(effect); });
}

// ---- views --------------------------------------------------------------------------------

QDockWidget* ScriptViews::dock(QString const& name) const
{
    if (!m_window)
        return nullptr;
    for (QDockWidget* const d : m_window->findChildren<QDockWidget*>())
        if (d->windowTitle().compare(name, Qt::CaseInsensitive) == 0)
            return d;
    return nullptr;
}

Canvas* ScriptViews::canvas() const
{
    return m_window ? m_window->findChild<Canvas*>() : nullptr;
}

QStringList ScriptViews::list() const
{
    QStringList names;
    if (m_window)
        for (QDockWidget* const d : m_window->findChildren<QDockWidget*>())
            if (!d->windowTitle().isEmpty())
                names << d->windowTitle();
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ScriptViews::isOpen(QString const& name) const
{
    QDockWidget* const d = dock(name);
    return d && !d->isHidden();
}

bool ScriptViews::open(QString const& name)
{
    QDockWidget* const d = dock(name);
    if (!d)
        return false;
    d->show();
    d->raise();
    return true;
}

bool ScriptViews::close(QString const& name)
{
    QDockWidget* const d = dock(name);
    if (!d)
        return false;
    d->hide();
    return true;
}

bool ScriptViews::drawingView() const
{
    Canvas* const c = canvas();
    return c && c->drawingView();
}

void ScriptViews::setDrawingView(bool on)
{
    if (Canvas* const c = canvas())
        c->setDrawingView(on);
}

double ScriptViews::zoom() const
{
    Canvas* const c = canvas();
    return c ? c->zoom() : 1.0;
}

void ScriptViews::setZoom(double zoom)
{
    if (Canvas* const c = canvas())
        if (zoom > 0.0 && c->zoom() > 0.0)
            c->zoomBy(zoom / c->zoom());
}

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

// ---- colouring ----------------------------------------------------------------------------

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
