// Eclipse - a collapse handle: the pair of arrows on the line between two things in the window.
#include "ui/window/CollapseHandle.h"

#include <QToolButton>

static int const kThick = 13, kLong = 22;

CollapseHandle::CollapseHandle(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    auto arrow = [this](char const* name, bool second) {
        QToolButton* const button = new QToolButton(this);
        button->setObjectName(QLatin1String(name));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral("QToolButton { background: rgba(40, 40, 40, 215); border: 1px solid #202020; border-radius: 2px; padding: 0px; }"
                                             "QToolButton:hover { background: rgba(42, 155, 176, 235); }"
                                             "QToolButton:disabled { background: rgba(40, 40, 40, 110); }"));
        connect(button, &QToolButton::clicked, this, [this, second] { emit pressed(second); });
        return button;
    };
    m_first = arrow("First", false);
    m_second = arrow("Second", true);
    m_stacked = true;
    setStacked(false);
}

QSize CollapseHandle::handleSize() const
{
    return m_stacked ? QSize(2 * kLong, kThick) : QSize(kThick, 2 * kLong);
}

void CollapseHandle::setStacked(bool stacked)
{
    if (stacked == m_stacked)
        return;
    m_stacked = stacked;
    m_first->setArrowType(stacked ? Qt::UpArrow : Qt::LeftArrow);
    m_second->setArrowType(stacked ? Qt::DownArrow : Qt::RightArrow);
    if (stacked) {
        m_first->setGeometry(0, 0, kLong, kThick);
        m_second->setGeometry(kLong, 0, kLong, kThick);
    } else {
        m_first->setGeometry(0, 0, kThick, kLong);
        m_second->setGeometry(0, kLong, kThick, kLong);
    }
    resize(handleSize());
}

void CollapseHandle::setArrowsEnabled(bool first, bool second)
{
    m_first->setEnabled(first);
    m_second->setEnabled(second);
}
