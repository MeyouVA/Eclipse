// Eclipse - Batch Rendering (see RenderQueue.h).
#include "ui/io/RenderQueue.h"

#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

#include "canvas/FrameRender.h"
#include "model/scene/Scene.h"

QVector<RenderQueue::Job> RenderQueue::load()
{
    QVector<Job> out;
    QByteArray const saved = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("renderQueue")).toByteArray();
    for (QJsonValue const& v : QJsonDocument::fromJson(saved).array()) {
        QJsonObject const o = v.toObject();
        Job j;
        j.scene = o["scene"].toString();
        j.first = o["first"].toInt(1);
        j.last = o["last"].toInt();
        j.folder = o["folder"].toString();
        j.format = o["format"].toString(QStringLiteral("png"));
        j.priority = o["priority"].toInt(50);
        j.width = o["width"].toInt();
        j.height = o["height"].toInt();
        j.status = o["status"].toString();
        out.push_back(j);
    }
    return out;
}

void RenderQueue::save(QVector<Job> const& jobs)
{
    QJsonArray list;
    for (Job const& j : jobs)
        list.append(QJsonObject{{"scene", j.scene}, {"first", j.first}, {"last", j.last}, {"folder", j.folder}, {"format", j.format}, {"priority", j.priority},
                                {"width", j.width}, {"height", j.height}, {"status", j.status}});
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("renderQueue"), QJsonDocument(list).toJson(QJsonDocument::Compact));
}

int RenderQueue::run(QVector<Job>& jobs, std::function<bool(int job, int frame, int frames)> const& progress)
{
    QVector<int> order(jobs.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return jobs[a].priority > jobs[b].priority; });
    int done = 0;
    for (int index : order) {
        Job& job = jobs[index];
        if (job.status == QObject::tr("Done"))
            continue;
        Scene scene;
        QString error;
        if (!Scene::load(job.scene, scene, &error)) {
            job.status = QObject::tr("Failed: %1").arg(error);
            continue;
        }
        int const first = std::clamp(job.first, 1, std::max(1, scene.frameCount()));
        int const last = job.last <= 0 ? scene.frameCount() : std::clamp(job.last, first, std::max(1, scene.frameCount()));
        QSize const size(job.width > 0 ? job.width : int(scene.width), job.height > 0 ? job.height : int(scene.height));
        QDir().mkpath(job.folder);
        QString const name = QFileInfo(job.scene).completeBaseName();
        bool stopped = false, written = true;
        for (int f = first; f <= last && written; ++f) {
            if (progress && !progress(index, f - first, last - first + 1)) {
                stopped = true;
                break;
            }
            QImage const image = FrameRender::frame(scene, f - 1, size);
            written = image.save(QDir(job.folder).filePath(QStringLiteral("%1-%2.%3").arg(name).arg(f, 4, 10, QLatin1Char('0')).arg(job.format)));
        }
        if (stopped) {
            job.status = QObject::tr("Stopped");
            break;
        }
        job.status = written ? QObject::tr("Done") : QObject::tr("Failed: a picture could not be written to %1").arg(QDir::toNativeSeparators(job.folder));
        done += written;
    }
    return done;
}

