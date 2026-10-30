// Eclipse - rendering frames of the scene, several at once.
#include "canvas/FrameRender.h"

#include <QMutex>
#include <QPainter>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <cmath>

#include "canvas/Canvas.h"
#include "colour/images/ColourTransform.h"

namespace {

std::atomic<int> gThreads{0};

}  // namespace

namespace FrameRender {

void setThreads(int count)
{
    gThreads = std::clamp(count, 0, 64);
}

int threads()
{
    return gThreads;
}

int threadsUsed()
{
    int const set = gThreads;
    return set > 0 ? set : std::clamp(QThread::idealThreadCount(), 1, 16);
}

QSize sizeAt(Scene const& scene, double scale)
{
    return QSize(std::max(1, int(std::lround(scene.width * scale))), std::max(1, int(std::lround(scene.height * scale))));
}

QImage frame(Scene const& scene, int f, QSize size, bool quick, QColor const& under)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(under);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, !quick);
    painter.scale(double(size.width()) / std::max(1, scene.width), double(size.height()) / std::max(1, scene.height));
    Canvas::paintFrame(painter, scene, f);
    painter.end();
    return image;
}

QImage inSpace(Scene const& scene, QImage const& image, QString const& space, bool floats)
{
    if (space.isEmpty())
        return image;
    Cms::Transform const out = scene.outputTransform(space);
    return floats ? out.toFloat(image) : out.applied(image);
}

bool render(Scene const& scene, QVector<int> const& frames, QSize size, bool quick,
            std::function<QString(int, QImage const&)> const& keep, std::function<bool(int)> const& progress, QString* error, QColor const& under)
{
    int const count = std::min(threadsUsed(), int(frames.size()));
    if (count <= 1) {
        int done = 0;
        for (int f : frames) {
            QString const why = keep(f, frame(scene, f, size, quick, under));
            if (!why.isEmpty()) {
                if (error)
                    *error = why;
                return false;
            }
            ++done;
            if (progress && !progress(done))
                return false;
        }
        return true;
    }
    QThreadPool pool;
    pool.setMaxThreadCount(count);
    pool.setStackSize(8u * 1024u * 1024u);
    std::atomic<bool> stop{false};
    std::atomic<int> done{0};
    QMutex lock;
    QString first;
    for (int f : frames)
        pool.start([&, f] {
            if (stop)
                return;
            QString const why = keep(f, frame(scene, f, size, quick, under));
            if (!why.isEmpty()) {
                QMutexLocker const locked(&lock);
                if (first.isEmpty())
                    first = why;
                stop = true;
            }
            ++done;
        });
    bool stopped = false;
    while (!pool.waitForDone(30))
        if (progress && !stopped && !progress(done)) {
            stopped = true;
            stop = true;
        }
    if (progress && !stopped && !stop)
        progress(done);
    if (!first.isEmpty() && error)
        *error = first;
    return first.isEmpty() && !stopped;
}

}  // namespace FrameRender
