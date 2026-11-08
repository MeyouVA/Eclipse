// Eclipse - a Master Controller's controls, made by its own script (see MasterControls.h).
#include "animation/ui/views/MasterControls.h"

#include "model/document/Commands.h"
#include "animation/ui/timeline/Timeline.h"
#include "animation/ui/views/ScriptGlobals.h"
#include "animation/ui/views/ScriptView.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJSEngine>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace {
constexpr int kSteps = 1000;    // a slider's steps from its least to its most
}

// ---- the pad ----

MasterPad::MasterPad(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(80, 80);
    setMouseTracking(false);
}

void MasterPad::setRange(QRectF range)
{
    m_range = range.normalized();
    if (m_range.width() <= 0.0)
        m_range.setWidth(1.0);
    if (m_range.height() <= 0.0)
        m_range.setHeight(1.0);
    update();
}

void MasterPad::setPoints(QVector<QPointF> const& points)
{
    if (m_dragging >= 0)
        return;     // (the one being dragged is where the pen is)
    m_points = points;
    update();
}

QRectF MasterPad::square() const
{
    double const side = std::max(10, std::min(width(), height()) - 8);
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QPointF MasterPad::toWidget(QPointF value) const
{
    QRectF const s = square();
    // (Up the values is up the pad.)
    return QPointF(s.left() + (value.x() - m_range.left()) / m_range.width() * s.width(),
                   s.bottom() - (value.y() - m_range.top()) / m_range.height() * s.height());
}

QPointF MasterPad::toValue(QPointF widgetPos) const
{
    QRectF const s = square();
    double const x = std::clamp((widgetPos.x() - s.left()) / s.width(), 0.0, 1.0);
    double const y = std::clamp((s.bottom() - widgetPos.y()) / s.height(), 0.0, 1.0);
    return QPointF(m_range.left() + x * m_range.width(), m_range.top() + y * m_range.height());
}

void MasterPad::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF const s = square();
    p.fillRect(s, palette().base());
    p.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));
    p.drawLine(QPointF(s.center().x(), s.top()), QPointF(s.center().x(), s.bottom()));
    p.drawLine(QPointF(s.left(), s.center().y()), QPointF(s.right(), s.center().y()));
    p.setPen(palette().text().color());
    p.drawRect(s);
    if (m_points.size() == 2) {
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawLine(toWidget(m_points[0]), toWidget(m_points[1]));
    }
    p.setPen(palette().text().color());
    p.setBrush(palette().highlight());
    for (QPointF const& point : m_points)
        p.drawEllipse(toWidget(point), 5, 5);
}

void MasterPad::mousePressEvent(QMouseEvent* event)
{
    m_dragging = -1;
    double nearest = 1e9;
    for (int i = 0; i < m_points.size(); ++i) {
        double const d = QLineF(toWidget(m_points[i]), event->position()).length();
        if (d < nearest)
            nearest = d, m_dragging = i;
    }
    // (One point: wherever the pad is pressed takes it there.)
    if (m_points.size() != 1 && nearest > 12)
        m_dragging = -1;
    if (m_dragging >= 0) {
        m_points[m_dragging] = toValue(event->position());
        update();
    }
}

void MasterPad::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging >= 0) {
        m_points[m_dragging] = toValue(event->position());
        update();
    }
}

void MasterPad::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging < 0)
        return;
    int const point = m_dragging;
    m_points[point] = toValue(event->position());
    m_dragging = -1;
    update();
    emit moved(point, m_points[point]);
}

// ---- the controls ----

MasterControls::MasterControls(Document* document, Timeline* timeline, QWidget* parent)
    : QWidget(parent), m_document(document), m_timeline(timeline)
{
    setObjectName(QStringLiteral("MasterControls"));
    m_rows = new QVBoxLayout(this);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_said = new QLabel(this);
    m_said->setObjectName(QStringLiteral("MasterControllerMessages"));
    m_said->setWordWrap(true);
    m_said->hide();
    m_rows->addWidget(m_said);
}

MasterControls::~MasterControls() = default;

void MasterControls::setController(int controller)
{
    Scene const& scene = m_document->scene();
    QString const script = controller >= 0 && controller < scene.controllers.size() ? scene.controllers[controller].script : QString();
    if (controller == m_controller && script == m_script && m_engine)
        return refresh();
    m_controller = controller;
    m_script = script;
    rebuild();
}

