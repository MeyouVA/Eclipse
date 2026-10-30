// Eclipse - the ease editors: the curve with its two handles, Set Ease for Multiple Parameters, the Velocity Editor.
#include "ui/views/EaseEditors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "actions/theme/InterfaceColours.h"
#include "model/document/Commands.h"

namespace {

double cubic(double s, double p1, double p2)
{
    double const u = 1.0 - s;
    return 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s;
}

}  // namespace

EaseCurveEdit::EaseCurveEdit(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("EaseCurve"));
    setMinimumSize(160, 130);
}

void EaseCurveEdit::setHandles(QPointF leave, QPointF arrive)
{
    m_leave = QPointF(std::clamp(leave.x(), 0.0, 1.0), std::clamp(leave.y(), -2.0, 3.0));
    m_arrive = QPointF(std::clamp(arrive.x(), 0.0, 1.0), std::clamp(arrive.y(), -2.0, 3.0));
    update();
}

double EaseCurveEdit::progressAt(double time) const
{
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 48; ++i) {
        double const mid = (lo + hi) / 2.0;
        (cubic(mid, m_leave.x(), m_arrive.x()) < time ? lo : hi) = mid;
    }
    return cubic((lo + hi) / 2.0, m_leave.y(), m_arrive.y());
}

QRectF EaseCurveEdit::box() const
{
    // Room above and below for a curve that goes past either end.
    return QRectF(12, 28, std::max(20, width() - 24), std::max(20, height() - 56));
}

QPointF EaseCurveEdit::toWidget(QPointF p) const
{
    QRectF const r = box();
    return QPointF(r.left() + p.x() * r.width(), r.bottom() - p.y() * r.height());
}

QPointF EaseCurveEdit::fromWidget(QPointF p) const
{
    QRectF const r = box();
    return QPointF((p.x() - r.left()) / r.width(), (r.bottom() - p.y()) / r.height());
}

QPointF EaseCurveEdit::handlePos(bool leaving) const
{
    QPointF const at = leaving ? QPointF(0, 0) : QPointF(1, 1);
    QPointF const h = leaving ? m_leave : m_arrive;
    if (QLineF(toWidget(at), toWidget(h)).length() < 3.0)
        return toWidget(leaving ? QPointF(1.0 / 3.0, 1.0 / 3.0) : QPointF(2.0 / 3.0, 2.0 / 3.0));
    return toWidget(h);
}

void EaseCurveEdit::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), InterfaceColours::get("functionBackground"));
    QRectF const r = box();
    p.fillRect(r, InterfaceColours::get("functionPlot"));
    p.setPen(InterfaceColours::get("functionGrid"));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4.0, r.top()), QPointF(r.left() + r.width() * i / 4.0, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4.0), QPointF(r.right(), r.top() + r.height() * i / 4.0));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    QPolygonF curve;
    for (int i = 0; i <= 64; ++i)
        curve << toWidget(QPointF(i / 64.0, progressAt(i / 64.0)));
    p.setPen(QPen(InterfaceColours::get("functionCurveX"), 1.8));
    p.drawPolyline(curve);
    for (bool leaving : {true, false}) {
        QPointF const at = toWidget(leaving ? QPointF(0, 0) : QPointF(1, 1)), h = handlePos(leaving);
        p.setPen(QPen(InterfaceColours::get("functionHandle"), 1.0));
        p.drawLine(at, h);
        p.setBrush(m_drag == (leaving ? 1 : 2) ? InterfaceColours::get("functionHandle") : QColor(46, 46, 46));
        p.drawEllipse(h, 4.0, 4.0);
        p.setBrush(QColor(235, 235, 235));
        p.drawRect(QRectF(at.x() - 3, at.y() - 3, 6, 6));
    }
}

void EaseCurveEdit::mousePressEvent(QMouseEvent* event)
{
    m_drag = 0;
    for (bool leaving : {true, false})
        if (QLineF(handlePos(leaving), event->position()).length() <= 7.0)
            m_drag = leaving ? 1 : 2;
    update();
}

void EaseCurveEdit::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == 0)
        return;
    QPointF const at = fromWidget(event->position());
    // Shift: kept level with its keyframe (no lift, a plain ease).
    bool const level = event->modifiers() & Qt::ShiftModifier;
    if (m_drag == 1)
        m_leave = QPointF(std::clamp(at.x(), 0.0, 1.0), level ? 0.0 : std::clamp(at.y(), -2.0, 3.0));
    else
        m_arrive = QPointF(std::clamp(at.x(), 0.0, 1.0), level ? 1.0 : std::clamp(at.y(), -2.0, 3.0));
    update();
    emit changed();
}

void EaseCurveEdit::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == 0)
        return;
    m_drag = 0;
    update();
    emit edited();
}

