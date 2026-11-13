#include "board/model/BoardDocument.h"

#include <QFileInfo>
#include <QLockFile>
#include <QSignalBlocker>
#include <QUndoCommand>
#include <limits>

class BoardEdit final : public QUndoCommand {
public:
    BoardEdit(BoardProject& current, BoardProject next, QString const& label, quint64 session)
        : QUndoCommand(label), m_current(current), m_before(current), m_after(std::move(next)), m_session(session) {}

    void redo() override { m_current = m_after; }
    void undo() override { m_current = m_before; }
    int id() const override { return m_session == 0 ? -1 : 1; }

    bool mergeWith(QUndoCommand const* other) override
    {
        auto const* edit = dynamic_cast<BoardEdit const*>(other);
        if (!edit || edit->m_session != m_session)
            return false;
        m_after = edit->m_after;
        return true;
    }

    void collectSoundAssets(QSet<QString>& assets) const
    {
        assets.insert(m_before.sound.asset);
        assets.insert(m_after.sound.asset);
    }

private:
    BoardProject& m_current;
    BoardProject m_before;
    BoardProject m_after;
    quint64 m_session;
};

BoardDocument::BoardDocument(QObject* parent) : QObject(parent)
{
    m_undo.setUndoLimit(100);
    connect(&m_undo, &QUndoStack::indexChanged, this, &BoardDocument::projectChanged);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) {
        if (clean && m_wroteRecovery)
            clearRecovery();
    });
}

BoardDocument::~BoardDocument()
{
    disconnect(&m_undo, nullptr, this, nullptr);
}

bool BoardDocument::open(QString const& path, QString* error)
{
    QFileInfo const info(path);
    if (!info.isFile() || info.isSymLink()) {
        if (error)
            *error = tr("Choose an existing, regular project file.");
        return false;
    }
    QString const canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        if (error)
            *error = tr("Could not resolve the project file location.");
        return false;
    }
    bool const sameProject = canonical == m_path;
    auto lock = sameProject ? nullptr : lockProject(canonical, error);
    if (!sameProject && !lock)
        return false;
    BoardProject project;
    bool legacy = false;
    if (!BoardProject::open(canonical, project, error, &legacy))
        return false;
    if (!m_undo.isClean())
        clearRecovery();
    if (!sameProject)
        m_projectLock = std::move(lock);
    resetProject(std::move(project), canonical, legacy);
    return true;
}

void BoardDocument::close()
{
    if (!m_undo.isClean())
        clearRecovery();
    m_projectLock.reset();
    resetProject({}, {});
}

void BoardDocument::resetProject(BoardProject project, QString path, bool legacy)
{
    {
        QSignalBlocker const block(&m_undo);
        m_undo.clear();
    }
    m_project = std::move(project);
    m_path = std::move(path);
    m_legacy = legacy;
    m_wroteRecovery = false;
    emit projectChanged();
}

bool BoardDocument::save(QString* error)
{
    if (m_path.isEmpty()) {
        if (error)
            *error = tr("Open or create a project before saving.");
        return false;
    }
    if (!m_project.save(m_path, error))
        return false;
    m_legacy = false;
    clearRecovery();
    m_undo.setClean();
    return true;
}

QSet<QString> BoardDocument::soundAssets() const
{
    QSet<QString> assets{m_project.sound.asset};
    for (int i = 0; i < m_undo.count(); ++i)
        if (auto const* command = dynamic_cast<BoardEdit const*>(m_undo.command(i)))
            command->collectSoundAssets(assets);
    assets.remove(QString());
    return assets;
}

bool BoardDocument::hasPanel(int shot, int panel) const
{
    return shot >= 0 && shot < m_project.shots.size() && panel >= 0 && panel < m_project.shots[shot].panels.size();
}

BoardPanel const* BoardDocument::panel(int shot, int panel) const
{
    return hasPanel(shot, panel) ? &m_project.shots[shot].panels[panel] : nullptr;
}

bool BoardDocument::hasLayer(int shot, int panel, int layer) const
{
    return hasPanel(shot, panel) && layer >= 0 && layer < m_project.shots[shot].panels[panel].layers.size();
}

bool BoardDocument::fitsFrames(int frames, int replacing) const
{
    return frames >= 1 && frames <= 1000000 && canGrow(0, frames - replacing);
}

bool BoardDocument::appendPanelAllowed(int frames) const
{
    return frames >= 1 && frames <= 1000000 && canGrow(1, frames);
}

bool BoardDocument::canGrow(qsizetype panels, qint64 frames) const
{
    for (auto const& shot : m_project.shots) {
        panels += shot.panels.size();
        for (auto const& panel : shot.panels)
            frames += panel.length;
    }
    return panels <= 50000 && frames <= std::numeric_limits<int>::max();
}

void BoardDocument::apply(BoardProject project, QString const& label, quint64 session)
{
    m_undo.push(new BoardEdit(m_project, std::move(project), label, session));
}

bool BoardDocument::setTitles(QString const& title, QString const& subtitle)
{
    if (m_path.isEmpty() || title.size() > 4096 || subtitle.size() > 4096
        || (title == m_project.title && subtitle == m_project.subtitle))
        return false;
    auto next = m_project;
    next.title = title;
    next.subtitle = subtitle;
    apply(std::move(next), tr("Edit Project Titles"));
    return true;
}

bool BoardDocument::addShot(QString const& name, int frames, int after)
{
    if (m_path.isEmpty() || name.trimmed().isEmpty() || !appendPanelAllowed(frames))
        return false;
    auto next = m_project;
    int const index = after < 0 ? int(next.shots.size()) : std::min(after, int(next.shots.size()) - 1) + 1;
    next.shots.insert(index, {uniqueShotName(name.trimmed()), {}, {}, {BoardProject::blankPanel(frames)}});
    apply(std::move(next), tr("Add Scene"));
    return true;
}

bool BoardDocument::removeShot(int shot)
{
    if (m_project.shots.size() <= 1 || shot < 0 || shot >= m_project.shots.size())
        return false;
    auto next = m_project;
    next.shots.removeAt(shot);
    apply(std::move(next), tr("Remove Scene"));
    return true;
}

bool BoardDocument::addPanel(int shot, int frames, int after)
{
    if (shot < 0 || shot >= m_project.shots.size() || !appendPanelAllowed(frames))
        return false;
    auto next = m_project;
    auto& panels = next.shots[shot].panels;
    int const index = after < 0 ? int(panels.size()) : std::min(after, int(panels.size()) - 1) + 1;
    panels.insert(index, BoardProject::blankPanel(frames));
    apply(std::move(next), tr("Add Panel"));
    return true;
}

bool BoardDocument::removePanel(int shot, int panel)
{
    if (!hasPanel(shot, panel) || m_project.shots[shot].panels.size() <= 1)
        return false;
    auto next = m_project;
    next.shots[shot].panels.removeAt(panel);
    apply(std::move(next), tr("Remove Panel"));
    return true;
}

bool BoardDocument::setPanelLength(int shot, int panel, int frames)
{
    if (!hasPanel(shot, panel))
        return false;
    int const previous = m_project.shots[shot].panels[panel].length;
    if (frames == previous || !fitsFrames(frames, previous))
        return false;
    auto next = m_project;
    auto& edited = next.shots[shot].panels[panel];
    if (frames < previous)
        edited.camera = BoardCamera::slice(edited.camera, 0, frames);
    edited.length = frames;
    apply(std::move(next), tr("Change Panel Duration"));
    return true;
}
