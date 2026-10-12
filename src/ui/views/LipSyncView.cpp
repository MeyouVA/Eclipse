#include "ui/views/LipSyncView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "audio/lipsync/LipSync.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "model/scene/Scene.h"

// ---- the strip -------------------------------------------------------------------------------

LipSyncStrip::LipSyncStrip(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setMinimumHeight(72);
    connect(m_document, &Document::changed, this, qOverload<>(&QWidget::update));
}

void LipSyncStrip::setCell(int soundLayer, int frame)
{
    m_layer = soundLayer;
    m_frame = frame;
    update();
}

QRectF LipSyncStrip::frameRect(int frame) const
{
    int const frames = std::max(1, m_document->scene().frameCount());
    double const each = double(width()) / frames;
    return QRectF(frame * each, 0, each, height());
}

void LipSyncStrip::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter p(this);
    p.fillRect(rect(), QColor(46, 46, 46));
    if (m_layer < 0 || m_layer >= scene.layers.size() || scene.layers[m_layer].kind != Layer::Sound) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, tr("There is no sound layer in the scene. Import a sound first (File > Import > Sound)."));
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    int const frames = std::max(1, scene.frameCount());
    double const each = double(width()) / frames;
    int const lettersTop = height() - 22;
    // The frame the scene is on.
    p.fillRect(frameRect(m_frame), QColor(224, 36, 36, 70));
    // The waveform, over the frames the sound lasts.
    if (std::shared_ptr<SoundData> const sound = Sound::load(layer.soundFile)) {
        double const perPixel = double(SoundData::kRate) / scene.fps / each;
        int const mid = lettersTop / 2;
        p.setPen(QColor(120, 200, 150));
        for (int x = int(layer.soundStart * each); x < width(); ++x) {
            qint64 const first = qint64((x - layer.soundStart * each) * perPixel);
            if (first >= sound->frames())
                break;
            int const half = int(sound->peak(first, qint64(perPixel) + 1) * (mid - 3) + 0.5);
            p.drawLine(x, mid - half, x, mid + half);
        }
    } else {
        p.setPen(QColor(220, 120, 100));
        p.drawText(QRect(6, 0, width() - 12, lettersTop), Qt::AlignVCenter | Qt::AlignLeft, tr("sound file missing"));
    }
    // The shapes, a letter a frame; lines between where there is room.
    p.fillRect(0, lettersTop, width(), height() - lettersTop, QColor(58, 58, 58));
    QFont small = font();
    small.setPointSizeF(std::max(6.5, std::min(small.pointSizeF(), each * 0.7)));
    small.setBold(true);
    p.setFont(small);
    for (int f = 0; f < frames; ++f) {
        QRectF const cell(f * each, lettersTop, each, height() - lettersTop);
        if (each >= 6.0) {
            p.setPen(QColor(40, 40, 40));
            p.drawLine(QPointF(cell.right(), 0), QPointF(cell.right(), height()));
        }
        QChar const shape = layer.mouthAt(f);
        if (shape.isNull())
            continue;
        // The start of a run of one shape stands out; where there is no room, only that is lettered.
        bool const starts = f == 0 || layer.mouthAt(f - 1) != shape;
        if (each < 9.0 && !starts)
            continue;
        p.setPen(shape == QLatin1Char('X') ? QColor(130, 130, 130) : starts ? QColor(255, 220, 150) : QColor(205, 205, 205));
        p.drawText(cell.adjusted(-6, 0, 6, 0), Qt::AlignCenter, QString(shape));
    }
}

void LipSyncStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    int const frames = std::max(1, m_document->scene().frameCount());
    emit framePicked(std::clamp(int(event->position().x() * frames / std::max(1, width())), 0, frames - 1));
}

void LipSyncStrip::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    int const frames = std::max(1, m_document->scene().frameCount());
    emit framePicked(std::clamp(int(event->position().x() * frames / std::max(1, width())), 0, frames - 1));
}

// ---- the view --------------------------------------------------------------------------------

