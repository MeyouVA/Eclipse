#include "board/model/BoardDocument.h"

#include <algorithm>

bool BoardDocument::setSceneDetails(int scene, QString const& name, QString const& sequence, QString const& act)
{
    if (!hasPanel(scene, 0))
        return false;
    auto const& current = m_project.shots[scene];
    QString const newName = name == current.name ? name : name.trimmed();
    QString const newSequence = sequence == current.sequence ? sequence : sequence.trimmed();
    QString const newAct = act == current.act ? act : act.trimmed();
    if (newName.isEmpty() || (newName == current.name && newSequence == current.sequence && newAct == current.act))
        return false;
    if (newName != current.name) {
        bool const duplicate = std::any_of(m_project.shots.cbegin(), m_project.shots.cend(), [&](BoardShot const& other) {
            return &other != &current && other.name.compare(newName, Qt::CaseInsensitive) == 0;
        });
        if (current.namesLocked || duplicate)
            return false;
    }
    BoardProject next = m_project;
    auto& changed = next.shots[scene];
    changed.name = newName;
    changed.sequence = newSequence;
    changed.act = newAct;
    apply(std::move(next), tr("Edit Scene Details"));
    return true;
}

bool BoardDocument::setPanelName(int scene, int panel, QString const& name)
{
    auto const* current = this->panel(scene, panel);
    QString const trimmed = name.trimmed();
    if (!current || m_project.shots[scene].namesLocked || current->name == trimmed)
        return false;
    BoardProject next = m_project;
    next.shots[scene].panels[panel].name = trimmed;
    apply(std::move(next), tr("Rename Panel"));
    return true;
}

bool BoardDocument::setSceneNamesLocked(int scene, bool locked)
{
    if (!hasPanel(scene, 0) || m_project.shots[scene].namesLocked == locked)
        return false;
    BoardProject next = m_project;
    next.shots[scene].namesLocked = locked;
    apply(std::move(next), locked ? tr("Lock Scene and Panel Names") : tr("Unlock Scene and Panel Names"));
    return true;
}

bool BoardDocument::renameSceneGroup(int scene, BoardProject::SceneGroup group, QString const& name)
{
    if (!hasPanel(scene, 0))
        return false;
    bool const sequence = group == BoardProject::SceneGroup::Sequence;
    auto const& current = m_project.shots[scene];
    if ((sequence ? current.sequence : current.act).trimmed().isEmpty())
        return false;
    auto label = [sequence](BoardShot& shot) -> QString& { return sequence ? shot.sequence : shot.act; };
    auto const [first, end] = m_project.groupRange(scene, group);
    QString const trimmed = name.trimmed();
    BoardProject next = m_project;
    bool changed = false;
    for (int i = first; i < end; ++i) {
        auto& field = label(next.shots[i]);
        if (field != trimmed) {
            field = trimmed;
            changed = true;
        }
    }
    if (changed)
        apply(std::move(next), sequence ? tr("Rename Sequence") : tr("Rename Act"));
    return changed;
}
