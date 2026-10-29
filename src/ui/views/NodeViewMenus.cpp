#include "ui/views/NodeView.h"
#include <QMenu>

void NodeView::addNavigationMenu(QMenu& menu)
{
    // The Node View's own commands (as its menu has them), for a graph wired by hand too.
    if (m_sharedMenu)
        if (QMenu* const shared = m_sharedMenu(&menu)) {
            for (QAction* action : shared->actions())
                menu.addAction(action);
            return;
        }
    QMenu* const selection = menu.addMenu(tr("Selection"));
    connect(selection->addAction(tr("Select All")), &QAction::triggered, this, &NodeView::selectAll);
    connect(selection->addAction(tr("Deselect All")), &QAction::triggered, this, &NodeView::deselectAll);
    connect(selection->addAction(tr("Invert Selection")), &QAction::triggered, this, &NodeView::onActionInvertSelection);
    connect(menu.addAction(tr("Centre on Selection (O)")), &QAction::triggered, this, &NodeView::onActionCentreOnSelection);
    connect(menu.addAction(tr("Show All Nodes")), &QAction::triggered, this, &NodeView::onActionResetView);
    QMenu* const navigator = menu.addMenu(tr("Navigator"));
    QAction* const shown = navigator->addAction(tr("Show Navigator"));
    shown->setCheckable(true);
    shown->setChecked(navigatorShown());
    connect(shown, &QAction::toggled, this, &NodeView::onActionShowNavigator);
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
    QString const styles[3] = {tr("Bezier"), tr("Line"), tr("Straight")};
    for (int type : {1, 2, 0}) {
        QAction* const action = style->addAction(styles[type]);
        action->setCheckable(true);
        action->setChecked(cableType() == type);
        connect(action, &QAction::triggered, this, [this, type] { setCableType(type); });
    }
}
