// Eclipse - the main window: keyframe and transform commands (position keyframes, flip scale, stepping along paths, keyframes over a selection, Capture Motion).
#include "ui/window/MainWindow.h"
#include "ui/window/MainWindowPrivate.h"

#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

void MainWindow::onActionInsertPositionKeyframe()
{
    // A keyframe of where the layer is (X, Y and Z) only: its other values go on as they were.
    int const l = layer();
    if (l < 0 || l >= m_document->scene().layers.size())
        return;
    m_document->setPartKeyframe(l, frame(), Keyframe::PartX | Keyframe::PartY | Keyframe::PartZ, m_document->scene().layers[l].poseAt(frame()),
                                tr("Insert Position Keyframe"));
}

void MainWindow::onActionFlipScale(QString const& axis)
{
    // The scale turned inside out: on this frame (a keyframe), or on every frame with Animate Off.
    QVector<int> layers{m_canvas->animationTarget(layer())};
    layers += m_canvas->transformLayers();
    QString const text = axis == QLatin1String("y") ? tr("Flip Scale Vertical") : tr("Flip Scale Horizontal");
    m_document->beginMacro(text);
    for (int l : std::as_const(layers)) {
        if (l < 0 || l >= m_document->scene().layers.size())
            continue;
        Pose const was = m_document->scene().layers[l].poseAt(frame());
        Pose now = was;
        (axis == QLatin1String("y") ? now.scaleY : now.scaleX) *= -1.0;
        if (m_canvas->animatesCurrentFrame())
            m_document->setKeyframe(l, frame(), now, text);
        else
            m_document->shiftPoses(l, frame(), was, now, text);
    }
    m_document->endMacro();
}

void MainWindow::onActionStopMotionMode(bool on)
{
    // New keyframes are stop-motion ones: the layer holds, then jumps.
    m_document->setStopMotionKeys(on);
    settings().setValue(QStringLiteral("prefs/stopMotionKeys"), on);
    statusBar()->showMessage(on ? tr("Stop-Motion Keyframe Mode: new keyframes hold until the next one.") : tr("New keyframes are motion keyframes."), 4000);
}

void MainWindow::onActionSnapKeyframe(bool on)
{
    Canvas::TransformOptions o = m_canvas->transformOptions();
    o.snapKeyframe = on;
    m_canvas->setTransformOptions(o);
    settings().setValue(QStringLiteral("prefs/snapKeyframe"), on);
}

void MainWindow::onActionToggleLineCurve()
{
    // The path through the keyframe (the one the control point picked belongs to, or the one on this frame or before): straight, or curved.
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l < 0 || l >= scene.layers.size() || scene.layers[l].keys.isEmpty())
        return;
    Layer const& layer = scene.layers[l];
    int key = m_canvas->pickedControlKey();
    if (key < 0 || key >= layer.keys.size()) {
        int const before = layer.previousKeyFrame(frame() + 1);
        key = before >= 0 ? layer.keyIndex(before) : 0;
    }
    if (key < 0)
        return;
    bool const line = layer.keys[key].tension < 0.5;
    m_document->editLayers({l}, line ? tr("Set Path to Line") : tr("Set Path to Curve"), [key, line](Layer& on) {
        if (key < on.keys.size())
            on.keys[key].tension = line ? 1.0 : 0.0;
    });
    statusBar()->showMessage(line ? tr("The path runs straight through that keyframe.") : tr("The path curves through that keyframe."), 4000);
}

void MainWindow::onActionSelectPathPoint(QString const& which)
{
    if (which == QLatin1String("nearest")) {
        if (!m_canvas->pickNearestControlPoint())
            statusBar()->showMessage(tr("The current layer's path has no control points (Insert > Control Point)."), 5000);
        return;
    }
    bool const controlsOnly = which.endsWith(QLatin1String("Control"));
    int const direction = which.startsWith(QLatin1String("previous")) ? -1 : 1;
    int at = -1;
    // (Only control points: step on until one is picked.)
    for (int guard = 0; guard < 1000; ++guard) {
        int f = -1;
        if (!m_canvas->stepPathPoint(direction, &f)) {
            if (at < 0)
                statusBar()->showMessage(tr("No more keyframes or control points that way."), 3000);
            break;
        }
        at = f;
        m_timeline->setCurrentFrame(f);
        if (!controlsOnly || m_canvas->pickedControlIndex() >= 0)
            break;
    }
}

