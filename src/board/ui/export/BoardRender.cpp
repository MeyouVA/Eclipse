#include "board/ui/export/BoardRender.h"
#include "board/ui/BoardStage.h"
#include "canvas/Canvas.h"

#include <QPainter>
#include <algorithm>

BoardRender::BoardRender(BoardProject const& project) : m_project(project), m_spans(project.spans()) {}

int BoardRender::length() const
{
    return m_spans.isEmpty() ? 0 : m_spans.last().start + m_spans.last().length;
}

QImage BoardRender::panel(int row, int local, QSize size, QColor background, bool deep, Cached& cache)
{
    auto const& span = m_spans[row];
    auto const& item = m_project.shots[span.shot].panels[span.panel];
    auto const camera = BoardCamera::poseAt(item.camera, local);
    if (cache.row == row && cache.camera == camera && cache.picture.size() == size
        && cache.background == background && cache.deep == deep)
        return cache.picture;
    Scene scene = boardPanelScene(m_project, span.shot, span.panel, camera);
    scene.background = background;
    QImage picture(size, deep ? QImage::Format_RGBA64_Premultiplied : QImage::Format_ARGB32_Premultiplied);
    if (picture.isNull())
        return {};
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.scale(double(size.width()) / m_project.width, double(size.height()) / m_project.height);
    Canvas::paintFrame(painter, scene, 0);
    painter.end();
    cache = {picture, row, camera, background, deep};
    return picture;
}

QImage BoardRender::frame(int frame, QSize size, QColor background, bool deep, bool dissolve)
{
    if (frame < 0 || frame >= length() || size.isEmpty() || qint64(size.width()) * size.height() > 64 * 1024 * 1024)
        return {};
    auto const after = std::upper_bound(m_spans.cbegin(), m_spans.cend(), frame,
        [](int target, BoardSpan const& span) { return target < span.start; });
    int const row = int(after - m_spans.cbegin() - 1);
    auto const& span = m_spans[row];
    int const local = frame - span.start;
    QImage const current = panel(row, local, size, background, deep, m_current);
    int const fade = dissolve && span.shot > 0 && span.panel == 0
        ? std::min(span.length, m_project.shots[span.shot].dissolve) : 0;
    if (local + 1 >= fade || current.isNull())
        return current;
    QImage const previous = panel(row - 1, m_spans[row - 1].length - 1, size, background, deep, m_previous);
    if (previous.isNull())
        return {};
    QImage blended(size, current.format());
    if (blended.isNull())
        return {};
    blended.fill(Qt::transparent);
    QPainter painter(&blended);
    double const mix = double(local + 1) / fade;
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setOpacity(1.0 - mix);
    painter.drawImage(0, 0, previous);
    painter.setOpacity(mix);
    painter.drawImage(0, 0, current);
    painter.end();
    return blended;
}