void MasterControls::rebuild()
{
    if (m_building)
        return;
    m_building = true;
    for (Control const& c : std::as_const(m_controls))
        delete c.widget;
    m_controls.clear();
    m_messages.clear();
    m_engine.reset();
    m_parts.reset();
    Scene const& scene = m_document->scene();
    if (m_controller < 0 || m_controller >= scene.controllers.size() || !scene.controllers[m_controller].master) {
        m_said->hide();
        m_building = false;
        return;
    }
    m_parts = std::make_unique<QObject>();
    m_engine = std::make_unique<QJSEngine>();
    QJSEngine& engine = *m_engine;
    m_output = new ScriptOutput(m_parts.get());
    m_layers = new ScriptLayers(m_document, m_timeline, &engine, m_parts.get());
    ScriptFrame* const frame = new ScriptFrame(m_timeline, m_parts.get());
    MasterScriptControls* const controls = new MasterScriptControls(this, m_parts.get());
    for (QObject* o : {static_cast<QObject*>(m_output), static_cast<QObject*>(m_layers), static_cast<QObject*>(frame), static_cast<QObject*>(controls)})
        QJSEngine::setObjectOwnership(o, QJSEngine::CppOwnership);
    engine.globalObject().setProperty(QStringLiteral("__out"), engine.newQObject(m_output));
    engine.globalObject().setProperty(QStringLiteral("layers"), engine.newQObject(m_layers));
    engine.globalObject().setProperty(QStringLiteral("frame"), engine.newQObject(frame));
    engine.globalObject().setProperty(QStringLiteral("controls"), engine.newQObject(controls));
    engine.evaluate(QStringLiteral("function log(t) { __out.log(String(t)); } var print = log;"));
    ScriptContext context;
    context.document = m_document;
    context.timeline = m_timeline;
    context.window = window();
    context.engine = &engine;
    context.output = m_output;
    context.layers = m_layers;
    installScriptGlobals(context, m_parts.get());

    // A script that never ends is stopped after a while.
    std::atomic<bool> finished{false};
    std::thread watch([&engine, &finished] {
        for (int waited = 0; waited < 100 && !finished; ++waited)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!finished)
            engine.setInterrupted(true);
    });
    QJSValue const result = engine.evaluate(m_script, scene.controllers[m_controller].name + QStringLiteral(".js"));
    finished = true;
    watch.join();
    if (engine.isInterrupted()) {
        m_messages << tr("Stopped: the Master Controller's script ran for too long.");
        engine.setInterrupted(false);
    } else if (result.isError()) {
        m_messages << tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString());
    }
    m_messages = m_output->lines + m_messages;
    m_output->lines.clear();
    for (int i = 0; i < m_controls.size(); ++i)
        makeWidget(i);
    m_said->setText(m_messages.join(QLatin1Char('\n')));
    m_said->setVisible(!m_messages.isEmpty());
    m_rows->removeWidget(m_said);
    m_rows->addWidget(m_said);
    for (QString const& line : std::as_const(m_messages))
        emit said(line);
    m_building = false;
    refresh();
}

QString MasterControls::kindOf(int control) const
{
    return control >= 0 && control < m_controls.size() ? m_controls[control].kind : QString();
}

QString MasterControls::labelOf(int control) const
{
    return control >= 0 && control < m_controls.size() ? m_controls[control].label : QString();
}

int MasterControls::addControl(QString const& kind, QJSValue const& options)
{
    Control c;
    c.kind = kind;
    c.label = options.property(QStringLiteral("label")).isUndefined() ? QString() : options.property(QStringLiteral("label")).toString();
    auto text = [&options](char const* name) {
        QJSValue const v = options.property(QLatin1String(name));
        return v.isUndefined() || v.isNull() ? QString() : v.toString();
    };
    auto number = [&options](char const* name, double fallback) {
        QJSValue const v = options.property(QLatin1String(name));
        return v.isNumber() ? v.toNumber() : fallback;
    };
    QString kindName;
    if (kind == QLatin1String("slider")) {
        kindName = tr("slider");
        c.attributes = {text("attribute")};
        double const low = number("min", 0.0), high = number("max", 1.0);
        c.range = QRectF(QPointF(std::min(low, high), 0.0), QPointF(std::max(low, high), 1.0));
    } else if (kind == QLatin1String("point") || kind == QLatin1String("line")) {
        kindName = kind == QLatin1String("point") ? tr("point") : tr("line");
        c.attributes = kind == QLatin1String("point") ? QStringList{text("x"), text("y")} : QStringList{text("x1"), text("y1"), text("x2"), text("y2")};
        double const low = number("min", -100.0), high = number("max", 100.0);
        double const left = number("minX", low), right = number("maxX", high), bottom = number("minY", low), top = number("maxY", high);
        c.range = QRectF(QPointF(std::min(left, right), std::min(bottom, top)), QPointF(std::max(left, right), std::max(bottom, top)));
    } else if (kind == QLatin1String("check")) {
        kindName = tr("check box");
        c.attributes = {text("attribute")};
        c.on = number("on", 1.0);
        c.off = number("off", 0.0);
    } else {
        kindName = tr("label");
        c.text = text("text");
        if (QString const shown = text("attribute"); !shown.isEmpty())
            c.attributes = {shown};
    }
    for (QString const& path : std::as_const(c.attributes))
        if (path.isEmpty() || !m_layers || !m_layers->hasAttribute(path)) {
            QString const line = tr("Logic error: %1 widget attribute creation failed: \"%2\" is not an attribute.").arg(kindName, path);
            if (m_building)
                m_messages << line;
            else
                emit said(line);
            return -1;
        }
    m_controls.push_back(c);
    if (!m_building)
        makeWidget(int(m_controls.size()) - 1);
    return int(m_controls.size()) - 1;
}

