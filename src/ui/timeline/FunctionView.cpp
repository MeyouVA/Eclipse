// Eclipse - the Function view: its graph - what it shows, fitting, zoom, where things are on it; the view round it.
#include "ui/timeline/FunctionView.h"

#include <QWheelEvent>

#include <QHBoxLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

#include "model/document/Commands.h"
#include "actions/theme/InterfaceColours.h"

FunctionGraph::FunctionGraph(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setObjectName(QStringLiteral("FunctionGraph"));
    setMinimumSize(160, 100);
    connect(m_document, &Document::changed, this, [this] {
        fit();
        update();
    });
    fit();
}

QColor FunctionGraph::colourOf(Timeline::Parameter which)
{
    // Across, down and depth are the user's (the Colours page); the others are fixed.
    if (which == Timeline::PosX)
        return InterfaceColours::get("functionCurveX");
    if (which == Timeline::PosY)
        return InterfaceColours::get("functionCurveY");
    if (which == Timeline::PosZ)
        return InterfaceColours::get("functionCurveZ");
    static QColor const colours[Timeline::ParameterCount] = {QColor(232, 96, 84),  QColor(110, 200, 120), QColor(240, 170, 70),
                                                             QColor(100, 170, 240), QColor(200, 130, 230), QColor(210, 210, 210),
                                                             QColor(90, 210, 200), QColor(180, 190, 90), QColor(230, 120, 170)};
    return colours[which];
}

void FunctionGraph::setCurrentCell(int layer, int frame)
{
    if (layer == m_layer && frame == m_frame)
        return;
    bool const other = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (other)
        fit();
    update();
}

void FunctionGraph::setShown(Timeline::Parameter which, bool shown)
{
    if (m_shown[which] == shown)
        return;
    m_shown[which] = shown;
    fit();
    update();
}

double FunctionGraph::valueAt(Timeline::Parameter which, int frame) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return 0.0;
    if (which == Timeline::Fov)
        return scene.layers[m_layer].fovAt(frame);
    double const value = Timeline::parameterOf(scene.layers[m_layer].poseAt(frame), which);
    return which == Timeline::Opacity ? value * 100.0 : value;
}

int FunctionGraph::firstShown() const
{
    for (int which = 0; which < Timeline::ParameterCount; ++which)
        if (m_shown[which])
            return which;
    return -1;
}

QPointF FunctionGraph::easeHandleOf(Layer const& layer, int key, bool out) const
{
    int const which = firstShown();
    int const other = out ? key + 1 : key - 1;
    if (which < 0 || key < 0 || key >= layer.keys.size() || other < 0 || other >= layer.keys.size())
        return QPointF();
    double const ease = std::clamp(m_easeDrag == (out ? 1 : 2) && m_easeKey == key ? m_easeValue : out ? layer.keys[key].easeOut : layer.keys[key].easeIn, 0.0, 1.0);
    double const from = xOfFrame(layer.keys[key].frame), to = xOfFrame(layer.keys[other].frame);
    double const scale = which == Timeline::Opacity ? 100.0 : 1.0;
    double const value = Timeline::parameterOf(layer.keys[key].pose, Timeline::Parameter(which)) * scale;
    double const beyond = Timeline::parameterOf(layer.keys[other].pose, Timeline::Parameter(which)) * scale;
    double const lift = m_easeDrag == (out ? 1 : 2) && m_easeKey == key ? m_liftValue : out ? layer.keys[key].liftOut : layer.keys[key].liftIn;
    // (Out: that share of the way on to the next value.  In: that share of the way come, carried on past this one.)
    double const lifted = out ? value + (beyond - value) * lift : value + (value - beyond) * lift;
    return QPointF(from + (to - from) * (0.1 + 0.4 * ease), yOfValue(lifted));
}

QPointF FunctionGraph::easeHandle(bool out) const
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return QPointF();
    return easeHandleOf(scene.layers[m_layer], scene.layers[m_layer].keyIndex(m_frame), out);
}

void FunctionGraph::setOtherLayers(QVector<int> const& layers)
{
    QVector<int> others;
    for (int l : layers)
        if (l != m_layer && l >= 0 && l < m_document->scene().layers.size() && !others.contains(l))
            others.push_back(l);
    if (others == m_others)
        return;
    m_others = others;
    fit();
    update();
}

