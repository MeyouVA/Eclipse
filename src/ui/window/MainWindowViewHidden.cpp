// Eclipse - the main window: layers hidden from the Camera and scene planning views (Hide Selected / Show Hidden).
#include "ui/window/MainWindow.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/views/DepthView.h"
#include "ui/views/PerspectiveView.h"
#include <QStatusBar>

void MainWindow::onActionHideSelectedInViews()
{
    Scene const& scene = m_document->scene();
    QVector<int> chosen = m_timeline->selectedLayers();
    if (chosen.isEmpty() && layer() >= 0)
        chosen.push_back(layer());
    QSet<int> hidden = m_canvas->viewHidden();
    int added = 0;
    for (int l : std::as_const(chosen))
        if (l >= 0 && l < scene.layers.size() && scene.layers[l].kind != Layer::Camera && !hidden.contains(scene.layers[l].id)) {
            hidden.insert(scene.layers[l].id);
            ++added;
        }
    if (added == 0) {
        statusBar()->showMessage(tr("Nothing selected to hide."), 3000);
        return;
    }
    m_canvas->setViewHidden(hidden);
    statusBar()->showMessage(tr("Hidden from the views: %n layer(s). Show Hidden brings them back.", nullptr, added), 4000);
}

void MainWindow::onActionShowHiddenInViews()
{
    if (m_canvas->viewHidden().isEmpty()) {
        statusBar()->showMessage(tr("Nothing is hidden."), 3000);
        return;
    }
    m_canvas->setViewHidden({});
    statusBar()->showMessage(tr("All hidden layers are shown again."), 3000);
}

void MainWindow::refreshViewHidden()
{
    for (DepthView* view : findChildren<DepthView*>())
        view->update();
    for (PerspectiveView* view : findChildren<PerspectiveView*>())
        view->update();
}
