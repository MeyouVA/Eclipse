#pragma once

#include "board/model/BoardContent.h"

#include <QAbstractScrollArea>

struct BoardProject;

class BoardTimeline : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit BoardTimeline(QWidget* parent = nullptr);
    void setProject(BoardProject const& project);
    void setFrame(int frame, bool follow);
    void zoom(int direction);
    int length() const { return m_length; }
    int frame() const { return m_frame; }
    BoardSpan const* spanAt(int frame) const;
    int panelStart(int shot, int panel) const;

signals:
    void seek(int frame);
    void durationChanged(int shot, int panel, int frames);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    double xAt(qint64 frame) const;
    int frameAt(double x) const;
    int visibleFrames() const;
    void updateScrollBar();
    QVector<BoardSpan> m_spans;
    QStringList m_panelLabels;
    QVector<int> m_dissolves;
    QVector<int> m_cameraKeys;
    BoardSound m_sound;
    int m_frame = 0;
    int m_length = 0;
    double m_scale = 5.0;
    int m_resizeStart = 0;
    int m_resizeLength = 0;
    QPair<int, int> m_resizing{-1, -1};
};
