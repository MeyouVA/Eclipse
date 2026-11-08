#include "animation/ui/window/MainWindow.h"
#include "animation/ui/views/PlaybackView.h"
#include "animation/ui/views/PreviewManagerView.h"
#include "model/document/Commands.h"
#include <QStatusBar>

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

void MainWindow::onActionPreviewToPlayback()
{
    auto* preview=findChild<PreviewManagerView*>();
    auto const frames=preview?preview->framesForPlayback():QVector<QImage>();
    if(frames.isEmpty()) { statusBar()->showMessage(tr("Render a current preview before opening it in Playback."),5000); return; }
    onActionOpenPlayback();
    if(auto* viewer=findChild<PlaybackView*>()) {
        QString error;
        if(viewer->loadPreview(frames,preview->sound(),preview->playbackRate(),&error)) viewer->play();
        else statusBar()->showMessage(error,5000);
    }
}
