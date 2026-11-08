#include "board/model/BoardProject.h"

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
        for (BoardPanel const& panel : shot.panels)
            timed.panels.push_back(panel);
        board.shots.push_back(std::move(timed));
    }
    return board;
}
