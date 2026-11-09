#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardStage.h"
#include "board/ui/BoardTimeline.h"
#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

void BoardWindow::updateCameraActions()
{
    auto const [shot, panel] = m_selected;
    auto const* current = m_document.panel(shot, panel);
    int const frame = m_timeline->frame() - m_timeline->panelStart(shot, panel);
    qsizetype const at = current ? BoardCamera::lowerKey(current->camera, frame) : 0;
    bool const keyed = current && at < current->camera.size() && current->camera[at].frame == frame;
    auto enable = [this](char const* id, bool on) {
        if (auto* action = m_actions->action(QLatin1String(id)))
            action->setEnabled(on);
    };
    for (auto id : {"BoardCameraTool", "BoardCameraPreview", "BoardCameraEdit", "BoardCameraAdd",
                    "BoardCameraStart", "BoardCameraEnd"})
        enable(id, current != nullptr);
    enable("BoardCameraRemove", keyed);
    enable("BoardCameraPrevious", current && at > 0);
    enable("BoardCameraNext", current && at + int(keyed) < current->camera.size());
}

void BoardWindow::showCameraGuide(bool show) { m_stage->showCameraGuide(show); }

void BoardWindow::showCameraPreview(bool show)
{
    m_cameraPreview = show;
    m_playback->stop();
    if (show)
        showPlaybackFrame(m_timeline->frame());
    else
        m_stage->hidePreview();
}

void BoardWindow::editCamera(QString const& operation)
{
    m_playback->stop();
    auto const [shot, panel] = m_selected;
    auto const* current = m_document.panel(shot, panel);
    if (!current)
        return;
    int const start = m_timeline->panelStart(shot, panel);
    int frame = std::clamp(m_timeline->frame() - start, 0, current->length - 1);
    if (operation == QLatin1String("previous") || operation == QLatin1String("next")) {
        auto const& keys = current->camera;
        qsizetype const at = BoardCamera::lowerKey(keys, frame);
        bool const previous = operation == QLatin1String("previous");
        qsizetype const index = previous ? at - 1 : at + int(at < keys.size() && keys[at].frame == frame);
        if (index >= 0 && index < keys.size())
            m_playback->seek(start + keys[index].frame);
        return;
    }
    if (operation == QLatin1String("remove")) {
        m_document.removeCameraKey(shot, panel, frame);
        return;
    }
    if (operation == QLatin1String("start"))
        frame = 0;
    else if (operation == QLatin1String("end"))
        frame = current->length - 1;
    auto pose = BoardCamera::poseAt(current->camera, frame);
    if (operation == QLatin1String("edit")) {
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("BoardCameraKeyDialog"));
        dialog.setWindowTitle(tr("Camera Key · Panel Frame %1").arg(frame + 1));
        auto* layout = new QFormLayout(&dialog);
        auto spin = [layout, &dialog](char const* id, QString const& label, double value, double min, double max) {
            auto* edit = new QDoubleSpinBox(&dialog);
            edit->setObjectName(QLatin1String(id));
            edit->setDecimals(2);
            edit->setRange(min, max);
            edit->setValue(value);
            layout->addRow(label, edit);
            return edit;
        };
        auto* x = spin("BoardCameraX", tr("Horizontal Offset (px)"), pose.offset.x(), -BoardCamera::offsetLimit, BoardCamera::offsetLimit);
        auto* y = spin("BoardCameraY", tr("Vertical Offset (px)"), pose.offset.y(), -BoardCamera::offsetLimit, BoardCamera::offsetLimit);
        auto* zoom = spin("BoardCameraZoom", tr("Zoom (%)"), pose.zoom * 100, BoardCamera::minZoom * 100, BoardCamera::maxZoom * 100);
        auto* angle = spin("BoardCameraRotation", tr("Rotation (°)"), pose.rotation, -BoardCamera::rotationLimit, BoardCamera::rotationLimit);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        pose = {{x->value(), y->value()}, zoom->value() / 100.0, angle->value()};
    }
    m_document.setCameraKey(shot, panel, frame, pose);
    if (operation == QLatin1String("start") || operation == QLatin1String("end"))
        m_playback->seek(start + frame);
}
