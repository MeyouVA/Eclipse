#include "board/model/BoardDocument.h"

#include <QSet>

QString BoardDocument::uniqueShotName(QString const& base) const
{
    QSet<QString> names;
    for (auto const& shot : m_project.shots)
        names.insert(shot.name.toCaseFolded());
    QString name = base;
    for (int number = 2; names.contains(name.toCaseFolded()); ++number)
        name = tr("%1 (%2)").arg(base).arg(number);
    return name;
}

bool BoardDocument::duplicateShot(int shot)
{
    if (!hasPanel(shot, 0))
        return false;
    BoardShot copy = m_project.shots[shot];
    qint64 frames = 0;
    for (auto const& panel : copy.panels)
        frames += panel.length;
    if (!canGrow(copy.panels.size(), frames))
        return false;
    copy.name = uniqueShotName(copy.name);
    auto next = m_project;
    next.shots.insert(shot + 1, std::move(copy));
    apply(std::move(next), tr("Duplicate Scene"));
    return true;
}

bool BoardDocument::splitShot(int shot, int firstPanel)
{
    if (firstPanel <= 0 || !hasPanel(shot, firstPanel))
        return false;
    auto next = m_project;
    auto& source = next.shots[shot];
    BoardShot tail{uniqueShotName(source.name), source.sequence, source.act, source.panels.mid(firstPanel)};
    tail.namesLocked = source.namesLocked;
    source.panels.resize(firstPanel);
    next.shots.insert(shot + 1, std::move(tail));
    apply(std::move(next), tr("Split Scene"));
    return true;
}

bool BoardDocument::duplicatePanel(int shot, int panel)
{
    if (!hasPanel(shot, panel) || !appendPanelAllowed(m_project.shots[shot].panels[panel].length))
        return false;
    auto next = m_project;
    auto& panels = next.shots[shot].panels;
    BoardPanel const copy = panels[panel];
    panels.insert(panel + 1, copy);
    apply(std::move(next), tr("Duplicate Panel"));
    return true;
}

bool BoardDocument::splitPanel(int shot, int panel, int firstFrames)
{
    if (!hasPanel(shot, panel) || firstFrames <= 0 || firstFrames >= m_project.shots[shot].panels[panel].length
        || !canGrow(1, 0))
        return false;
    auto next = m_project;
    auto& panels = next.shots[shot].panels;
    BoardPanel tail = panels[panel];
    tail.length -= firstFrames;
    tail.camera = BoardCamera::slice(panels[panel].camera, firstFrames, tail.length);
    panels[panel].camera = BoardCamera::slice(panels[panel].camera, 0, firstFrames);
    panels[panel].length = firstFrames;
    panels.insert(panel + 1, std::move(tail));
    apply(std::move(next), tr("Split Panel"));
    return true;
}

int BoardDocument::moveShot(int source, int target, bool after)
{
    if (!hasPanel(source, 0) || !hasPanel(target, 0))
        return -1;
    int const boundary = target + int(after);
    int const destination = boundary - int(source < boundary);
    if (destination == source)
        return -1;
    auto next = m_project;
    next.shots.move(source, destination);
    apply(std::move(next), tr("Reorder Scenes"));
    return destination;
}

QPair<int, int> BoardDocument::movePanel(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after)
{
    if (!hasPanel(sourceShot, sourcePanel) || !hasPanel(targetShot, targetPanel))
        return {-1, -1};
    int const boundary = targetPanel + int(after);
    if (sourceShot == targetShot && (sourcePanel == boundary || sourcePanel + 1 == boundary))
        return {-1, -1};

    auto next = m_project;
    BoardPanel moved = next.shots[sourceShot].panels.takeAt(sourcePanel);
    int const insertion = boundary - int(sourceShot == targetShot && sourcePanel < boundary);
    next.shots[targetShot].panels.insert(insertion, std::move(moved));
    if (next.shots[sourceShot].panels.isEmpty()) {
        next.shots.removeAt(sourceShot);
        targetShot -= int(sourceShot < targetShot);
    }
    apply(std::move(next), tr("Reorder Panels"));
    return {targetShot, insertion};
}
