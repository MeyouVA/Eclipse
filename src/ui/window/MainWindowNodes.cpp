// Eclipse - the window's Node View commands: printing it, group content, parent and child past effects.
#include "ui/window/MainWindow.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QStatusBar>

#include "actions/manager/ActionManager.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/NodeView.h"

void MainWindow::onActionSelectParentSkippingEffects()
{
    if (m_followNodeParenting && m_nodeView->graphMode())
        m_nodeView->onActionSelectParentSkippingEffects();
    else
        onActionSelectParent();
}

void MainWindow::onActionSelectChildSkippingEffects()
{
    if (m_followNodeParenting && m_nodeView->graphMode())
        m_nodeView->onActionSelectChildSkippingEffects();
    else
        onActionSelectChild();
}

void MainWindow::onActionSelectGroupContent()
{
    Scene const& scene = m_document->scene();
    // The current layer when it is a group; otherwise the group it is in.
    int group = layer();
    while (group >= 0 && group < scene.layers.size() && scene.layers[group].kind != Layer::Group)
        group = scene.parentIndex(group);
    if (group < 0 || group >= scene.layers.size()) {
        statusBar()->showMessage(tr("Choose a group layer, or a layer in one."), 5000);
        return;
    }
    QVector<int> inside;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        for (int up = scene.parentIndex(l), guard = 0; up >= 0 && guard < 256; up = scene.parentIndex(up), ++guard)
            if (up == group) {
                inside << l;
                break;
            }
    if (inside.isEmpty()) {
        statusBar()->showMessage(tr("The group %1 is empty.").arg(scene.layers[group].name), 5000);
        return;
    }
    m_timeline->setCurrentCell(inside.first(), frame());
    m_timeline->setExtraLayers(inside.mid(1));
}

void MainWindow::onActionPrintNodeView()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setDocName(m_path.isEmpty() ? tr("Node View") : QFileInfo(m_path).completeBaseName() + tr(" - Node View"));
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print Node View"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    // Drawn at screen size and laid on the page: the node names keep their size against the nodes.
    QRectF const page = printer.pageLayout().paintRectPixels(printer.resolution());
    double const k = 2400.0 / std::max(page.width(), page.height());
    QImage const picture = m_nodeView->picture(QSize(int(page.width() * k), int(page.height() * k)));
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(this, tr("Print"), tr("The Node View could not be printed."));
        return;
    }
    painter.drawImage(QRectF(0, 0, page.width(), page.height()), picture);
    painter.end();
    statusBar()->showMessage(tr("The Node View was sent to the printer."), 5000);
}

void MainWindow::syncNodeViewChecks()
{
    m_actions->setChecked(QStringLiteral("NodeCableCutter"), m_nodeView->cableCutter());
    m_actions->setChecked(QStringLiteral("NodeMagnifier"), m_nodeView->magnifier());
    m_actions->setChecked(QStringLiteral("NodeViewNavigatorShow"), m_nodeView->navigatorShown());
    m_actions->setChecked(QStringLiteral("NodeThumbnailPattern"), m_nodeView->thumbnailPattern());
    m_actions->setChecked(QStringLiteral("NodeShowPortFiltering"), m_nodeView->portFilteringShown());
}
