// Eclipse - the main window: playback and going from frame to frame.
#include "ui/window/MainWindow.h"
#include "ui/views/PreviewManagerView.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QSlider>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "render/images/ImageFiles.h"
#include "ui/views/DepthView.h"
#include "ui/views/PerspectiveView.h"
#include "ui/io/MovieExport.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/LipSyncView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QSpinBox>

void MainWindow::startPlayingFrom(int from)
{
    m_playFrom = from;
    m_playClock.restart();
    if (m_soundOn && !m_playMix.isEmpty())
        m_player.play(m_playMix, qint64(from / m_document->scene().fps * SoundData::kRate));
}

// Play > Render and Play: every frame is rendered once and kept, then the scene plays from those.
void MainWindow::onActionRenderAndPlay()
{
    if (m_playTimer.isActive())
        stop();
    PreviewManagerView* const preview = findChild<PreviewManagerView*>();
    if (preview && preview->render() >= 0 && m_canvas->previewFrames() > 0)
        onActionTogglePlayback();
}

void MainWindow::onActionTogglePlayback()
{
    if (m_playTimer.isActive()) {
        stop();
        return;
    }
    if (frame() >= playStop() || frame() < m_playStart)
        m_timeline->setCurrentFrame(m_playStart);
    // Preroll: from so many frames before (not before where playing begins).
    if (m_preroll > 0)
        m_timeline->setCurrentFrame(std::max(m_playStart, frame() - m_preroll));
    m_canvas->setPlaying(true);
    m_actions->setChecked(QStringLiteral("PlayToggle"), true);
    // Playing from the Preview Manager's frames: the sound kept with them; otherwise the scene's, mixed now.
    PreviewManagerView* const preview = m_canvas->previewFrames() > 0 ? findChild<PreviewManagerView*>() : nullptr;
    m_playMix = !m_soundOn || m_playSpeed != 100 ? QVector<qint16>() : preview ? preview->sound() : Sound::mix(m_document->scene());
    startPlayingFrom(frame());
    // Look at the clock a few times per frame.
    m_playTimer.start(std::max(1, int(250.0 / m_document->scene().fps)));
}

void MainWindow::onActionEnablePlayback(QString const& which)
{
    char const* const action = which == QLatin1String("top") ? "PlayInTop" : which == QLatin1String("side") ? "PlayInSide" : "PlayInPerspective";
    if (m_playViews.contains(which))
        m_playViews.removeAll(which);
    else
        m_playViews.push_back(which);
    m_actions->setChecked(QLatin1String(action), m_playViews.contains(which));
    settings().setValue(QStringLiteral("play/views"), m_playViews);
    showFrameInStillViews();
}

// The views that stood still while playing show the frame the scene is on.
void MainWindow::showFrameInStillViews()
{
    for (DepthView* const view : findChildren<DepthView*>())
        view->setCurrentCell(layer(), frame());
    for (PerspectiveView* const view : findChildren<PerspectiveView*>())
        view->setCurrentCell(layer(), frame());
}

void MainWindow::stop()
{
    m_playTimer.stop();
    m_player.stop();
    m_playMix.clear();
    m_canvas->setPlaying(false);
    m_actions->setChecked(QStringLiteral("PlayToggle"), false);
    showFrameInStillViews();
    if (m_mini) {
        // Mini Playback is over: the frames playing runs between are as they were, and so is the frame.
        m_mini = false;
        m_loop = m_miniLoop;
        setPlayRange(m_miniStart, m_miniStop);
        m_timeline->setCurrentFrame(std::min(m_miniFrame, m_document->scene().frameCount() - 1));
    }
}

