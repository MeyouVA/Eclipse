#pragma once

#include "EclipseExport.h"
#include "board/model/BoardContent.h"

struct ECLIPSE_BOARD_MODEL_API BoardProject {
    enum class SceneGroup { Sequence, Act };

    QString name;
    QString title;
    QString subtitle;
    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    QVector<BoardShot> shots = {};
    QStringList captionFields = Storyboard::defaultCaptionFields();
    BoardSound sound = {};
    QVector<BoardLayer> library = {};
    static constexpr qsizetype maxLibraryItems = 512;
    static constexpr qsizetype maxLibraryName = 128;

    QString create(QString const& directory, QString* error = nullptr) const;
    bool save(QString const& path, QString* error = nullptr) const;
    static bool open(QString const& path, BoardProject& project, QString* error = nullptr);
    QByteArray toBytes(QString* error = nullptr) const;
    static bool fromBytes(QByteArray const& bytes, BoardProject& project, QString* error = nullptr);
    Storyboard timeline() const;
    QVector<BoardSpan> spans() const;
    QPair<int, int> groupRange(int scene, SceneGroup group) const;
    QString audioPath(QString const& projectFile) const;
    static BoardPanel blankPanel(int frames);
    static bool validName(QString const& name);
};
