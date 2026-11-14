#include "board/model/BoardDocument.h"
#include "board/model/BoardProjectJson.h"

#include <QFileInfo>

bool BoardDocument::setDissolve(int shot, int frames)
{
    if (shot < 1 || !hasPanel(shot, 0) || frames < 0 || frames > m_project.shots[shot].panels[0].length
        || m_project.shots[shot].dissolve == frames)
        return false;
    auto next = m_project;
    next.shots[shot].dissolve = frames;
    apply(std::move(next), frames == 0 ? tr("Remove Dissolve") : tr("Change Dissolve"));
    return true;
}

bool BoardDocument::setSound(BoardSound const& sound)
{
    if (m_path.isEmpty() || !BoardProjectJson::validSound(sound) || m_project.sound == sound)
        return false;
    auto next = m_project;
    next.sound = sound;
    apply(std::move(next), tr("Edit Sound Track"));
    return true;
}

bool BoardDocument::relinkSound(QString const& asset)
{
    auto const& current = m_project.sound;
    if (m_path.isEmpty() || current.asset.isEmpty() || asset.isEmpty() || asset == current.asset)
        return false;
    auto next = m_project;
    next.sound.asset = asset;
    if (!BoardProjectJson::validSound(next.sound) || !QFileInfo(next.audioPath(m_path)).isFile())
        return false;

    QString const missing = QFileInfo(m_project.audioPath(m_path)).isFile() ? QString() : current.asset;
    apply(std::move(next), tr("Relink Sound"), 0, missing);
    return true;
}