QString MainWindow::recordForPlay(QString* error)
{
    stop();
    Scene const scene = m_document->scene();
    QString const folder = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("Eclipse"));
    QDir().mkpath(folder);
    QString const path = QDir(folder).filePath(QStringLiteral("record_and_play.mp4"));
    QFile::remove(path);
    MovieExport::Options options;
    options.first = m_playStart;
    options.last = playStop();
    options.scale = 0.5;        // quick to make and to play: it is for looking at the movement
    options.quality = 23;
    QProgressDialog progress(tr("Recording..."), tr("Stop"), 0, scene.frameCount(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QString why;
    bool const ok = MovieExport::write(scene, path, [&progress](int f) {
        progress.setValue(f);
        QApplication::processEvents();
        return !progress.wasCanceled();
    }, &why, options);
    progress.setValue(scene.frameCount());
    if (!ok && error && !progress.wasCanceled())
        *error = why;
    return ok ? path : QString();
}

void MainWindow::onActionRecordAndPlay()
{
    QString error;
    QString const movie = recordForPlay(&error);
    if (movie.isEmpty()) {
        if (!error.isEmpty())
            QMessageBox::critical(this, tr("Eclipse"), tr("The scene could not be recorded.\n%1").arg(error));
        return;
    }
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(movie)))
        statusBar()->showMessage(tr("Recorded %1").arg(QDir::toNativeSeparators(movie)), 6000);
    else
        QMessageBox::information(this, tr("Eclipse"), tr("The recording is in\n%1\nbut no program could be found to play it.").arg(QDir::toNativeSeparators(movie)));
}

void MainWindow::onActionMiniPlayback()
{
    if (m_playTimer.isActive()) {
        stop();
        return;
    }
    int const reach = std::clamp(settings().value(QStringLiteral("play/miniReach"), 5).toInt(), 1, 100);
    int const last = m_document->scene().frameCount() - 1;
    m_miniFrame = frame();
    m_miniStart = m_playStart;
    m_miniStop = m_playStop;
    m_miniLoop = m_loop;
    setPlayRange(std::max(0, m_miniFrame - reach), std::min(last, m_miniFrame + reach));
    m_loop = true;
    m_timeline->setCurrentFrame(m_playStart);
    m_mini = true;
    onActionTogglePlayback();
}

void MainWindow::onActionMiniPlaybackRange()
{
    bool ok = false;
    int const reach = QInputDialog::getInt(this, tr("Mini Playback Range"), tr("How many frames before and after the current one Mini Playback plays:"),
                                           std::clamp(settings().value(QStringLiteral("play/miniReach"), 5).toInt(), 1, 100), 1, 100, 1, &ok);
    if (ok)
        settings().setValue(QStringLiteral("play/miniReach"), reach);
}

void MainWindow::advanceFrame()
{
    Scene const& scene = m_document->scene();
    int const last = playStop();
    int const due = m_playFrom + int(m_playClock.elapsed() * scene.fps * m_playSpeed / 100.0 / 1000.0);
    if (due <= last) {
        if (due != frame())
            m_timeline->setCurrentFrame(due);
    } else if (m_loop) {
        // The last frame held a moment before it goes round again, when the preferences say so.
        double const lastEnds = (last - m_playFrom + 1) * 1000.0 / std::max(scene.fps * m_playSpeed / 100.0, 1e-6);
        if (m_loopPause > 0 && m_playClock.elapsed() < lastEnds + m_loopPause) {
            if (frame() != last)
                m_timeline->setCurrentFrame(last);
            return;
        }
        m_timeline->setCurrentFrame(m_playStart);
        startPlayingFrom(m_playStart);
    } else {
        m_timeline->setCurrentFrame(last);
        stop();
    }
}

void MainWindow::setPlaybackSpeed(int percent)
{
    percent = std::clamp(percent, 5, 800);
    if (percent == m_playSpeed)
        return;
    bool const playing = m_playTimer.isActive();
    if (playing)
        stop();
    m_playSpeed = percent;
    if (playing)
        onActionTogglePlayback();
}

void MainWindow::onActionPlaybackSpeed()
{
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Playback Speed"), tr("Speed, as a percentage of the scene's frame rate\n(the sound plays only at 100):"),
                                             m_playSpeed, 5, 800, 5, &ok);
    if (ok)
        setPlaybackSpeed(percent);
}

