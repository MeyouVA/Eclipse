#include "board/ui/BoardPreview.h"
#include "board/ui/BoardStage.h"
#include "board/model/BoardProject.h"
#include "canvas/Canvas.h"

#include <QPainter>

namespace {
QPixmap paintPanel(BoardProject const& project, QPair<int, int> address, BoardCameraPose const& camera)
{
    if (address.first < 0)
        return {};
    QSize size(project.width, project.height);
    if (size.width() > 1920 || size.height() > 1080)
        size.scale(QSize(1920, 1080), Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    QPixmap image(size);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.scale(double(size.width()) / project.width, double(size.height()) / project.height);
    Canvas::paintFrame(painter, boardPanelScene(project, address.first, address.second, camera), 0);
    painter.end();
    return image;
}
}

BoardPreview::BoardPreview(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("BoardStagePreview"));
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void BoardPreview::showFrame(BoardProject const& project, QPair<int, int> panel, int frame, QPair<int, int> previous, double mix)
{
    if (previous != m_before) {
        m_before = previous;
        m_previous = {};
        if (previous.first >= 0) {
            auto const& before = project.shots[previous.first].panels[previous.second];
            auto const pose = BoardCamera::poseAt(before.camera, before.length - 1);
            m_previous = previous == m_panel && pose == m_pose ? m_current : paintPanel(project, previous, pose);
        }
    }
    auto const pose = BoardCamera::poseAt(project.shots[panel.first].panels[panel.second].camera, frame);
    if (panel != m_panel || pose != m_pose) {
        m_panel = panel;
        m_pose = pose;
        m_current = paintPanel(project, panel, pose);
    }
    m_mix = mix;
    update();
}

void BoardPreview::clear()
{
    m_current = {};
    m_previous = {};
    m_panel = m_before = {-1, -1};
}

void BoardPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(238, 238, 238));
    if (m_current.isNull())
        return;
    QSize const fitted = m_current.size().scaled((size() - QSize(48, 48)).expandedTo(QSize(1, 1)), Qt::KeepAspectRatio);
    QRect const destination(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!m_previous.isNull()) {
        painter.drawPixmap(destination, m_previous);
        painter.setOpacity(m_mix);
    }
    painter.drawPixmap(destination, m_current);
}
