#pragma once

#include "audio/sound/SoundPlayer.h"

#include <QElapsedTimer>
#include <QTimer>
#include <memory>

class BoardDocument;
struct SoundData;

class BoardPlayback : public QObject {
    Q_OBJECT
public:
    explicit BoardPlayback(BoardDocument* document, QObject* parent = nullptr);
    void setLength(int length);
    void seek(int frame, bool notify = true);
    void toggle();
    void stop();
    void setLoop(bool loop) { m_loop = loop; }
    bool playing() const { return m_timer.isActive(); }
    int frame() const { return m_frame; }

signals:
    void frameChanged(int frame);
    void playingChanged(bool playing);
    void message(QString const& text);

private:
    void advance();
    void startSound(int frame);
    BoardDocument* m_document;
    QTimer m_timer;
    QElapsedTimer m_clock;
    SoundPlayer m_player;
    std::shared_ptr<SoundData> m_sound;
    int m_length = 0;
    int m_frame = 0;
    int m_origin = 0;
    qint64 m_cycle = 0;
    bool m_loop = false;
    bool m_soundStarted = false;
};
