#include "board/model/BoardProject.h"

#include <QDir>
#include <QFileInfo>

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

QString BoardProject::audioPath(QString const& projectFile) const
{
    if (sound.asset.isEmpty())
        return {};
    return QFileInfo(projectFile).dir().absoluteFilePath(QStringLiteral("audio/") + sound.asset);
}
