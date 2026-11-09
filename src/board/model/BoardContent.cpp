#include "board/model/BoardProject.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

BoardSound BoardSound::within(int first, int end) const
{
    int const begin = std::max(first, start);
    int const finish = std::min(end, start + length);
    if (asset.isEmpty() || finish <= begin)
        return {};
    auto clipped = *this;
    clipped.start = begin - first;
    clipped.sourceOffset += begin - start;
    clipped.length = finish - begin;
    return clipped;
}

BoardPanel BoardProject::blankPanel(int frames)
{
    BoardPanel panel;
    panel.length = frames;
    panel.layers = {{QStringLiteral("A"), BoardLayer::Kind::Vector, true, {}},
                    {QStringLiteral("BG"), BoardLayer::Kind::Vector, true, {}}};
    return panel;
}

Storyboard BoardProject::timeline() const
{
    Storyboard board;
    board.title = title;
    board.subtitle = subtitle;
    board.captionFields = captionFields;
    for (BoardShot const& shot : shots) {
        Storyboard::Shot timed;
        timed.name = shot.name;
        timed.sequence = shot.sequence;
        timed.act = shot.act;
        if (shot.dissolve > 0) {
            timed.transition = QStringLiteral("Dissolve");
            timed.transitionLength = shot.dissolve;
        }
        for (BoardPanel const& panel : shot.panels)
            timed.panels.push_back(panel);
        board.shots.push_back(std::move(timed));
    }
    return board;
}

QVector<BoardSpan> BoardProject::spans() const
{
    QVector<BoardSpan> result;
    int frame = 0;
    for (int s = 0; s < shots.size(); ++s)
        for (int p = 0; p < shots[s].panels.size(); ++p) {
            int const length = shots[s].panels[p].length;
            result.push_back({s, p, frame, length});
            frame += length;
        }
    return result;
}

QPair<int, int> BoardProject::groupRange(int scene, SceneGroup group) const
{
    if (scene < 0 || scene >= shots.size())
        return {-1, -1};
    auto const& current = shots[scene];
    auto sameGroup = [&](int index) {
        auto const& other = shots[index];
        return current.act.compare(other.act, Qt::CaseInsensitive) == 0
            && (group == SceneGroup::Act || current.sequence.compare(other.sequence, Qt::CaseInsensitive) == 0);
    };
    int first = scene;
    int end = scene + 1;
    while (first > 0 && sameGroup(first - 1))
        --first;
    while (end < shots.size() && sameGroup(end))
        ++end;
    return {first, end};
}

QString BoardProject::audioPath(QString const& projectFile) const
{
    if (sound.asset.isEmpty())
        return {};
    return QFileInfo(projectFile).dir().absoluteFilePath(QStringLiteral("audio/") + sound.asset);
}
