// Eclipse - the Preview Manager view: the frames of the scene rendered once and kept, to play back smoothly.
#include "ui/views/PreviewManagerView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "ui/canvas/Canvas.h"

PreviewManagerView::PreviewManagerView(Document* document, Canvas* canvas, QWidget* parent) : QWidget(parent), m_document(document), m_canvas(canvas)
{
    m_size = new QComboBox(this);
    m_size->setObjectName(QStringLiteral("PreviewSize"));
    m_size->addItem(tr("Full size"), 100);
    m_size->addItem(tr("Half size"), 50);
    m_size->addItem(tr("Quarter size"), 25);
    m_size->setCurrentIndex(1);
    m_size->setToolTip(tr("How big the frames are kept. Smaller takes less memory and less time to render."));
    m_use = new QCheckBox(tr("Play from the preview"), this);
    m_use->setObjectName(QStringLiteral("PreviewUse"));
    m_use->setChecked(m_canvas->usesPreview());
    m_use->setToolTip(tr("While playing, the Camera view shows the frames kept here instead of drawing every layer again."));
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("PreviewStatus"));
    m_status->setWordWrap(true);
    QPushButton* const renderButton = new QPushButton(tr("Render Preview"), this);
    renderButton->setObjectName(QStringLiteral("PreviewRender"));
    QPushButton* const clearButton = new QPushButton(tr("Clear"), this);
    clearButton->setObjectName(QStringLiteral("PreviewClear"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(renderButton);
    buttons->addWidget(clearButton);
    buttons->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_size);
    layout->addWidget(m_use);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addStretch(1);
    connect(renderButton, &QPushButton::clicked, this, [this] { render(); });
    connect(clearButton, &QPushButton::clicked, this, &PreviewManagerView::clear);
    connect(m_use, &QCheckBox::toggled, m_canvas, &Canvas::setUsePreview);
    // A scene that has changed is not the scene that was rendered: the preview goes.
    connect(m_document, &Document::changed, this, [this] {
        if (m_rendering || m_canvas->previewFrames() == 0)
            return;
        m_canvas->setPreview({});
        showStatus(true);
    });
    showStatus();
}

int PreviewManagerView::render()
{
    Scene const scene = m_document->scene();
    double const scale = m_size->currentData().toInt() / 100.0;
    QSize const size(std::max(2, int(std::lround(scene.width * scale))), std::max(2, int(std::lround(scene.height * scale))));
    QVector<QImage> frames;
    frames.reserve(scene.frameCount());
    m_rendering = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int f = 0; f < scene.frameCount(); ++f) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(double(size.width()) / scene.width, double(size.height()) / scene.height);
        Canvas::paintFrame(painter, scene, f);
        painter.end();
        frames.push_back(image);
    }
    QApplication::restoreOverrideCursor();
    m_canvas->setPreview(frames);
    m_rendering = false;
    showStatus();
    return int(frames.size());
}

void PreviewManagerView::clear()
{
    m_canvas->setPreview({});
    showStatus();
}

void PreviewManagerView::showStatus(bool stale)
{
    int const frames = m_canvas->previewFrames();
    if (frames == 0) {
        m_status->setText(stale ? tr("The scene has changed since the preview was rendered: render it again.") : tr("No preview. Render Preview keeps every frame of the scene, to play back without drawing each one again."));
        return;
    }
    QSize const size = m_canvas->previewSize();
    double const megabytes = double(frames) * size.width() * size.height() * 4.0 / (1024.0 * 1024.0);
    m_status->setText(tr("%1 frames kept, %2 x %3 each: %4 MB.").arg(frames).arg(size.width()).arg(size.height()).arg(megabytes, 0, 'f', 1));
}
