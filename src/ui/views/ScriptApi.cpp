// Eclipse - what a script sees: the scene, the frame, layers, effects, views and the palette.
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
#include "canvas/Canvas.h"
#include <QDockWidget>

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

int ScriptFrame::current() const
{
    return m_timeline->currentFrame() + 1;
}

void ScriptFrame::setCurrent(int frame)
{
    m_timeline->setCurrentFrame(frame - 1);
}

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
    static char const* const kKinds[] = {"drawing", "peg", "camera", "sound", "colour-card", "group", "bone", "quadmap", "annotation"};
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