RenderQueue::RenderQueue(QWidget* parent) : QDialog(parent), m_jobs(load())
{
    setObjectName(QStringLiteral("RenderQueue"));
    setWindowTitle(tr("Batch Rendering"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 6, this);
    m_table->setObjectName(QStringLiteral("RenderQueueTable"));
    m_table->setHorizontalHeaderLabels({tr("Scene"), tr("Frames"), tr("Output"), tr("Format"), tr("Priority"), tr("Status")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
    QHBoxLayout* const buttons = new QHBoxLayout;
    QPushButton* const add = new QPushButton(tr("Add Scene..."), this);
    QPushButton* const priority = new QPushButton(tr("Change Priority..."), this);
    priority->setObjectName(QStringLiteral("RenderQueuePriority"));
    QPushButton* const remove = new QPushButton(tr("Remove"), this);
    QPushButton* const clear = new QPushButton(tr("Clear Done"), this);
    QPushButton* const run = new QPushButton(tr("Render Queue"), this);
    run->setObjectName(QStringLiteral("RenderQueueRun"));
    QPushButton* const close = new QPushButton(tr("Close"), this);
    for (QPushButton* b : {add, priority, remove, clear})
        buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(run);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        QString const file = QFileDialog::getOpenFileName(this, tr("Add Scene"), QString(), tr("Eclipse scenes (*.eclipse)"));
        if (file.isEmpty())
            return;
        QString const folder = QFileDialog::getExistingDirectory(this, tr("Output"), QFileInfo(file).path());
        if (folder.isEmpty())
            return;
        Job job;
        job.scene = file;
        job.folder = folder;
        addJob(job);
    });
    connect(priority, &QPushButton::clicked, this, [this] {
        int const row = m_table->currentRow();
        if (row < 0)
            return;
        // Choose New Priority: a slider from low to high.
        QDialog ask(this);
        ask.setWindowTitle(tr("Choose New Priority"));
        QVBoxLayout* const l = new QVBoxLayout(&ask);
        l->addWidget(new QLabel(tr("New Priority"), &ask));
        QSlider* const slider = new QSlider(Qt::Horizontal, &ask);
        slider->setObjectName(QStringLiteral("PrioritySlider"));
        slider->setRange(0, 100);
        slider->setValue(m_jobs[row].priority);
        l->addWidget(slider);
        QDialogButtonBox* const b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
        connect(b, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
        l->addWidget(b);
        if (ask.exec() == QDialog::Accepted)
            setPriority(row, slider->value());
    });
    connect(remove, &QPushButton::clicked, this, [this] { removeJob(m_table->currentRow()); });
    connect(clear, &QPushButton::clicked, this, [this] {
        m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [](Job const& j) { return j.status == tr("Done"); }), m_jobs.end());
        save(m_jobs);
        fill();
    });
    connect(run, &QPushButton::clicked, this, &RenderQueue::runQueue);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    resize(860, 380);
    fill();
}

void RenderQueue::fill()
{
    m_table->setRowCount(0);
    for (Job const& j : m_jobs) {
        int const row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(QFileInfo(j.scene).fileName()));
        m_table->item(row, 0)->setToolTip(QDir::toNativeSeparators(j.scene));
        m_table->setItem(row, 1, new QTableWidgetItem(j.last > 0 ? tr("%1 - %2").arg(j.first).arg(j.last) : tr("%1 - end").arg(j.first)));
        m_table->setItem(row, 2, new QTableWidgetItem(QDir::toNativeSeparators(j.folder)));
        m_table->setItem(row, 3, new QTableWidgetItem(j.format.toUpper()));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(j.priority)));
        m_table->setItem(row, 5, new QTableWidgetItem(j.status.isEmpty() ? tr("Waiting") : j.status));
    }
}

void RenderQueue::addJob(Job const& job)
{
    m_jobs.push_back(job);
    save(m_jobs);
    fill();
}

void RenderQueue::setPriority(int row, int priority)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    m_jobs[row].priority = std::clamp(priority, 0, 100);
    save(m_jobs);
    fill();
}

void RenderQueue::removeJob(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    m_jobs.removeAt(row);
    save(m_jobs);
    fill();
}

void RenderQueue::runQueue()
{
    QProgressDialog progress(tr("Batch Rendering"), tr("Stop"), 0, 1000, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    run(m_jobs, [&](int job, int frame, int frames) {
        progress.setLabelText(tr("Rendering %1: frame %2 of %3").arg(QFileInfo(m_jobs[job].scene).fileName()).arg(frame + 1).arg(frames));
        progress.setValue(std::min(999, frame * 1000 / std::max(1, frames)));
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.reset();
    save(m_jobs);
    fill();
}
