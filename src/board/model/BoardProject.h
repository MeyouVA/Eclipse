#pragma once

#include "EclipseExport.h"
#include "board/model/BoardContent.h"

struct ECLIPSE_BOARD_MODEL_API BoardProject {
    QString name;
    QString title;
    QString subtitle;
    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    QVector<BoardShot> shots = {};
    QStringList captionFields = Storyboard::defaultCaptionFields();

    QString create(QString const& directory, QString* error = nullptr) const;
    bool save(QString const& path, QString* error = nullptr) const;
    static bool open(QString const& path, BoardProject& project, QString* error = nullptr);
    Storyboard timeline() const;
    static BoardPanel blankPanel(int frames);
};
