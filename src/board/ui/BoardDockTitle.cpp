#include "board/ui/BoardDockTitle.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>

BoardDockTitle::BoardDockTitle(QDockWidget* dock, std::function<void(QMenu*)> fillMenu)
    : QWidget(dock), m_dock(dock), m_fill(std::move(fillMenu)), m_label(new QLabel(dock->windowTitle(), this)),
      m_menuButton(new QToolButton(this)), m_close(new QToolButton(this)), m_menu(new QMenu(this))
{
    setObjectName(QStringLiteral("BoardDockTitle"));
    m_label->setObjectName(QStringLiteral("BoardDockTitleLabel"));
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setMinimumWidth(96);
    m_menuButton->setObjectName(QStringLiteral("BoardDockMenu"));
    m_menuButton->setText(QStringLiteral("+"));
    m_menuButton->setToolTip(tr("%1 menu").arg(dock->windowTitle()));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setMenu(m_menu);
    m_close->setObjectName(QStringLiteral("BoardDockClose"));
    m_close->setText(QStringLiteral("×"));
    m_close->setToolTip(tr("Close %1").arg(dock->windowTitle()));
    m_close->setAutoRaise(true);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 1, 2, 1);
    row->setSpacing(0);
    row->addStretch();
    row->addWidget(m_label);
    row->addWidget(m_menuButton);
    row->addWidget(m_close);
    connect(m_menu, &QMenu::aboutToShow, this, &BoardDockTitle::rebuildMenu);
    connect(m_close, &QToolButton::clicked, dock, &QDockWidget::close);
    connect(dock, &QDockWidget::windowTitleChanged, m_label, &QLabel::setText);
}

void BoardDockTitle::rebuildMenu()
{
    m_menu->clear();
    if (m_fill) {
        m_fill(m_menu);
        if (!m_menu->isEmpty())
            m_menu->addSeparator();
    }
    QAction* floating = m_menu->addAction(m_dock->isFloating() ? tr("Dock View") : tr("Float View"));
    floating->setObjectName(QStringLiteral("BoardDockFloat"));
    connect(floating, &QAction::triggered, m_dock, [dock = m_dock] { dock->setFloating(!dock->isFloating()); });
    QAction* close = m_menu->addAction(tr("Close View"));
    close->setObjectName(QStringLiteral("BoardDockCloseView"));
    connect(close, &QAction::triggered, m_dock, &QDockWidget::close);
}

void BoardDockTitle::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dock->features().testFlag(QDockWidget::DockWidgetFloatable)) {
        m_dock->setFloating(!m_dock->isFloating());
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