void MasterControls::makeWidget(int i)
{
    Control& c = m_controls[i];
    QWidget* const row = new QWidget(this);
    row->setObjectName(QStringLiteral("MasterControl%1").arg(i));
    QHBoxLayout* const line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    QLabel* const name = new QLabel(c.kind == QLatin1String("label") && c.attributes.isEmpty() ? c.text : c.label, row);
    line->addWidget(name);
    if (c.kind == QLatin1String("slider")) {
        QSlider* const slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(QStringLiteral("MasterSlider"));
        slider->setRange(0, kSteps);
        // (Set when it is let go, or moved with the keys: one step of undo each time.)
        slider->setTracking(false);
        c.value = new QLabel(row);
        c.value->setMinimumWidth(48);
        line->addWidget(slider, 1);
        line->addWidget(c.value);
        connect(slider, &QSlider::valueChanged, this, [this, i](int v) {
            if (m_refreshing || i >= m_controls.size())
                return;
            QRectF const r = m_controls[i].range;
            double const value = r.left() + v / double(kSteps) * r.width();
            apply(i, {value}, value);
        });
    } else if (c.kind == QLatin1String("point") || c.kind == QLatin1String("line")) {
        MasterPad* const pad = new MasterPad(row);
        pad->setObjectName(QStringLiteral("MasterPad"));
        pad->setRange(c.range);
        pad->setFixedSize(120, 120);
        line->addWidget(pad, 1);
        connect(pad, &MasterPad::moved, this, [this, i](int point, QPointF value) { set(i, value.x(), value.y(), point); });
    } else if (c.kind == QLatin1String("check")) {
        QCheckBox* const box = new QCheckBox(row);
        box->setObjectName(QStringLiteral("MasterCheck"));
        line->addWidget(box, 1);
        connect(box, &QCheckBox::toggled, this, [this, i](bool on) {
            if (!m_refreshing && i < m_controls.size())
                apply(i, {on ? m_controls[i].on : m_controls[i].off}, on ? 1.0 : 0.0);
        });
    } else if (!c.attributes.isEmpty()) {
        c.value = new QLabel(row);
        line->addWidget(c.value, 1);
    }
    c.widget = row;
    m_rows->insertWidget(m_rows->indexOf(m_said), row);
}

double MasterControls::read(QString const& path) const
{
    return m_layers ? m_layers->attribute(path, frame()) : 0.0;
}

void MasterControls::refresh()
{
    if (m_building)
        return;
    m_refreshing = true;
    for (Control const& c : std::as_const(m_controls)) {
        if (!c.widget)
            continue;
        if (c.kind == QLatin1String("slider")) {
            double const v = read(c.attributes[0]);
            if (QSlider* const slider = c.widget->findChild<QSlider*>(); slider && !slider->isSliderDown())
                slider->setValue(int(std::lround(std::clamp((v - c.range.left()) / c.range.width(), 0.0, 1.0) * kSteps)));
            c.value->setText(QString::number(v, 'f', 2));
        } else if (c.kind == QLatin1String("point") || c.kind == QLatin1String("line")) {
            QVector<QPointF> points;
            for (int a = 0; a + 1 < c.attributes.size(); a += 2)
                points.push_back(QPointF(read(c.attributes[a]), read(c.attributes[a + 1])));
            if (MasterPad* const pad = c.widget->findChild<MasterPad*>())
                pad->setPoints(points);
        } else if (c.kind == QLatin1String("check")) {
            if (QCheckBox* const box = c.widget->findChild<QCheckBox*>())
                box->setChecked(std::abs(read(c.attributes[0]) - c.on) < 1e-9);
        } else if (!c.attributes.isEmpty() && c.value) {
            c.value->setText(QStringLiteral("%1 %2").arg(c.text, QString::number(read(c.attributes[0]), 'f', 2)).trimmed());
        }
    }
    m_refreshing = false;
}

