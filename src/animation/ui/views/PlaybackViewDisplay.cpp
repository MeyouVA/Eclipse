#include "animation/ui/views/PlaybackView.h"
#include "animation/ui/views/PlaybackSurface.h"
#include <QAction>
#include <algorithm>

void PlaybackView::setImageChannel(int channel)
{
    channel=std::clamp(channel,0,4); m_surface->setChannel(channel);
    if(auto* action=findChild<QAction*>(QStringLiteral("PlaybackChannel%1").arg(channel))) action->setChecked(true);
}
QImage PlaybackView::displayImage() const { return m_surface->displayImage(); }
