// Eclipse - the Timeline: the parameter rows of a layer, and typing a value into one.
#include "ui/timeline/Timeline.h"

#include <QKeyEvent>
#include <algorithm>
#include <QLineEdit>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include <QScrollBar>

QString Timeline::parameterName(Parameter which)
{
    switch (which) {
    case PosX: return tr("Position: Pos x");
    case PosY: return tr("Position: Pos y");
    case ScaleX: return tr("Scale: Scale x");
    case ScaleY: return tr("Scale: Scale y");
    case Angle: return tr("Rotation: Angle_z");
    case Opacity: return tr("Opacity");
    case PosZ: return tr("Position: Pos z");
    case Skew: return tr("Skew");
    case Fov: return tr("Field of view");
    default: return QString();
    }
}

static double& poseField(Pose& pose, Timeline::Parameter which)
{
    switch (which) {
    case Timeline::PosX: return pose.x;
    case Timeline::PosY: return pose.y;
    case Timeline::ScaleX: return pose.scaleX;
    case Timeline::ScaleY: return pose.scaleY;
    case Timeline::Angle: return pose.rotation;
    case Timeline::PosZ: return pose.z;
    case Timeline::Skew: return pose.skew;
    case Timeline::Fov: return pose.fov;
    default: return pose.opacity;
    }
}

double Timeline::parameterOf(Pose const& pose, Parameter which)
{
    Pose copy = pose;
    return poseField(copy, which);
}

void Timeline::setParameterOf(Pose& pose, Parameter which, double value)
{
    poseField(pose, which) = value;
}

bool Timeline::hasParameters(int layer) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return false;
    Layer::Kind const kind = scene.layers[layer].kind;
    return kind != Layer::Sound && kind != Layer::Group && kind != Layer::ColourCard;
}

bool Timeline::parametersShown(int layer) const
{
    return hasParameters(layer) && m_open.contains(m_document->scene().layers[layer].id);
}

void Timeline::setParametersShown(int layer, bool shown)
{
    if (!hasParameters(layer) || shown == parametersShown(layer))
        return;
    closeEntry(false);
    int const id = m_document->scene().layers[layer].id;
    if (shown)
        m_open.insert(id);
    else
        m_open.remove(id);
    // A block is counted in rows: it does not outlast them.
    clearRange();
    rebuildRows();
    updateScrollBars();
    viewport()->update();
}

QRect Timeline::parameterToggleRect(int layer) const
{
    int const row = rowOfLayer(layer);
    if (row < 0 || !hasParameters(layer))
        return QRect();
    return QRect(kHeaderWidth - kParamWidth - 17, kRulerHeight + rowTop(row) - verticalScrollBar()->value() + 3, 14, 14);
}

QRect Timeline::parameterRowRect(int layer, Parameter which) const
{
    int const row = rowOfLayer(layer);
    if (row < 0 || (!isControllerRow(which) && (!parametersShown(layer) || !parameterFor(m_document->scene().layers[layer].kind, which))))
        return QRect();
    for (int r = row + 1; r < m_rows.size() && m_rows[r] == layer && m_params[r] >= 0; ++r)
        if (m_params[r] == int(which))
            return QRect(0, kRulerHeight + rowTop(r) - verticalScrollBar()->value(), kHeaderWidth, rowHeight(r));
    return QRect();
}

// An effect whose amount is a share (shown in percent).
static bool inPercent(Effect const& e)
{
    return e.type == Effect::Tint || e.type == Effect::Transparency || e.type == Effect::HueSaturation || e.type == Effect::BrightnessContrast
           || e.type == Effect::Grain || e.type == Effect::RadialBlur || e.type == Effect::ColourScale || e.type == Effect::Negate
           || e.type == Effect::BlackWhite || e.type == Effect::Gradient;
}