void MainWindow::onActionSelectionKeyframes(QString const& what)
{
    // The cells selected (in the Timeline or the Xsheet): a keyframe on each of their frames, or none.
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty())
        layers = {layer()};
    int const first = m_timeline->hasRange() ? m_timeline->selectionFirstFrame() : frame();
    int const last = m_timeline->hasRange() ? m_timeline->selectionLastFrame() : frame();
    bool const set = what == QLatin1String("set");
    bool const stop = m_document->stopMotionKeys();
    m_document->editScene(set ? tr("Set Key Frames") : tr("Clear Key Frames"), [&](Scene& s) {
        for (int l : std::as_const(layers)) {
            if (l < 0 || l >= s.layers.size())
                continue;
            Layer& on = s.layers[l];
            if (set) {
                QVector<Pose> poses;
                for (int f = first; f <= last; ++f)
                    poses.push_back(on.poseAt(f));
                for (int f = first; f <= last; ++f) {
                    bool const made = on.keyIndex(f) < 0;
                    on.setKey(f, poses[f - first]);
                    if (made && stop)
                        on.keys[on.keyIndex(f)].tween = false;
                }
            } else {
                for (int k = int(on.keys.size()) - 1; k >= 0; --k)
                    if (on.keys[k].frame >= first && on.keys[k].frame <= last)
                        on.keys.removeAt(k);
            }
        }
    });
}

void MainWindow::onActionCaptureMotion()
{
    // The pointer's moves over the Camera view, while the frames go by, become the position of the layer (or its peg), one keyframe a frame.
    int const target = m_canvas->animationTarget(layer());
    if (target < 0 || target >= m_document->scene().layers.size()) {
        statusBar()->showMessage(tr("Pick a layer or a peg to capture motion for."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CaptureMotionDialog"));
    dialog.setWindowTitle(tr("Capture Motion"));
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(new QLabel(tr("Record the pointer's movement over the Camera view and apply it to the X and Y position of %1.")
                                .arg(m_document->scene().layers[target].name), &dialog));
    QSpinBox* const from = new QSpinBox(&dialog);
    from->setObjectName(QStringLiteral("CaptureFrom"));
    from->setRange(1, m_document->scene().frameCount());
    from->setValue(frame() + 1);
    QSpinBox* const to = new QSpinBox(&dialog);
    to->setObjectName(QStringLiteral("CaptureTo"));
    to->setRange(1, m_document->scene().frameCount());
    to->setValue(m_document->scene().frameCount());
    QSpinBox* const countdown = new QSpinBox(&dialog);
    countdown->setObjectName(QStringLiteral("CaptureCountdown"));
    countdown->setRange(0, 10);
    countdown->setValue(3);
    countdown->setSuffix(tr(" s"));
    QSpinBox* const rate = new QSpinBox(&dialog);
    rate->setObjectName(QStringLiteral("CaptureRate"));
    rate->setRange(1, 120);
    rate->setValue(int(std::round(m_document->scene().fps)));
    rate->setSuffix(tr(" fps"));
    form->addRow(tr("From frame"), from);
    form->addRow(tr("To frame"), to);
    form->addRow(tr("Countdown"), countdown);
    form->addRow(tr("Frame rate"), rate);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->addButton(tr("Start"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    startCaptureMotion(target, from->value() - 1, std::max(from->value(), to->value()) - 1, countdown->value(), rate->value());
}

void MainWindow::startCaptureMotion(int target, int first, int last, int countdown, int fps)
{
    struct Capture {
        int target, frame, last;
        QPointF start;
        Pose base;
        QVector<QPair<int, Pose>> poses;
    };
    auto* const capture = new Capture{target, first, last, QPointF(), m_document->scene().layers[target].poseAt(first), {}};
    auto* const timer = new QTimer(this);
    timer->setInterval(1000 / std::max(1, fps));
    connect(timer, &QTimer::timeout, this, [this, capture, timer] {
        Scene const& scene = m_document->scene();
        if (capture->target >= scene.layers.size()) {
            timer->stop();
            timer->deleteLater();
            delete capture;
            return;
        }
        // Where the pointer is, in what the layer hangs on.
        int const parent = scene.parentIndex(capture->target);
        QTransform const up = parent >= 0 ? scene.worldTransform(parent, capture->frame) : QTransform();
        QPointF const sceneAt = m_canvas->sceneAtWidget(m_canvas->mapFromGlobal(QCursor::pos()));
        QPointF const at = up.isInvertible() ? up.inverted().map(sceneAt) : sceneAt;
        if (capture->poses.isEmpty())
            capture->start = at;
        Pose pose = scene.layers[capture->target].poseAt(capture->frame);
        pose.x = capture->base.x + at.x() - capture->start.x();
        pose.y = capture->base.y + at.y() - capture->start.y();
        capture->poses.push_back({capture->frame, pose});
        m_timeline->setCurrentFrame(capture->frame);
        if (++capture->frame > capture->last) {
            timer->stop();
            timer->deleteLater();
            QVector<QPair<int, Pose>> const poses = capture->poses;
            int const target = capture->target;
            delete capture;
            m_document->editScene(tr("Capture Motion"), [&](Scene& s) {
                for (auto const& [f, p] : poses)
                    s.layers[target].setKey(f, p);
            });
            statusBar()->showMessage(tr("Motion captured: %n keyframe(s).", nullptr, int(poses.size())), 5000);
        }
    });
    statusBar()->showMessage(tr("Capture Motion starts in %n second(s): put the pointer over the Camera view.", nullptr, countdown), countdown * 1000 + 500);
    QTimer::singleShot(countdown * 1000, timer, qOverload<>(&QTimer::start));
}
