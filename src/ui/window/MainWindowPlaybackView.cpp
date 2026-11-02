#include "ui/window/MainWindow.h"
#include "ui/views/PlaybackView.h"
#include "model/document/Commands.h"

void MainWindow::onActionOpenPlayback()
{
    stop();
    auto* view=findChild<PlaybackView*>();
    if(!view) {
        view=new PlaybackView(m_document->scene().fps,this);
        view->setAttribute(Qt::WA_DeleteOnClose);
    }
    view->show(); view->raise();
}