QString Timeline::rowName(int layer, Parameter which) const
{
    if (isControllerRow(which)) {
        int const c = int(which) - kControllerRows;
        QVector<Scene::Controller> const& all = m_document->scene().controllers;
        return c < all.size() ? tr("Controller: %1").arg(all[c].name) : QString();
    }
    if (isBoneRow(which))
        return tr("Bone %1: Angle").arg(int(which) - kBoneRows + 1);
    if (isWarpRow(which)) {
        int const at = int(which) - kWarpRows;
        return (at % 2 == 0 ? tr("Deformer point %1: Offset x") : tr("Deformer point %1: Offset y")).arg(at / 2 + 1);
    }
    if (!isEffectRow(which))
        return parameterName(which);
    Scene const& scene = m_document->scene();
    int const e = int(which) - ParameterCount;
    if (layer < 0 || layer >= scene.layers.size() || e >= scene.layers[layer].effects.size())
        return QString();
    static char const* const kNames[Effect::TypeCount] = {QT_TR_NOOP("Blur"), QT_TR_NOOP("Tint"), QT_TR_NOOP("Cutter"), QT_TR_NOOP("Glow"),
                                                          QT_TR_NOOP("Shadow"), QT_TR_NOOP("Particles"), QT_TR_NOOP("Transparency"),
                                                          QT_TR_NOOP("Directional Blur"), QT_TR_NOOP("Hue-Saturation"), QT_TR_NOOP("Brightness-Contrast"),
                                                          QT_TR_NOOP("Grain"), QT_TR_NOOP("Radial Blur"), QT_TR_NOOP("Colour-Scale"),
                                                          QT_TR_NOOP("Negate"), QT_TR_NOOP("Median"), QT_TR_NOOP("Ripple"), QT_TR_NOOP("Tone"),
                                                          QT_TR_NOOP("Highlight"), QT_TR_NOOP("Bloom"), QT_TR_NOOP("Pixelate"), QT_TR_NOOP("Turbulence"),
                                                          QT_TR_NOOP("Matte Generator"), QT_TR_NOOP("Black & White"), QT_TR_NOOP("Matte Resize"),
                                                          QT_TR_NOOP("Matte Blur"), QT_TR_NOOP("Gradient"), QT_TR_NOOP("Channel Selector"),
                                                          QT_TR_NOOP("Crop"), QT_TR_NOOP("Remove Transparency"), QT_TR_NOOP("Matte Composite"),
                                                          QT_TR_NOOP("Blending"), QT_TR_NOOP("Variable Blur"), QT_TR_NOOP("Motion Blur"),
                                                          QT_TR_NOOP("Field Chart"), QT_TR_NOOP("Colour Curves"), QT_TR_NOOP("Focus"),
                                                          QT_TR_NOOP("Apply Peg Transformation"), QT_TR_NOOP("Scale Output"),
                                                          QT_TR_NOOP("Glue"), QT_TR_NOOP("Script"), QT_TR_NOOP("External")};
    Effect const& effect = scene.layers[layer].effects[e];
    return tr("Effect: %1").arg(tr(kNames[std::clamp(int(effect.type), 0, int(Effect::TypeCount) - 1)]));
}

QRect Timeline::parameterValueRect(int layer, Parameter which) const
{
    QRect const row = parameterRowRect(layer, which);
    if (row.isNull())
        return QRect();
    return QRect(kHeaderWidth - kParamWidth + 3, row.y() + 1, kParamWidth - 7, kRowHeight - 3);
}

int Timeline::drivenBy(int layer, Parameter which) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || int(which) < 0 || int(which) >= ParameterCount)
        return -1;
    // (The rows of the pose are numbered as the values a driver sets.)
    int by = -1;
    for (Driver const& d : scene.layers[layer].drivers)
        if (d.value == int(which))
            by = scene.layerIndex(d.source);
    return by;
}

