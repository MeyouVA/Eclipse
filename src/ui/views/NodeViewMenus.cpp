#include "ui/views/NodeView.h"
#include <QMenu>

void NodeView::addNavigationMenu(QMenu& menu)
{
    QMenu* const selection = menu.addMenu(tr("Selection"));
    connect(selection->addAction(tr("Select All")), &QAction::triggered, this, &NodeView::selectAll);
    connect(selection->addAction(tr("Deselect All")), &QAction::triggered, this, &NodeView::deselectAll);
    connect(selection->addAction(tr("Invert Selection")), &QAction::triggered, this, &NodeView::onActionInvertSelection);
    connect(menu.addAction(tr("Centre on Selection (O)")), &QAction::triggered, this, &NodeView::onActionCentreOnSelection);
    connect(menu.addAction(tr("Show All Nodes")), &QAction::triggered, this, &NodeView::onActionResetView);
    QMenu* const navigator = menu.addMenu(tr("Navigator"));
    QAction* const hidden = navigator->addAction(tr("Hide Navigator"));
    hidden->setCheckable(true);
    hidden->setChecked(!navigatorShown());
    connect(hidden, &QAction::triggered, this, [this] { setNavigatorShown(false); });
    QStringList const corners{tr("Top Left"), tr("Top Right"), tr("Bottom Right"), tr("Bottom Left")};
    for (int corner = 0; corner < corners.size(); ++corner) {
        QAction* const action = navigator->addAction(corners[corner]);
        action->setCheckable(true);
        action->setChecked(navigatorShown() && navigatorCorner() == corner);
        connect(action, &QAction::triggered, this, [this, corner] {
            setNavigatorCorner(corner);
            setNavigatorShown(true);
        });
    }
    QMenu* const style = menu.addMenu(tr("Cable Style"));
    for (bool straight : {true, false}) {
        QAction* const action = style->addAction(straight ? tr("Line") : tr("Bezier"));
        action->setCheckable(true);
        action->setChecked(straightCables() == straight);
        connect(action, &QAction::triggered, this, [this, straight] { setStraightCables(straight); });
    }
}