QStringList EaseMultipleDialog::presetNames()
{
    return {tr("Linear"),
            tr("Slow Ease IN"), tr("Medium Ease IN"), tr("Fast Ease IN"),
            tr("Slow Ease OUT"), tr("Medium Ease OUT"), tr("Fast Ease OUT"),
            tr("Slow Ease IN/OUT"), tr("Medium Ease IN/OUT"), tr("Fast Ease IN/OUT")};
}

void EaseMultipleDialog::preset(QString const& name, QPointF& leave, QPointF& arrive)
{
    // In: arriving gently at the second keyframe (its handle level); out: leaving the first gently.  Slow eases are long.
    leave = QPointF(1.0 / 3.0, 1.0 / 3.0);
    arrive = QPointF(2.0 / 3.0, 2.0 / 3.0);
    double const reach = name.startsWith(QLatin1String("Slow")) ? 0.8 : name.startsWith(QLatin1String("Medium")) ? 0.5 : 0.25;
    if (name == QLatin1String("Linear"))
        return;
    if (name.endsWith(QLatin1String("IN")) || name.endsWith(QLatin1String("IN/OUT")))
        arrive = QPointF(1.0 - reach, 1.0);
    if (name.endsWith(QLatin1String("OUT")))
        leave = QPointF(reach, 0.0);
}

EaseMultipleDialog::EaseMultipleDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("EaseMultipleDialog"));
    setWindowTitle(tr("Set Ease for Multiple Parameters"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    m_presets = new QComboBox(this);
    m_presets->setObjectName(QStringLiteral("EasePreset"));
    m_presets->addItem(tr("Custom"));
    m_presets->addItems(presetNames());
    layout->addWidget(m_presets);
    m_curve = new EaseCurveEdit(this);
    layout->addWidget(m_curve, 1);
    // The handles as numbers: how far into the move each reaches, and how far along the way.
    QGridLayout* const numbers = new QGridLayout;
    auto box = [this](char const* name) {
        QDoubleSpinBox* const b = new QDoubleSpinBox(this);
        b->setObjectName(QLatin1String(name));
        b->setRange(-200.0, 300.0);
        b->setDecimals(1);
        b->setSuffix(QStringLiteral(" %"));
        return b;
    };
    m_outTime = box("EaseOutTime");
    m_outValue = box("EaseOutValue");
    m_inTime = box("EaseInTime");
    m_inValue = box("EaseInValue");
    numbers->addWidget(new QLabel(tr("Ease Out (right)"), this), 0, 0);
    numbers->addWidget(m_outTime, 0, 1);
    numbers->addWidget(m_outValue, 0, 2);
    numbers->addWidget(new QLabel(tr("Ease In (left)"), this), 1, 0);
    numbers->addWidget(m_inTime, 1, 1);
    numbers->addWidget(m_inValue, 1, 2);
    layout->addLayout(numbers);
    // The kinds of value it is given to.
    QGridLayout* const kinds = new QGridLayout;
    QStringList const names = {tr("Motion"), tr("Rotation"), tr("Scale"), tr("Skew"), tr("Morphing"), tr("Other")};
    QStringList const ids = {QStringLiteral("motion"), QStringLiteral("rotation"), QStringLiteral("scale"), QStringLiteral("skew"),
                             QStringLiteral("morphing"), QStringLiteral("other")};
    for (int i = 0; i < names.size(); ++i) {
        QCheckBox* const tick = new QCheckBox(names[i], this);
        tick->setObjectName(QStringLiteral("EaseKind_") + ids[i]);
        tick->setProperty("kind", ids[i]);
        tick->setChecked(true);
        kinds->addWidget(tick, i / 3, i % 3);
        m_kinds.push_back(tick);
    }
    layout->addLayout(kinds);
    QDialogButtonBox* const buttons = new QDialogButtonBox(this);
    QPushButton* const apply = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    QPushButton* const previous = buttons->addButton(tr("Apply/Previous"), QDialogButtonBox::ActionRole);
    QPushButton* const next = buttons->addButton(tr("Apply/Next"), QDialogButtonBox::ActionRole);
    QPushButton* const close = buttons->addButton(QDialogButtonBox::Close);
    apply->setObjectName(QStringLiteral("EaseApply"));
    previous->setObjectName(QStringLiteral("EaseApplyPrevious"));
    next->setObjectName(QStringLiteral("EaseApplyNext"));
    connect(apply, &QPushButton::clicked, this, [this] { emit applyRequested(0); });
    connect(previous, &QPushButton::clicked, this, [this] { emit applyRequested(-1); });
    connect(next, &QPushButton::clicked, this, [this] { emit applyRequested(1); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_presets, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index <= 0 || m_filling)
            return;
        QPointF leave, arrive;
        preset(presetNames().value(index - 1), leave, arrive);
        m_curve->setHandles(leave, arrive);
        showNumbers();
    });
    connect(m_curve, &EaseCurveEdit::changed, this, [this] {
        m_filling = true;
        m_presets->setCurrentIndex(0);
        m_filling = false;
        showNumbers();
    });
    auto typed = [this] {
        if (m_filling)
            return;
        m_curve->setHandles(QPointF(m_outTime->value() / 100.0, m_outValue->value() / 100.0),
                            QPointF(1.0 - m_inTime->value() / 100.0, 1.0 - m_inValue->value() / 100.0));
        m_filling = true;
        m_presets->setCurrentIndex(0);
        m_filling = false;
    };
    for (QDoubleSpinBox* b : {m_outTime, m_outValue, m_inTime, m_inValue})
        connect(b, &QDoubleSpinBox::valueChanged, this, typed);
    m_presets->setCurrentIndex(int(presetNames().indexOf(QStringLiteral("Medium Ease IN/OUT"))) + 1);
    showNumbers();
}