double Timeline::parameter(int layer, Parameter which) const
{
    Scene const& scene = m_document->scene();
    if (isControllerRow(which)) {
        int const c = int(which) - kControllerRows;
        return c < scene.controllers.size() ? scene.controllers[c].valueAt(m_frame) : 0.0;
    }
    if (layer < 0 || layer >= scene.layers.size())
        return 0.0;
    if (drivenBy(layer, which) >= 0)
        return Driver::read(scene.poseOf(layer, m_frame), int(which));
    if (which == Fov)
        return scene.layers[layer].fovAt(m_frame);     // the angle it has, whether a keyframe says or the camera does
    Pose pose = scene.layers[layer].poseAt(m_frame);
    if (isEffectRow(which))
        return scene.layers[layer].effectAmount(int(which) - ParameterCount, pose);
    if (isBoneRow(which)) {
        int const b = int(which) - kBoneRows;
        return b < pose.bend.size() ? pose.bend[b] : 0.0;
    }
    if (isWarpRow(which)) {
        int const at = int(which) - kWarpRows, entry = 3 * (at / 2);
        QPointF const moved = entry < pose.warp.size() ? pose.warp[entry] : QPointF();
        return at % 2 == 0 ? moved.x() : moved.y();
    }
    return poseField(pose, which);
}

bool Timeline::parameterFor(int layerKind, Parameter which)
{
    return which != Fov || layerKind == Layer::Camera;
}

// Whether the row of `which` shows a share in percent.
static bool percentRow(Scene const& scene, int layer, Timeline::Parameter which)
{
    if (which == Timeline::Opacity)
        return true;
    int const e = int(which) - Timeline::ParameterCount;
    return e >= 0 && layer >= 0 && layer < scene.layers.size() && e < scene.layers[layer].effects.size() && inPercent(scene.layers[layer].effects[e]);
}

bool Timeline::rowInPercent(int layer, Parameter which) const
{
    return percentRow(m_document->scene(), layer, which);
}