int MainWindow::playStop() const
{
    int const last = m_document->scene().frameCount() - 1;
    return m_playStop < 0 ? last : std::min(m_playStop, last);
}

void MainWindow::setPlayRange(int start, int stop)
{
    int const last = m_document->scene().frameCount() - 1;
    start = std::clamp(start, 0, last);
    stop = stop < 0 || stop >= last ? -1 : std::max(stop, start);
    m_playStart = start;
    m_playStop = stop;
    QSignalBlocker const a(m_startBox), b(m_stopBox);
    m_startBox->setValue(m_playStart + 1);
    m_stopBox->setValue(playStop() + 1);
    m_timeline->setPlayRange(m_playStart, m_playStop);
}

void MainWindow::onActionPlaybackStartFrame()
{
    bool ok = false;
    int const f = QInputDialog::getInt(this, tr("Playback Start Frame"), tr("Start playing at frame:"), m_playStart + 1, 1, playStop() + 1, 1, &ok);
    if (ok)
        setPlayRange(f - 1, m_playStop);
}

void MainWindow::onActionPlaybackStopFrame()
{
    bool ok = false;
    int const f = QInputDialog::getInt(this, tr("Playback Stop Frame"), tr("Stop playing at frame:"), playStop() + 1, m_playStart + 1,
                                       m_document->scene().frameCount(), 1, &ok);
    if (ok)
        setPlayRange(m_playStart, f - 1);
}

void MainWindow::onActionFirstFrame()
{
    m_timeline->setCurrentFrame(0);
}

void MainWindow::onActionLastFrame()
{
    m_timeline->setCurrentFrame(m_document->scene().frameCount() - 1);
}

void MainWindow::onActionGotoFrame()
{
    bool ok = false;
    int const target = QInputDialog::getInt(this, tr("Go to Frame"), tr("Frame:"), frame() + 1, 1, m_document->scene().frameCount(), 1, &ok);
    if (ok)
        m_timeline->setCurrentFrame(target - 1);
}

void MainWindow::stepFrame(int delta)
{
    m_timeline->setCurrentFrame(frame() + delta);
}

void MainWindow::stepDrawing(int direction)
{
    Layer const& l = m_document->scene().layers[layer()];
    int const target = direction < 0 ? l.previousDrawingFrame(frame()) : l.nextDrawingFrame(frame());
    if (target >= 0)
        m_timeline->setCurrentFrame(target);
}

void MainWindow::shuttleStep()
{
    Scene const& scene = m_document->scene();
    if (!m_shuttle || scene.frameCount() < 1)
        return;
    if (int(std::lround(m_shuttleAt)) != frame())
        m_shuttleAt = frame();      // the frame was changed some other way meanwhile
    // Up to four times the scene's speed at the end of its travel, gently near the middle.
    double const pull = m_shuttle->value() / 100.0;
    double const framesPerSecond = pull * std::abs(pull) * 4.0 * scene.fps;
    m_shuttleAt = std::clamp(m_shuttleAt + framesPerSecond * 0.04, 0.0, double(scene.frameCount() - 1));
    int const to = int(std::lround(m_shuttleAt));
    if (to != frame())
        m_timeline->setCurrentFrame(to);
}

// ---- Scene > Render: the preview of the Preview Manager -------------------------------------

void MainWindow::onActionOpenPreviewManager()
{
    QDockWidget* const dock = dockOf("PreviewManagerDock");
    if (!dock)
        return;
    showView(dock);
    dock->raise();
}

void MainWindow::onActionRenderPreview()
{
    if (PreviewManagerView* const preview = findChild<PreviewManagerView*>()) {
        int const made = preview->render();
        if (made >= 0)
            statusBar()->showMessage(tr("Preview rendered: %n frame(s) drawn again, the rest unchanged.", nullptr, made), 5000);
    }
}

void MainWindow::onActionAutoRender(bool on)
{
    if (PreviewManagerView* const preview = findChild<PreviewManagerView*>())
        preview->setAutoRender(on);
}