void EaseMultipleDialog::showNumbers()
{
    m_filling = true;
    m_outTime->setValue(m_curve->leave().x() * 100.0);
    m_outValue->setValue(m_curve->leave().y() * 100.0);
    m_inTime->setValue((1.0 - m_curve->arrive().x()) * 100.0);
    m_inValue->setValue((1.0 - m_curve->arrive().y()) * 100.0);
    m_filling = false;
}

QStringList EaseMultipleDialog::kinds() const
{
    QStringList out;
    for (QCheckBox* tick : m_kinds)
        if (tick->isChecked())
            out << tick->property("kind").toString();
    return out;
}

void EaseMultipleDialog::setKinds(QStringList const& kinds)
{
    for (QCheckBox* tick : std::as_const(m_kinds))
        tick->setChecked(kinds.contains(tick->property("kind").toString()));
}

VelocityEditor::VelocityEditor(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setObjectName(QStringLiteral("VelocityEditor"));
    setWindowTitle(tr("Velocity Editor"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    m_what = new QLabel(this);
    m_what->setObjectName(QStringLiteral("VelocityWhat"));
    m_curve = new EaseCurveEdit(this);
    // Apply velocity on X, Y or Z: when the value is linked to a function column, the velocity of that column.
    m_channel = new QComboBox(this);
    m_channel->setObjectName(QStringLiteral("VelocityChannel"));
    m_channel->addItems({tr("Keyframes"), tr("X"), tr("Y"), tr("Z")});
    m_channel->setToolTip(tr("Keyframes: the layer's own. X, Y, Z: the function column the value is linked to."));
    connect(m_channel, &QComboBox::currentIndexChanged, this, &VelocityEditor::refresh);
    layout->addWidget(m_channel);
    layout->addWidget(m_what);
    layout->addWidget(m_curve, 1);
    connect(m_curve, &EaseCurveEdit::edited, this, &VelocityEditor::commit);
    connect(m_document, &Document::changed, this, &VelocityEditor::refresh);
    resize(300, 260);
}

void VelocityEditor::setCurrentCell(int layer, int frame)
{
    m_layer = layer;
    m_frame = frame;
    refresh();
}

void VelocityEditor::refresh()
{
    Scene const& scene = m_document->scene();
    m_from = m_to = -1;
    m_column = 0;
    setWindowTitle(tr("Velocity Editor"));
    if (m_layer < 0 || m_layer >= scene.layers.size()) {
        m_curve->setEnabled(false);
        m_what->setText(tr("No velocity available: the layer has no move from a keyframe here."));
        return;
    }
    Layer const* owner = &scene.layers[m_layer];
    int const which = m_channel->currentIndex();
    if (which > 0) {
        int const value = which == 1 ? Driver::X : which == 2 ? Driver::Y : Driver::Z;
        FunctionColumn const* c = scene.function(scene.layers[m_layer].linkOf(value));
        if (!c || c->kind == FunctionColumn::Expression) {
            m_curve->setEnabled(false);
            m_what->setText(c ? tr("No velocity available: an expression has no points.") : tr("No velocity column set"));
            return;
        }
        m_column = c->id;
        owner = c->hasPoints() && c->kind != FunctionColumn::VeloBased ? &c->curve : &c->velocity;
        setWindowTitle(tr("Velocity Editor - %1").arg(FunctionColumn::kindName(c->kind)));
    }
    QVector<Keyframe> const& keys = owner->keys;
    int k = -1;
    for (int i = 0; i < keys.size(); ++i)
        if (keys[i].frame <= m_frame)
            k = i;
    if (k >= 0 && k + 1 < keys.size()) {
        m_from = keys[k].frame;
        m_to = keys[k + 1].frame;
    }
    m_curve->setEnabled(m_from >= 0);
    if (m_from < 0) {
        m_what->setText(tr("No velocity available: the layer has no move from a keyframe here."));
        return;
    }
    m_what->setText(tr("%1: frames %2 to %3").arg(m_column ? scene.function(m_column)->name : scene.layers[m_layer].name).arg(m_from + 1).arg(m_to + 1));
    Keyframe const& a = keys[k];
    Keyframe const& b = keys[k + 1];
    ValueCurve const* const ca = m_column ? a.curveOf(Driver::X) : nullptr;
    ValueCurve const* const cb = m_column ? b.curveOf(Driver::X) : nullptr;
    if (ca || cb) {
        // Points with handles of their own: the handles, in shares of the time and the way between the two points.
        double const span = std::max(1, b.frame - a.frame), way = b.pose.x - a.pose.x;
        double const dy = std::abs(way) > 1e-9 ? way : 1.0;
        QPointF const leave = ca ? QPointF(ca->out.x() / span, ca->out.y() / dy) : QPointF(0, 0);
        QPointF const arrive = cb ? QPointF(1.0 + cb->in.x() / span, 1.0 + cb->in.y() / dy) : QPointF(1, 1);
        m_curve->setHandles(leave, arrive);
        return;
    }
    // The keyframes' ease as handles (see easedProgress).
    double const out = std::clamp(a.easeOut, 0.0, 1.0), in = std::clamp(b.easeIn, 0.0, 1.0);
    QPointF const leave(out * 0.8, (out > 0.0 || a.liftOut != 0.0 ? 0.0 : out * 0.8) + a.liftOut);
    double const x2 = 1.0 - in * 0.8;
    QPointF const arrive(x2, (in > 0.0 || b.liftIn != 0.0 ? 1.0 : x2) + b.liftIn);
    m_curve->setHandles(leave, arrive);
}

void VelocityEditor::commit()
{
    Scene const& scene = m_document->scene();
    if (m_from < 0 || m_layer < 0 || m_layer >= scene.layers.size())
        return;
    QPointF const leave = m_curve->leave(), arrive = m_curve->arrive();
    double const easeOut = std::clamp(leave.x() / 0.8, 0.0, 1.0), easeIn = std::clamp((1.0 - arrive.x()) / 0.8, 0.0, 1.0);
    if (m_column) {
        FunctionColumn const* now = scene.function(m_column);
        if (!now)
            return;
        FunctionColumn changed = *now;
        bool const points = changed.hasPoints() && changed.kind != FunctionColumn::VeloBased;
        Layer& owner = points ? changed.curve : changed.velocity;
        int const ia = owner.keyIndex(m_from), ib = owner.keyIndex(m_to);
        if (ia < 0 || ib < 0)
            return;
        Keyframe& a = owner.keys[ia];
        Keyframe& b = owner.keys[ib];
        if (a.curveOf(Driver::X) || b.curveOf(Driver::X)) {
            // Handles of their own: set from the curve's (each turned apart from the other side's, which stays as it was).
            double const span = std::max(1, b.frame - a.frame), way = b.pose.x - a.pose.x;
            double const dy = std::abs(way) > 1e-9 ? way : 1.0;
            ValueCurve ca = a.curveOf(Driver::X) ? *a.curveOf(Driver::X) : ValueCurve{Driver::X, {}, {}, false, ValueCurve::Curved};
            ValueCurve cb = b.curveOf(Driver::X) ? *b.curveOf(Driver::X) : ValueCurve{Driver::X, {}, {}, false, ValueCurve::Curved};
            ca.out = QPointF(leave.x() * span, leave.y() * dy);
            ca.after = ValueCurve::Curved;
            ca.corner = true;
            cb.in = QPointF((arrive.x() - 1.0) * span, (arrive.y() - 1.0) * dy);
            cb.corner = true;
            a.setCurve(ca);
            b.setCurve(cb);
        } else {
            a.easeOut = easeOut;
            a.liftOut = leave.y();
            b.easeIn = easeIn;
            b.liftIn = arrive.y() - 1.0;
        }
        int const which = m_channel->currentIndex();
        QString const text = !points ? tr("Apply velocity changed") : which == 1 ? tr("Apply velocity on X") : which == 2 ? tr("Apply velocity on Y") : tr("Apply velocity on Z");
        m_document->setFunction(changed, text);
        return;
    }
    Layer const& l = scene.layers[m_layer];
    Keyframe const a = l.keys[l.keyIndex(m_from)];
    Keyframe const b = l.keys[l.keyIndex(m_to)];
    m_document->beginMacro(tr("Apply Velocity Editor"));
    m_document->setKeyEase(m_layer, m_from, a.easeIn, easeOut, a.liftIn, leave.y());
    m_document->setKeyEase(m_layer, m_to, easeIn, b.easeOut, arrive.y() - 1.0, b.liftOut);
    m_document->endMacro();
}