QString Timeline::parameterText(int layer, Parameter which) const
{
    if (isControllerRow(which)) {
        // A slider: where it stands (0: the first state, 1 the second ...); a 2D point: where the point is on its pad.
        int const c = int(which) - kControllerRows;
        QVector<Scene::Controller> const& all = m_document->scene().controllers;
        if (c >= all.size())
            return QString();
        if (all[c].point) {
            QPointF const at = all[c].whereAt(m_frame);
            return QStringLiteral("%1, %2").arg(at.x(), 0, 'f', 2).arg(at.y(), 0, 'f', 2);
        }
        return QString::number(all[c].valueAt(m_frame), 'f', 2);
    }
    double const value = parameter(layer, which);
    // Opacity as a percentage; the rest to three places, without noughts at the end.
    QString text = QString::number(percentRow(m_document->scene(), layer, which) ? value * 100.0 : value, 'f', 3);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

void Timeline::setParameter(int layer, Parameter which, double value)
{
    Scene const& scene = m_document->scene();
    if (isControllerRow(which)) {
        // The controller set here: its keyframe, and its layers posed between its states.
        int const c = int(which) - kControllerRows;
        if (c < scene.controllers.size() && !scene.controllers[c].point)
            m_document->applyController(c, m_frame, value);
        return;
    }
    if (!hasParameters(layer))
        return;
    if (which == Opacity)
        value = std::clamp(value, 0.0, 1.0);
    if (isWarpRow(which)) {
        // A keyframe of the deformer here, that point moved so far (the rest of the pose goes on from its own keyframes).
        int const at = int(which) - kWarpRows, entry = 3 * (at / 2);
        if (!scene.layers[layer].hasWarp() || entry >= scene.layers[layer].warp.size())
            return;
        Pose pose = scene.layers[layer].poseAt(m_frame);
        if (pose.warp.size() < scene.layers[layer].warp.size())
            pose.warp.resize(scene.layers[layer].warp.size());
        QPointF moved = pose.warp[entry];
        (at % 2 == 0 ? moved.rx() : moved.ry()) = value;
        int const key = scene.layers[layer].keyIndex(m_frame);
        if (moved == pose.warp[entry] && key >= 0 && scene.layers[layer].keys[key].sets(Keyframe::PartRest))
            return;
        pose.warp[entry] = moved;
        m_document->setPartKeyframe(layer, m_frame, Keyframe::PartRest, pose, tr("Set %1").arg(rowName(layer, which)));
        return;
    }
    if (isBoneRow(which)) {
        // A keyframe of the bones' angles here (the rest of the pose goes on from its own keyframes).
        int const b = int(which) - kBoneRows;
        if (b + 1 >= scene.layers[layer].bones.size())
            return;
        Pose pose = scene.layers[layer].poseAt(m_frame);
        if (pose.bend.size() <= b)
            pose.bend.resize(b + 1);
        int const key = scene.layers[layer].keyIndex(m_frame);
        if (pose.bend[b] == value && key >= 0 && scene.layers[layer].keys[key].sets(Keyframe::PartRest))
            return;
        pose.bend[b] = value;
        m_document->setPartKeyframe(layer, m_frame, Keyframe::PartRest, pose, tr("Set %1").arg(rowName(layer, which)));
        return;
    }
    if (isEffectRow(which)) {
        // An animated effect: a keyframe of its amount here; otherwise its one amount.
        int const e = int(which) - ParameterCount;
        if (e >= scene.layers[layer].effects.size())
            return;
        Effect const& effect = scene.layers[layer].effects[e];
        value = std::max(0.0, inPercent(effect) ? std::min(value, 1.0) : value);
        if (effect.animated) {
            m_document->setEffectAmount(layer, m_frame, e, value);
        } else if (effect.amount != value) {
            QVector<Effect> effects = scene.layers[layer].effects;
            effects[e].amount = value;
            m_document->setEffects(layer, effects, tr("Change Effect"));
        }
        return;
    }
    if (which == Fov) {
        if (scene.layers[layer].kind == Layer::Camera)
            m_document->setFov(layer, value, m_frame);
        return;
    }
    Pose pose = scene.layers[layer].poseAt(m_frame);
    int const key = scene.layers[layer].keyIndex(m_frame);
    if (poseField(pose, which) == value && key >= 0 && scene.layers[layer].keys[key].sets(1 << int(which)))
        return;
    poseField(pose, which) = value;
    // A keyframe of this parameter alone: the others keep going from their own keyframes.
    m_document->setPartKeyframe(layer, m_frame, 1 << int(which), pose, tr("Set %1").arg(parameterName(which)));
}

void Timeline::editParameter(int layer, Parameter which)
{
    closeEntry(true);
    if (isControllerRow(which)) {
        int const c = int(which) - kControllerRows;
        if (c >= m_document->scene().controllers.size() || m_document->scene().controllers[c].point)
            return;     // a 2D point is set on its pad, in the Pose Controllers view
    }
    if (drivenBy(layer, which) >= 0)
        return;     // it follows another layer: nothing to type in (Layer Properties > Attribute Controls)
    QRect const box = parameterValueRect(layer, which);
    if (box.isNull())
        return;
    m_entryLayer = layer;
    m_entryParam = which;
    m_entry = new QLineEdit(parameterText(layer, which), viewport());
    m_entry->setObjectName(QStringLiteral("ParameterEntry"));
    m_entry->setFrame(false);
    m_entry->setGeometry(box);
    m_entry->selectAll();
    m_entry->show();
    m_entry->setFocus();
    m_entry->installEventFilter(this);
    connect(m_entry, &QLineEdit::returnPressed, this, [this] { closeEntry(true); });
}

void Timeline::closeEntry(bool take)
{
    if (!m_entry)
        return;
    QLineEdit* const entry = m_entry;
    m_entry = nullptr;
    entry->setObjectName(QString());       // gone for whoever looks for it by name
    bool ok = false;
    double value = entry->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    entry->hide();
    entry->deleteLater();
    if (take && ok)
        setParameter(m_entryLayer, m_entryParam, percentRow(m_document->scene(), m_entryLayer, m_entryParam) ? value / 100.0 : value);
    viewport()->update();
}

bool Timeline::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_entry && m_entry) {
        // Keys typed in the box are its own: not shortcuts of the program.
        if (event->type() == QEvent::ShortcutOverride) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            closeEntry(false);
            return true;
        }
        if (event->type() == QEvent::FocusOut) {
            closeEntry(true);
            return false;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}
