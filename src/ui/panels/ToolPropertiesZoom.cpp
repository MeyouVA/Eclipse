// Eclipse - Tool Properties: the Zoom tool - which way a click zooms, and zooming or resetting the view at once.
#include "ui/panels/ToolProperties.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>

#include "actions/theme/AppSettings.h"

void ToolProperties::addZoomRows()
{
    QWidget* const way = new QWidget(this);
    way->setObjectName(QStringLiteral("ZoomWay"));
    QHBoxLayout* const across = new QHBoxLayout(way);
    across->setContentsMargins(0, 0, 0, 0);
    QButtonGroup* const group = new QButtonGroup(way);
    auto button = [&](char const* name, QString const& text, QString const& tip) {
        QToolButton* const b = new QToolButton(way);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        across->addWidget(b);
        return b;
    };
    QToolButton* const in = button("ZoomModeIn", tr("Zoom In"), tr("A click zooms in (Alt-click: out)."));
    QToolButton* const out = button("ZoomModeOut", tr("Zoom Out"), tr("A click zooms out (Alt-click: in)."));
    for (QToolButton* const b : {in, out}) {
        b->setCheckable(true);
        group->addButton(b);
    }
    bool const zoomsOut = settings().value(QStringLiteral("tools/zoomOut"), false).toBool();
    (zoomsOut ? out : in)->setChecked(true);
    m_canvas->setZoomToolOut(zoomsOut);
    connect(out, &QToolButton::toggled, this, [this](bool on) {
        m_canvas->setZoomToolOut(on);
        settings().setValue(QStringLiteral("tools/zoomOut"), on);
    });
    across->addSpacing(12);
    connect(button("ZoomPerformIn", tr("+"), tr("Perform Zoom In")), &QToolButton::clicked, m_canvas, [this] { m_canvas->onActionPerformZoom(QStringLiteral("in")); });
    connect(button("ZoomPerformOut", tr("-"), tr("Perform Zoom Out")), &QToolButton::clicked, m_canvas, [this] { m_canvas->onActionPerformZoom(QStringLiteral("out")); });
    across->addSpacing(12);
    connect(button("ZoomReset", tr("Reset Zoom"), tr("Reset Zoom")), &QToolButton::clicked, m_canvas, &Canvas::onActionResetZoom);
    connect(button("ZoomResetView", tr("Reset View"), tr("Reset View")), &QToolButton::clicked, m_canvas, &Canvas::onActionResetView);
    across->addStretch(1);
    m_form->addRow(way);
    m_zoomRows.push_back(way);
}
