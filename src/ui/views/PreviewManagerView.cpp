// Eclipse - the Preview Manager view: the frames of the scene rendered once and kept, to play back smoothly.
#include "ui/views/PreviewManagerView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "canvas/Canvas.h"

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
    m_autoTimer = new QTimer(this);
    m_autoTimer->setSingleShot(true);
    m_autoTimer->setInterval(600);
    connect(m_autoTimer, &QTimer::timeout, this, [this] {
        if (m_auto && !m_rendering)
            render();
    });
    connect(m_document, &Document::changed, this, [this] {
        if (m_auto) {
            m_autoTimer->start();
            return;
        }
        if (m_rendering || m_canvas->previewFrames() == 0)
            return;
        m_canvas->setPreview({});
        showStatus(true);
    });
    showStatus();
}

quint64 PreviewManagerView::fingerprint(Scene const& scene, int frame)
{
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](void const* at, size_t bytes) {
        unsigned char const* p = static_cast<unsigned char const*>(at);
        for (size_t i = 0; i < bytes; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    auto number = [&mix](double v) { mix(&v, sizeof v); };
    auto whole = [&mix](qint64 v) { mix(&v, sizeof v); };
    auto pointer = [&whole](void const* p) { whole(qint64(reinterpret_cast<quintptr>(p))); };
    auto matrix = [&number](QTransform const& t) {
        for (double v : {t.m11(), t.m12(), t.m13(), t.m21(), t.m22(), t.m23(), t.m31(), t.m32(), t.m33()})
            number(v);
    };
    auto pose = [&](Pose const& p) {
        for (double v : {p.x, p.y, p.scaleX, p.scaleY, p.rotation, p.skew, p.z, p.rotX, p.rotY, p.fov, p.opacity})
            number(v);
        for (double v : p.fx)
            number(v);
        for (double v : p.bend)
            number(v);
        for (QPointF const& q : p.warp) {
            number(q.x());
            number(q.y());
        }
        for (QPointF const& q : p.quad) {
            number(q.x());
            number(q.y());
        }
        for (PointMove const& point : p.points) {
            whole(point.drawing); whole(point.sheet); whole(point.fill); whole(point.line); whole(point.point);
            for (QPointF const& q : {point.by, point.in, point.out}) {
                number(q.x()); number(q.y());
            }
        }
    };
    auto drawing = [&](Drawing const& d) {
        // (A drawing that was not changed keeps the very lists it had: the same places in memory.)
        pointer(d.pictures.constData());
        whole(d.pictures.size());
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            pointer(d.fillsOf(sheet).constData());
            whole(d.fillsOf(sheet).size());
            pointer(d.strokesOf(sheet).constData());
            whole(d.strokesOf(sheet).size());
        }
    };
    // What all frames have in common: the size, the colours, the symbols, the camera.
    number(scene.width);
    number(scene.height);
    whole(scene.background.rgba());
    whole(scene.orthographic ? 1 : 0);
    pointer(scene.palette.constData());
    whole(scene.palette.size());
    pointer(scene.symbols.constData());
    whole(scene.symbols.size());
    pointer(scene.graph.nodes.constData());
    pointer(scene.graph.cables.constData());
    for (GraphNode const& node : scene.graph.nodes)
        number(node.amountAt(frame));
    matrix(scene.cameraTransform(frame));
    number(scene.focalDistance(frame));
    if (int const camera = scene.cameraIndex(); camera >= 0) {
        number(scene.layers[camera].nearPlane);
        number(scene.layers[camera].farPlane);
    }
    for (int l : scene.drawOrder(frame)) {
        Layer const& layer = scene.layers[l];
        // A drawing layer with nothing on this frame shows nothing, wherever it is.
        if (layer.holdsDrawings() && !layer.showsSymbol() && layer.drawingAt(frame) == Layer::kNoDrawing)
            continue;
        whole(layer.id);
        whole(layer.kind);
        whole(scene.enabled(l) ? 1 : 0);
        number(layer.opacity);
        whole(layer.hiddenSheets);
        whole(layer.cardColor.rgba());
        matrix(scene.worldTransform(l, frame));
        pose(scene.poseOf(l, frame));
        pointer(layer.bones.constData());
        pointer(layer.warp.constData());
        pointer(layer.effects.constData());
        whole(layer.effects.size());
        whole(layer.symbolId);
        int const index = layer.drawingAt(frame);
        whole(index);
        if (layer.morphing(frame)) {
            pointer(layer.morphs.constData());
            whole(frame);
        }
        if (index >= 0 && index < layer.drawings.size())
            drawing(layer.drawings[index]);
        // A cutter: the drawing of its matte counts too.
        for (Effect const& e : layer.effects) {
            int const m = e.type == Effect::Cutter ? scene.layerIndex(e.matte) : -1;
            if (m < 0)
                continue;
            matrix(scene.worldTransform(m, frame));
            int const mi = scene.layers[m].drawingAt(frame);
            whole(mi);
            if (mi >= 0 && mi < scene.layers[m].drawings.size())
                drawing(scene.layers[m].drawings[mi]);
        }
    }
    return h;
}

void PreviewManagerView::setSizePercent(int percent)
{
    int const at = m_size->findData(percent);
    if (at >= 0)
        m_size->setCurrentIndex(at);
}

int PreviewManagerView::sizePercent() const
{
    return m_size->currentData().toInt();
}

void PreviewManagerView::setAutoRender(bool on)
{
    m_auto = on;
    if (on && !m_rendering)
        render();
}

int PreviewManagerView::render(bool everything)
{
    if (m_rendering)
        return 0;
    Scene const scene = m_document->scene();
    double const scale = m_size->currentData().toInt() / 100.0;
    QSize const size(std::max(2, int(std::lround(scene.width * scale))), std::max(2, int(std::lround(scene.height * scale))));
    // What was kept before, where it is still the picture of that frame.
    bool const reuse = !everything && m_frames.size() == m_prints.size() && !m_frames.isEmpty() && m_frames.first().size() == size;
    QVector<QImage> frames;
    QVector<quint64> prints;
    frames.reserve(scene.frameCount());
    m_rendering = true;
    m_cancel = false;
    int made = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int f = 0; f < scene.frameCount(); ++f) {
        quint64 const print = fingerprint(scene, f);
        prints.push_back(print);
        if (reuse && f < m_prints.size() && m_prints[f] == print) {
            frames.push_back(m_frames[f]);
            continue;
        }
        // (Now and then the window gets a word in: Cancel Preview Render is taken notice of.)
        if (made % 4 == 3)
            QApplication::processEvents();
        if (m_cancel)
            break;
        ++made;
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
    m_rendering = false;
    if (m_cancel) {
        // What was rendered is kept for next time; the preview is not complete, so it is not played from.
        m_frames = frames;
        m_prints = prints.mid(0, frames.size());
        m_cancel = false;
        showStatus(true);
        return -1;
    }
    m_frames = frames;
    m_prints = prints;
    m_sound = Sound::mix(scene);
    m_canvas->setPreview(frames);
    showStatus();
    return made;
}

void PreviewManagerView::clear()
{
    m_frames.clear();
    m_prints.clear();
    m_sound.clear();
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
