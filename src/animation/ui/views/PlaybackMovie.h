#pragma once
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <functional>
#include <memory>

struct PlaybackMovie {
    QString source;
    std::unique_ptr<QTemporaryDir> folder;
    QVector<qint16> audio;
    double rate=24;
    static std::unique_ptr<PlaybackMovie> decode(QString const& path,std::function<bool()> const& keepGoing,QString* error);
};
