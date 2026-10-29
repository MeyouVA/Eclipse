// Eclipse - the Camera view's Render View: frames as they are rendered, kept, and brought up to date by itself or when asked.
#include "canvas/Canvas.h"
#include "canvas/CanvasPaintShared.h"
#include "canvas/FrameRender.h"

#include <QTimer>

#include <algorithm>

using namespace canvas_paint_shared;

void Canvas::setAutoRender(bool on)
{
    m_autoRender = on;
    if (on && m_renderStale)
        clearRenderCache();
    emit renderStateChanged();
    update();
}

void Canvas::setPreviewReduction(int factor)
{
    int const reduce = factor >= 8 ? 8 : factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (reduce == m_renderReduce)
        return;
    m_renderReduce = reduce;
    // (What was kept is of another size: rendered again as it is shown.)
    bool const stale = m_renderStale;
    clearRenderCache();
    m_renderStale = stale && !m_autoRender;
    update();
}

void Canvas::setProgressiveRender(bool on)
{
    m_progressiveRender = on;
}

void Canvas::setRenderCacheSize(int megabytes)
{
    m_renderFrames.setMaxCost(std::clamp(megabytes, 0, 64 * 1024) * qsizetype(1024));
}

void Canvas::clearRenderCache()
{
    m_renderFrames.clear();
    m_renderShown = QImage();
    m_renderShownFrame = -1;
    m_roughRender = QImage();
    m_roughFrame = -1;
    m_renderStale = false;
    emit renderStateChanged();
}

void Canvas::renderCurrentFrame()
{
    if (m_renderStale) {
        // Every picture kept is of the scene as it was.
        clearRenderCache();
    } else {
        m_renderFrames.remove(m_frame);
        if (m_renderShownFrame == m_frame) {
            m_renderShown = QImage();
            m_renderShownFrame = -1;
        }
    }
    update();
}

void Canvas::setViewTimeouts(int effectsMs, int drawMs)
{
    m_effectsTimeout = std::max(0, effectsMs);
    m_drawTimeout = std::max(0, drawMs);
    update();
}

void Canvas::setViewTimeoutOff(bool off)
{
    m_timeoutOff = off;
    update();
}

QImage Canvas::renderViewImage(int frame)
{
    if (m_renderShownFrame == frame && !m_renderShown.isNull())
        return m_renderShown;
    if (QImage const* const kept = m_renderFrames.object(frame)) {
        m_renderShown = *kept;
        m_renderShownFrame = frame;
        return m_renderShown;
    }
    Scene const& scene = m_document->scene();
    if (m_progressiveRender && m_renderReduce < 4 && !m_playing) {
        // Progressive: a quick picture now, the full one a moment later.
        if (m_roughFrame != frame || m_roughRender.isNull()) {
            m_roughRender = FrameRender::frame(scene, frame, FrameRender::sizeAt(scene, 0.25 / m_renderReduce), true);
            m_roughFrame = frame;
            QTimer::singleShot(0, this, &Canvas::finishRender);
        }
        return m_roughRender;
    }
    QImage const image = FrameRender::frame(scene, frame, FrameRender::sizeAt(scene, 1.0 / m_renderReduce));
    m_renderShown = image;
    m_renderShownFrame = frame;
    m_renderFrames.insert(frame, new QImage(image), std::max<qsizetype>(1, qsizetype(image.sizeInBytes() / 1024)));
    if (m_renderReduce == 1)
        emit frameRendered(frame, image);
    return image;
}

void Canvas::finishRender()
{
    if (m_roughFrame < 0)
        return;
    int const frame = m_roughFrame;
    m_roughFrame = -1;
    m_roughRender = QImage();
    if (frame != m_frame || m_viewMode != ViewRender)
        return;
    bool const progressive = m_progressiveRender;
    m_progressiveRender = false;
    renderViewImage(frame);
    m_progressiveRender = progressive;
    update();
}
