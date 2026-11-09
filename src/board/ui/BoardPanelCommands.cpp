#include "board/ui/BoardWindow.h"
#include "board/ui/BoardPlayback.h"
#include "board/ui/BoardTimeline.h"

#include <QInputDialog>

void BoardWindow::editArrangement(QString const& operation)
{
    m_playback->stop();
    auto const [shot, panel] = m_selected;
    auto const& project = m_document.project();
    if (shot < 0 || shot >= project.shots.size() || panel < 0 || panel >= project.shots[shot].panels.size())
        return;
    int const frames = defaultPanelFrames(project.fps);
    if (operation == QLatin1String("new-scene")) {
        if (m_document.addShot(QString::number(project.shots.size() + 1), frames, shot))
            selectPanel(shot + 1, 0);
    } else if (operation == QLatin1String("duplicate-scene")) {
        if (m_document.duplicateShot(shot))
            selectPanel(shot + 1, panel);
    } else if (operation == QLatin1String("remove-scene")) {
        if (m_document.removeShot(shot))
            selectPanel(std::min(shot, int(m_document.project().shots.size()) - 1), 0);
    } else if (operation == QLatin1String("split-scene")) {
        if (m_document.splitShot(shot, panel))
            selectPanel(shot + 1, 0);
    } else if (operation == QLatin1String("new-panel")) {
        if (m_document.addPanel(shot, frames, panel))
            selectPanel(shot, panel + 1);
    } else if (operation == QLatin1String("duplicate-panel")) {
        if (m_document.duplicatePanel(shot, panel))
            selectPanel(shot, panel + 1);
    } else if (operation == QLatin1String("remove-panel")) {
        m_document.removePanel(shot, panel);
    } else if (operation == QLatin1String("split-panel")) {
        int const length = project.shots[shot].panels[panel].length;
        if (length < 2)
            return;
        int first = m_timeline->frame() - m_timeline->panelStart(shot, panel);
        bool accepted = first > 0 && first < length;
        if (!accepted)
            first = QInputDialog::getInt(this, tr("Split Panel"), tr("Frames in the first panel:"),
                                        length / 2, 1, length - 1, 1, &accepted);
        if (accepted && m_document.splitPanel(shot, panel, first))
            selectPanel(shot, panel + 1);
    }
}

void BoardWindow::dropPanel(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after, bool wholeScene)
{
    if (wholeScene) {
        int const shot = m_document.moveShot(sourceShot, targetShot, after);
        if (shot >= 0)
            selectPanel(shot, sourcePanel);
    } else {
        auto const moved = m_document.movePanel(sourceShot, sourcePanel, targetShot, targetPanel, after);
        if (moved.first >= 0)
            selectPanel(moved.first, moved.second);
    }
}