bool MasterControls::set(int control, double value, double y, int point)
{
    if (control < 0 || control >= m_controls.size())
        return false;
    Control const& c = m_controls[control];
    if (c.kind == QLatin1String("slider")) {
        double const v = std::clamp(value, c.range.left(), c.range.right());
        apply(control, {v}, v);
    } else if (c.kind == QLatin1String("check")) {
        apply(control, {value != 0.0 ? c.on : c.off}, value != 0.0 ? 1.0 : 0.0);
    } else if (c.kind == QLatin1String("point") || c.kind == QLatin1String("line")) {
        int const end = c.kind == QLatin1String("line") ? std::clamp(point, 0, 1) : 0;
        double const x = std::clamp(value, c.range.left(), c.range.right()), yy = std::clamp(y, c.range.top(), c.range.bottom());
        QVector<double> values;
        for (QString const& path : c.attributes)
            values.push_back(read(path));
        values[end * 2] = x;
        values[end * 2 + 1] = yy;
        apply(control, values, x);
    } else {
        return false;
    }
    return true;
}

void MasterControls::apply(int control, QVector<double> const& values, double reported)
{
    Control const c = m_controls[control];
    Scene const& scene = m_document->scene();
    QString const name = m_controller >= 0 && m_controller < scene.controllers.size() ? scene.controllers[m_controller].name : QString();
    m_document->beginMacro(tr("Master Controller: %1").arg(name));
    for (int a = 0; a < c.attributes.size() && a < values.size(); ++a)
        if (std::abs(read(c.attributes[a]) - values[a]) > 1e-9)
            m_layers->setAttribute(c.attributes[a], frame(), values[a]);
    if (m_engine) {
        QJSValue const onChange = m_engine->globalObject().property(QStringLiteral("onChange"));
        if (onChange.isCallable()) {
            QJSValue const result = onChange.call({QJSValue(c.label), QJSValue(reported)});
            if (result.isError())
                emit said(tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString()));
        }
        for (QString const& line : std::as_const(m_output->lines))
            emit said(line);
        m_output->lines.clear();
    }
    m_document->endMacro();
    // (Nothing changed: no step left behind.)
    if (QUndoStack* const stack = m_document->undoStack(); stack->index() > 0 && stack->command(stack->index() - 1)->childCount() == 0)
        stack->undo();
    refresh();
}

QJSValue MasterControls::call(QString const& function, QJSValueList const& arguments)
{
    if (!m_engine)
        return QJSValue();
    QJSValue const f = m_engine->globalObject().property(function);
    if (!f.isCallable())
        return QJSValue();
    QJSValue const result = f.call(arguments);
    if (result.isError())
        m_messages << tr("Line %1: %2").arg(result.property(QStringLiteral("lineNumber")).toInt()).arg(result.toString());
    m_messages << m_output->lines;
    m_output->lines.clear();
    refresh();
    return result;
}

bool MasterControls::runOnLoad(Document* document, Timeline* timeline, int controller, QStringList* said)
{
    Scene const& scene = document->scene();
    if (controller < 0 || controller >= scene.controllers.size() || !scene.controllers[controller].master)
        return false;
    MasterControls controls(document, timeline);
    document->beginMacro(tr("Master Controller - On Load"));
    controls.setController(controller);
    bool const has = controls.m_engine && controls.m_engine->globalObject().property(QStringLiteral("onLoad")).isCallable();
    if (has)
        controls.call(QStringLiteral("onLoad"));
    document->endMacro();
    if (QUndoStack* const stack = document->undoStack(); stack->index() > 0 && stack->command(stack->index() - 1)->childCount() == 0)
        stack->undo();
    if (said)
        *said = controls.messages();
    return has;
}

QString MasterControls::data() const
{
    Scene const& scene = m_document->scene();
    return m_controller >= 0 && m_controller < scene.controllers.size() ? scene.controllers[m_controller].data : QString();
}

void MasterControls::setData(QString const& data)
{
    if (m_controller >= 0)
        m_document->setControllerData(m_controller, data);
}

QString MasterControls::controllerName() const
{
    Scene const& scene = m_document->scene();
    return m_controller >= 0 && m_controller < scene.controllers.size() ? scene.controllers[m_controller].name : QString();
}

int MasterControls::frame() const
{
    return (m_timeline ? m_timeline->currentFrame() : 0) + 1;
}
