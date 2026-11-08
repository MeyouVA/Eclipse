// Eclipse - Batch Rendering: scenes queued to be rendered one after another (the highest priority first), each to a
// folder of numbered pictures; the queue is kept between sessions.
#pragma once

#include "EclipseExport.h"

#include <QDialog>
#include <QVector>

#include <functional>

class QTableWidget;

class ECLIPSE_UI_API RenderQueue : public QDialog {
    Q_OBJECT

public:
    struct Job {
        QString scene;          // the scene file
        int first = 1, last = 0;    // frames (last 0: to the end)
        QString folder;         // where the pictures go
        QString format = QStringLiteral("png");
        int priority = 50;      // 0..100, higher first
        int width = 0, height = 0;  // 0: the scene's own size
        QString status;
    };
    explicit RenderQueue(QWidget* parent = nullptr);

    static QVector<Job> load();
    static void save(QVector<Job> const& jobs);
    // Every job not yet done rendered, the highest priority first; `progress` is told each frame (false from it: stop).
    // How many jobs were done.
    static int run(QVector<Job>& jobs, std::function<bool(int job, int frame, int frames)> const& progress = {});

    QVector<Job> const& jobs() const { return m_jobs; }
    void addJob(Job const& job);
    void setPriority(int row, int priority);
    void removeJob(int row);

public slots:
    void runQueue();

private:
    void fill();
    QVector<Job> m_jobs;
    QTableWidget* m_table;
};
