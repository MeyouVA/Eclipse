// Eclipse - the Timeline: the parameter rows of a layer, and typing a value into one.
#include "ui/timeline/Timeline.h"

#include <QKeyEvent>
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
    return QRect(kHeaderWidth - kParamWidth - 17, kRulerHeight + row * kRowHeight - verticalScrollBar()->value() + 3, 14, 14);
}

QRect Timeline::parameterRowRect(int layer, Parameter which) const
{
    int const row = rowOfLayer(layer);
    if (row < 0 || !parametersShown(layer) || !parameterFor(m_document->scene().layers[layer].kind, which))
        return QRect();
    return QRect(0, kRulerHeight + (row + 1 + int(which)) * kRowHeight - verticalScrollBar()->value(), kHeaderWidth, kRowHeight);
}

QRect Timeline::parameterValueRect(int layer, Parameter which) const
{
    QRect const row = parameterRowRect(layer, which);
    if (row.isNull())
        return QRect();
    return QRect(kHeaderWidth - kParamWidth + 3, row.y() + 1, kParamWidth - 7, kRowHeight - 3);
}

double Timeline::parameter(int layer, Parameter which) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size())
        return 0.0;
    if (which == Fov)
        return scene.layers[layer].fovAt(m_frame);     // the angle it has, whether a keyframe says or the camera does
    Pose pose = scene.layers[layer].poseAt(m_frame);
    return poseField(pose, which);
}

bool Timeline::parameterFor(int layerKind, Parameter which)
{
    return which != Fov || layerKind == Layer::Camera;
}

QString Timeline::parameterText(int layer, Parameter which) const
{
    double const value = parameter(layer, which);
    // Opacity as a percentage; the rest to three places, without noughts at the end.
    QString text = QString::number(which == Opacity ? value * 100.0 : value, 'f', 3);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

void Timeline::setParameter(int layer, Parameter which, double value)
{
    Scene const& scene = m_document->scene();
    if (!hasParameters(layer))
        return;
    if (which == Opacity)
        value = std::clamp(value, 0.0, 1.0);
    if (which == Fov) {
        if (scene.layers[layer].kind == Layer::Camera)
            m_document->setFov(layer, value, m_frame);
        return;
    }
    Pose pose = scene.layers[layer].poseAt(m_frame);
    if (poseField(pose, which) == value && scene.layers[layer].keyIndex(m_frame) >= 0)
        return;
    poseField(pose, which) = value;
    m_document->setKeyframe(layer, m_frame, pose, tr("Set %1").arg(parameterName(which)));
}

void Timeline::editParameter(int layer, Parameter which)
{
    closeEntry(true);
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
        setParameter(m_entryLayer, m_entryParam, m_entryParam == Opacity ? value / 100.0 : value);
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