int LipSyncView::soundLayerFor(Scene const& scene, int current)
{
    if (current >= 0 && current < scene.layers.size() && scene.layers[current].kind == Layer::Sound)
        return current;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l)
        if (scene.layers[l].kind == Layer::Sound)
            return l;
    return -1;
}

void LipSyncView::paintMouth(QPainter& painter, QRectF const& box, QChar shape)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPointF const c = box.center();
    double const u = std::min(box.width(), box.height()) / 2.0;     // half the room there is
    QColor const lip(196, 92, 84), dark(70, 28, 30), teeth(244, 240, 232), tongue(226, 128, 128);
    QPen const outline(dark, std::max(1.0, u * 0.09), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(outline);
    // An open mouth: the dark of it, with teeth above and perhaps the tongue below.
    auto open = [&](double halfWide, double halfHigh, bool showTeeth, bool showTongue) {
        QPainterPath mouth;
        mouth.addEllipse(c, u * halfWide, u * halfHigh);
        painter.setBrush(dark);
        painter.drawPath(mouth);
        painter.save();
        painter.setClipPath(mouth);
        painter.setPen(Qt::NoPen);
        if (showTeeth) {
            painter.setBrush(teeth);
            painter.drawRect(QRectF(c.x() - u * halfWide, c.y() - u * halfHigh, u * halfWide * 2, u * halfHigh * 0.55));
        }
        if (showTongue) {
            painter.setBrush(tongue);
            painter.drawEllipse(QPointF(c.x(), c.y() + u * halfHigh * 0.95), u * halfWide * 0.7, u * halfHigh * 0.6);
        }
        painter.restore();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(lip, std::max(1.2, u * 0.16)));
        painter.drawPath(mouth);
    };
    switch (LipSync::indexOf(shape)) {
    case 0: {   // A: lips pressed together
        painter.setPen(QPen(lip, std::max(1.5, u * 0.22), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(c + QPointF(-u * 0.62, 0), c + QPointF(u * 0.62, 0));
        painter.setPen(QPen(dark, std::max(1.0, u * 0.07), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(c + QPointF(-u * 0.6, 0), c + QPointF(u * 0.6, 0));
        break;
    }
    case 1: {   // B: a little open, the teeth together
        QRectF const gap(c.x() - u * 0.66, c.y() - u * 0.2, u * 1.32, u * 0.4);
        painter.setBrush(teeth);
        painter.setPen(QPen(lip, std::max(1.2, u * 0.16)));
        painter.drawRoundedRect(gap, u * 0.2, u * 0.2);
        painter.setPen(QPen(QColor(150, 140, 130), std::max(0.8, u * 0.05)));
        painter.drawLine(QPointF(gap.left() + u * 0.1, c.y()), QPointF(gap.right() - u * 0.1, c.y()));
        break;
    }
    case 2: open(0.62, 0.36, true, false); break;       // C: open
    case 3: open(0.66, 0.62, true, true); break;        // D: wide open
    case 4: open(0.42, 0.48, false, false); break;      // E: round
    case 5: open(0.22, 0.24, false, false); break;      // F: small and pushed out
    case 6: {   // G: the top teeth resting on the lower lip
        painter.setPen(QPen(lip, std::max(1.5, u * 0.2), Qt::SolidLine, Qt::RoundCap));
        QPainterPath lower;
        lower.moveTo(c + QPointF(-u * 0.6, u * 0.05));
        lower.quadTo(c + QPointF(0, u * 0.42), c + QPointF(u * 0.6, u * 0.05));
        painter.drawPath(lower);
        painter.setPen(QPen(QColor(150, 140, 130), std::max(0.8, u * 0.05)));
        painter.setBrush(teeth);
        painter.drawRect(QRectF(c.x() - u * 0.36, c.y() - u * 0.22, u * 0.72, u * 0.3));
        break;
    }
    case 7: {   // X: at rest - a quiet line
        painter.setPen(QPen(lip, std::max(1.2, u * 0.14), Qt::SolidLine, Qt::RoundCap));
        QPainterPath rest;
        rest.moveTo(c + QPointF(-u * 0.48, -u * 0.03));
        rest.quadTo(c + QPointF(0, u * 0.12), c + QPointF(u * 0.48, -u * 0.03));
        painter.drawPath(rest);
        break;
    }
    default: break;
    }
    painter.restore();
}

QIcon LipSyncView::mouthIcon(QChar shape, int size)
{
    QPixmap picture(size, size);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    paintMouth(painter, QRectF(2, 2, size - 4, size - 4), shape);
    painter.end();
    QIcon icon;
    icon.addPixmap(picture, QIcon::Normal);
    icon.addPixmap(picture, QIcon::Selected);
    icon.addPixmap(picture, QIcon::Active);
    return icon;
}

LipSyncView::LipSyncView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("LipSyncTitle"));
    m_strip = new LipSyncStrip(document, this);
    m_strip->setObjectName(QStringLiteral("LipSyncStrip"));
    connect(m_strip, &LipSyncStrip::framePicked, this, &LipSyncView::framePicked);

    // The chart: the eight shapes; a press gives the frame the scene is on that shape.
    QHBoxLayout* const chart = new QHBoxLayout;
    chart->setSpacing(2);
    for (int i = 0; i < LipSync::kShapeCount; ++i) {
        QChar const shape = QLatin1Char(LipSync::kShapes[i]);
        QToolButton* const button = new QToolButton(this);
        button->setObjectName(QStringLiteral("Mouth") + shape);
        button->setIcon(mouthIcon(shape, 40));
        button->setIconSize(QSize(40, 40));
        button->setText(QString(shape));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setCheckable(true);
        button->setAutoExclusive(false);
        button->setToolTip(LipSync::describe(shape));
        connect(button, &QToolButton::clicked, this, [this, shape] { emit mouthChosen(QString(shape)); });
        chart->addWidget(button);
        m_mouths.push_back(button);
    }
    chart->addStretch(1);

    QPushButton* const detect = new QPushButton(tr("Detect"), this);
    detect->setObjectName(QStringLiteral("LipSyncDetect"));
    detect->setToolTip(tr("Listens to the sound and gives every frame of it a mouth shape."));
    QPushButton* const map = new QPushButton(tr("Map..."), this);
    map->setObjectName(QStringLiteral("LipSyncMap"));
    map->setToolTip(tr("Puts the drawing that goes with each shape on the frames of a drawing layer."));
    QPushButton* const clear = new QPushButton(tr("Clear"), this);
    clear->setObjectName(QStringLiteral("LipSyncClear"));
    connect(detect, &QPushButton::clicked, this, &LipSyncView::detectRequested);
    connect(map, &QPushButton::clicked, this, &LipSyncView::mapRequested);
    connect(clear, &QPushButton::clicked, this, &LipSyncView::clearRequested);
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(m_title, 1);
    buttons->addWidget(detect);
    buttons->addWidget(map);
    buttons->addWidget(clear);

    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(buttons);
    layout->addWidget(m_strip, 1);
    layout->addLayout(chart);
    connect(m_document, &Document::changed, this, &LipSyncView::refresh);
    refresh();
}

void LipSyncView::setCurrentCell(int layer, int frame)
{
    m_layer = layer;
    m_frame = frame;
    refresh();
}

void LipSyncView::refresh()
{
    Scene const& scene = m_document->scene();
    m_soundLayer = soundLayerFor(scene, m_layer);
    m_strip->setCell(m_soundLayer, m_frame);
    QChar const now = m_soundLayer >= 0 ? scene.layers[m_soundLayer].mouthAt(m_frame) : QChar();
    for (int i = 0; i < m_mouths.size(); ++i) {
        m_mouths[i]->setEnabled(m_soundLayer >= 0);
        m_mouths[i]->setChecked(!now.isNull() && LipSync::indexOf(now) == i);
    }
    if (m_soundLayer < 0)
        m_title->setText(tr("No sound layer"));
    else
        m_title->setText(now.isNull() ? tr("%1 - frame %2").arg(scene.layers[m_soundLayer].name).arg(m_frame + 1)
                                      : tr("%1 - frame %2: %3").arg(scene.layers[m_soundLayer].name).arg(m_frame + 1).arg(LipSync::describe(now)));
}