void FunctionGraph::fit()
{
    if (m_zoomed)
        return;     // the values were chosen by hand
    // From the lowest to the highest value any curve shown takes, with room above and below.
    Scene const& scene = m_document->scene();
    bool any = false;
    double low = 0.0, high = 0.0;
    for (int which = 0; which < Timeline::ParameterCount; ++which) {
        if (!m_shown[which])
            continue;
        for (int f = 0; f < scene.frameCount(); ++f) {
            double const v = valueAt(Timeline::Parameter(which), f);
            low = any ? std::min(low, v) : v;
            high = any ? std::max(high, v) : v;
            any = true;
            // ... and any curve of the other layers shown with it.
            if (which == Timeline::Fov)
                continue;
            for (int other : m_others) {
                if (other == m_layer || other >= scene.layers.size())
                    continue;
                double const o = Timeline::parameterOf(scene.layers[other].poseAt(f), Timeline::Parameter(which)) * (which == Timeline::Opacity ? 100.0 : 1.0);
                low = std::min(low, o);
                high = std::max(high, o);
            }
        }
    }
    double const room = std::max((high - low) * 0.15, std::max(std::abs(high), std::abs(low)) * 0.05 + 1.0);
    m_low = low - room;
    m_high = high + room;
}

double FunctionGraph::frameSpan() const
{
    return m_span > 0.0 ? m_span : std::max(2, m_document->scene().frameCount()) - 1.0;
}

void FunctionGraph::resetView()
{
    m_first = 0.0;
    m_span = 0.0;
    m_zoomed = false;
    fit();
    update();
}

void FunctionGraph::wheelEvent(QWheelEvent* event)
{
    QRectF const r = plot();
    double const factor = std::pow(1.2, -event->angleDelta().y() / 120.0);     // rolled away: closer
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        // Along the values, about the one under the pointer.
        double const at = valueOfY(event->position().y());
        double const span = std::clamp((m_high - m_low) * factor, 1e-3, 1e9);
        double const share = (at - m_low) / (m_high - m_low);
        m_low = at - span * share;
        m_high = m_low + span;
        m_zoomed = true;
    } else {
        // Along the frames, about the one under the pointer; never further out than the whole scene.
        double const whole = std::max(2, m_document->scene().frameCount()) - 1.0;
        double const at = m_first + (event->position().x() - r.left()) * frameSpan() / r.width();
        double const span = std::clamp(frameSpan() * factor, 2.0, whole);
        m_first = std::clamp(at - (event->position().x() - r.left()) * span / r.width(), 0.0, whole - span);
        m_span = span >= whole - 1e-9 ? 0.0 : span;
        if (m_span == 0.0)
            m_first = 0.0;
    }
    update();
    event->accept();
}

QRectF FunctionGraph::plot() const
{
    return QRectF(46, 8, std::max(10, width() - 54), std::max(10, height() - 28));
}

double FunctionGraph::xOfFrame(double frame) const
{
    QRectF const r = plot();
    return r.left() + (frame - m_first) * r.width() / frameSpan();
}

int FunctionGraph::frameOfX(double x) const
{
    QRectF const r = plot();
    return std::clamp(int(std::lround(m_first + (x - r.left()) * frameSpan() / r.width())), 0, m_document->scene().frameCount() - 1);
}

double FunctionGraph::yOfValue(double value) const
{
    QRectF const r = plot();
    return r.bottom() - (value - m_low) * r.height() / (m_high - m_low);
}

double FunctionGraph::valueOfY(double y) const
{
    QRectF const r = plot();
    return m_low + (r.bottom() - y) * (m_high - m_low) / r.height();
}

QPointF FunctionGraph::pointAt(Timeline::Parameter which, int frame) const
{
    return QPointF(xOfFrame(frame), yOfValue(valueAt(which, frame)));
}

void FunctionGraph::setGridShown(bool on)
{
    m_grid = on;
    update();
}

FunctionView::FunctionView(Document* document, QWidget* parent) : QWidget(parent)
{
    m_graph = new FunctionGraph(document, this);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("FunctionList"));
    m_list->setFixedWidth(110);
    for (int which = 0; which < Timeline::ParameterCount; ++which) {
        Timeline::Parameter const param = Timeline::Parameter(which);
        QPixmap swatch(12, 12);
        swatch.fill(FunctionGraph::colourOf(param));
        QListWidgetItem* const item = new QListWidgetItem(QIcon(swatch), Timeline::parameterName(param), m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_graph->isShown(param) ? Qt::Checked : Qt::Unchecked);
    }
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        m_graph->setShown(Timeline::Parameter(m_list->row(item)), item->checkState() == Qt::Checked);
    });
    connect(m_graph, &FunctionGraph::framePicked, this, &FunctionView::framePicked);
    QHBoxLayout* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list);
    layout->addWidget(m_graph, 1);
}