void MainWindow::onActionRecalculateAll()
{
    if (PreviewManagerView* const preview = findChild<PreviewManagerView*>())
        preview->render(true);
}

void MainWindow::onActionCancelPreviewRender()
{
    if (PreviewManagerView* const preview = findChild<PreviewManagerView*>())
        preview->cancel();
}

// ---- View: the Render View, and the Camera view's time limits --------------------------------------

void MainWindow::onActionCameraViewMode(QString const& mode)
{
    // (Through the view mode box at the foot of the Camera view, so the two agree.)
    Canvas::ViewMode const wanted = mode == QLatin1String("render") ? Canvas::ViewRender : Canvas::ViewNormal;
    if (QComboBox* const box = findChild<QComboBox*>(QStringLiteral("ViewMode")))
        box->setCurrentIndex(int(wanted));
    else
        m_canvas->setViewMode(wanted);
    m_actions->setChecked(QStringLiteral("ViewNormalMode"), m_canvas->viewMode() == Canvas::ViewNormal);
    m_actions->setChecked(QStringLiteral("ViewRenderMode"), m_canvas->viewMode() == Canvas::ViewRender);
}

void MainWindow::onActionRenderCurrentFrame()
{
    if (m_canvas->viewMode() != Canvas::ViewRender)
        onActionCameraViewMode(QStringLiteral("render"));
    m_canvas->renderCurrentFrame();
    m_canvas->repaint();
    statusBar()->showMessage(tr("Rendered frame %1").arg(frame() + 1), 3000);
}

void MainWindow::onActionViewAutoRender(bool on)
{
    m_canvas->setAutoRender(on);
    m_actions->setChecked(QStringLiteral("ViewAutoRender"), on);
    settings().setValue(QStringLiteral("view/autoRender"), on);
}

void MainWindow::onActionProgressiveRender(bool on)
{
    m_canvas->setProgressiveRender(on);
    m_actions->setChecked(QStringLiteral("ProgressiveRender"), on);
    settings().setValue(QStringLiteral("view/progressiveRender"), on);
}

void MainWindow::onActionViewTimeoutOff(bool on)
{
    m_canvas->setViewTimeoutOff(on);
    m_actions->setChecked(QStringLiteral("ViewTimeoutOff"), on);
    statusBar()->showMessage(on ? tr("View Timeout Disabled") : tr("View Timeout Enabled"), 4000);
}

void MainWindow::onActionAutoRenderWrite(bool on)
{
    m_autoRenderWrite = on;
    m_actions->setChecked(QStringLiteral("AutoRenderWrite"), on);
    if (on && std::none_of(m_document->scene().writes.cbegin(), m_document->scene().writes.cend(), [](Scene::Write const& w) { return w.enabled && !w.movie; }))
        statusBar()->showMessage(tr("Auto Render Write is on, but no write output of images is on: add one with Scene > Write Outputs."), 7000);
}

void MainWindow::writeRenderedFrame(int at, QImage const& image)
{
    if (!m_autoRenderWrite || image.isNull())
        return;
    QStringList problems;
    int made = 0;
    for (Scene::Write const& w : m_document->scene().writes) {
        if (!w.enabled || w.movie || w.path.trimmed().isEmpty())
            continue;
        QString const folder = writeFolder(w.path);
        QDir().mkpath(folder);
        QString const name = w.name.trimmed().isEmpty() ? QStringLiteral("frame") : w.name.trimmed();
        QString const file = QDir(folder).filePath(QStringLiteral("%1_%2.%3").arg(name).arg(at + 1, 4, 10, QLatin1Char('0')).arg(w.format));
        QImage const sized = w.percent == 100 ? image : image.scaled(image.size() * (w.percent / 100.0), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QString why;
        if (ImageFiles::save(sized, file, w.format, &why))
            ++made;
        else
            problems << why;
    }
    if (!problems.isEmpty())
        statusBar()->showMessage(problems.first(), 7000);
    else if (made > 0)
        statusBar()->showMessage(tr("Rendered frame %1, written by %n write output(s).", nullptr, made).arg(at + 1), 3000);
}
