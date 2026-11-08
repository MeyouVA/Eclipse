// Eclipse - the Sound Editor: a sound layer's clips on its waveform, their times, loops, volume and envelope, the layer's effects, lip-sync.
#include "animation/ui/views/SoundEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"

namespace {

// The frame after a clip's last, on its layer's frames.
int clipEnd(SoundClip const& clip, double fps)
{
    if (clip.stop >= 0)
        return clip.stop;
    double seconds = clip.length;
    if (seconds < 0.0) {
        std::shared_ptr<SoundData> const data = Sound::load(clip.file);
        seconds = data ? std::max(0.0, data->seconds() - clip.offset) : 0.0;
    }
    return clip.start + int(std::ceil(seconds * std::max(1, clip.loops) * fps));
}

}  // namespace

// The waveform of a sound layer's track, its clips as boxes, the chosen one's envelope as a line of points to drag.
class SoundWaveView : public QWidget {
public:
    SoundWaveView(Document* document, QWidget* parent)
        : QWidget(parent), m_document(document)
    {
        setObjectName(QStringLiteral("SoundWave"));
        setMouseTracking(true);
        setMinimumHeight(150);
    }

    int layer = -1;
    int clip = -1;
    int frame = 0;
    double zoom = 8.0;      // pixels a frame
    std::function<void(int)> clipPicked;

    static constexpr int kRuler = 18;
    double xOf(double f) const { return 4.0 + f * zoom; }
    double frameAt(double x) const { return (x - 4.0) / zoom; }
    double yOfGain(double gain) const { return height() - 4.0 - std::clamp(gain, 0.0, 2.0) / 2.0 * (height() - kRuler - 8.0); }
    double gainAt(double y) const { return std::clamp((height() - 4.0 - y) / (height() - kRuler - 8.0) * 2.0, 0.0, 2.0); }

    QVector<SoundClip> clips() const
    {
        Scene const& scene = m_document->scene();
        return layer >= 0 && layer < scene.layers.size() ? scene.layers[layer].clipsOfSound() : QVector<SoundClip>();
    }

    void fitWidth()
    {
        double const fps = m_document->scene().fps;
        int end = m_document->scene().frameCount();
        for (SoundClip const& c : clips())
            end = std::max(end, clipEnd(c, fps));
        setMinimumWidth(int(xOf(end) + 20));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(38, 40, 44));
        Scene const& scene = m_document->scene();
        if (layer < 0 || layer >= scene.layers.size())
            return;
        double const fps = scene.fps;
        QVector<SoundClip> const all = clips();
        // The clips, the chosen one lighter.
        for (int c = 0; c < all.size(); ++c) {
            QRectF const box(xOf(all[c].start), kRuler, xOf(clipEnd(all[c], fps)) - xOf(all[c].start), height() - kRuler - 1);
            p.fillRect(box, c == clip ? QColor(70, 96, 120) : QColor(c % 2 ? 56 : 62, 66, 74));
            p.setPen(QColor(150, 160, 175));
            p.drawRect(box);
            p.drawText(box.adjusted(4, 2, -4, 0), Qt::AlignLeft | Qt::AlignTop, QFileInfo(all[c].file).fileName());
        }
        // The waveform of the whole track, effects and all.
        if (std::shared_ptr<SoundData> const track = Sound::track(scene.layers[layer], fps)) {
            double const perPixel = SoundData::kRate / fps / zoom;
            double const mid = (kRuler + height()) / 2.0, half = (height() - kRuler) / 2.0 - 4.0;
            p.setPen(QColor(120, 200, 150));
            for (int x = 4; x < width(); ++x) {
                qint64 const first = qint64(frameAt(x) * SoundData::kRate / fps);
                if (first >= track->frames())
                    break;
                double const h = track->peak(first, qint64(perPixel) + 1) * half;
                p.drawLine(QPointF(x, mid - h), QPointF(x, mid + h));
            }
        }
        // The ruler, every so many frames.
        p.fillRect(QRectF(0, 0, width(), kRuler), QColor(28, 29, 32));
        p.setPen(QColor(170, 170, 170));
        int const step = zoom >= 10 ? 5 : zoom >= 4 ? 10 : zoom >= 1.5 ? 25 : 100;
        for (int f = 0; xOf(f) < width(); f += step)
            p.drawText(QPointF(xOf(f) + 2, 13), QString::number(f + 1));
        // The chosen clip's envelope.
        if (clip >= 0 && clip < all.size()) {
            SoundClip const& c = all[clip];
            QVector<QPointF> line;
            if (c.envelope.isEmpty()) {
                line = {QPointF(xOf(c.start), yOfGain(1.0)), QPointF(xOf(clipEnd(c, fps)), yOfGain(1.0))};
            } else {
                line << QPointF(xOf(c.start), yOfGain(c.envelope.first().y()));
                for (QPointF const& e : c.envelope)
                    line << QPointF(xOf(c.start + e.x()), yOfGain(e.y()));
                line << QPointF(xOf(clipEnd(c, fps)), yOfGain(c.envelope.last().y()));
            }
            p.setPen(QPen(QColor(255, 200, 60), 1.5));
            p.drawPolyline(line.constData(), int(line.size()));
            p.setBrush(QColor(255, 200, 60));
            for (QPointF const& e : c.envelope)
                p.drawRect(QRectF(xOf(c.start + e.x()) - 3, yOfGain(e.y()) - 3, 6, 6));
        }
        p.setPen(QPen(QColor(224, 60, 60), 1.0));
        p.drawLine(QPointF(xOf(frame - scene.layers[layer].soundStart), 0), QPointF(xOf(frame - scene.layers[layer].soundStart), height()));
    }

    // The envelope point of the chosen clip near a place, or -1.
    int pointAt(QPointF pos) const
    {
        QVector<SoundClip> const all = clips();
        if (clip < 0 || clip >= all.size())
            return -1;
        for (int i = 0; i < all[clip].envelope.size(); ++i) {
            QPointF const e = all[clip].envelope[i];
            if (std::abs(xOf(all[clip].start + e.x()) - pos.x()) <= 6 && std::abs(yOfGain(e.y()) - pos.y()) <= 6)
                return i;
        }
        return -1;
    }

    int clipUnder(double x) const
    {
        QVector<SoundClip> const all = clips();
        double const f = frameAt(x), fps = m_document->scene().fps;
        for (int c = int(all.size()) - 1; c >= 0; --c)
            if (f >= all[c].start && f < clipEnd(all[c], fps))
                return c;
        return -1;
    }

    void setClipEnvelope(QVector<QPointF> envelope)
    {
        QVector<SoundClip> all = clips();
        if (clip < 0 || clip >= all.size())
            return;
        std::sort(envelope.begin(), envelope.end(), [](QPointF const& a, QPointF const& b) { return a.x() < b.x(); });
        all[clip].envelope = envelope;
        m_document->setSoundClips(layer, all, tr("Change Sound Clip Envelope"));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QPointF const pos = event->position();
        m_point = pointAt(pos);
        if (event->button() == Qt::RightButton && m_point >= 0) {
            QVector<QPointF> envelope = clips()[clip].envelope;
            envelope.remove(m_point);
            m_point = -1;
            setClipEnvelope(envelope);
            return;
        }
        if (event->button() != Qt::LeftButton || m_point >= 0)
            return;
        int const under = clipUnder(pos.x());
        if (under != clip && clipPicked) {
            clipPicked(under);
            return;
        }
        // Pressed on the chosen clip away from its points: dragging moves it.
        m_moveFrom = under >= 0 ? frameAt(pos.x()) : -1e9;
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!(event->buttons() & Qt::LeftButton))
            return;
        QVector<SoundClip> all = clips();
        if (clip < 0 || clip >= all.size())
            return;
        QPointF const pos = event->position();
        if (m_point >= 0) {
            QVector<QPointF> envelope = all[clip].envelope;
            double const lowest = m_point > 0 ? envelope[m_point - 1].x() : 0.0;
            double const highest = m_point + 1 < envelope.size() ? envelope[m_point + 1].x() : 1e9;
            envelope[m_point] = QPointF(std::clamp(std::round(frameAt(pos.x()) - all[clip].start), lowest, highest), gainAt(pos.y()));
            setClipEnvelope(envelope);
        } else if (m_moveFrom > -1e8 && all[clip].loops <= 1) {
            int const by = int(std::round(frameAt(pos.x()) - m_moveFrom));
            if (by != 0) {
                all[clip].start += by;
                if (all[clip].stop >= 0)
                    all[clip].stop += by;
                m_moveFrom += by;
                m_document->setSoundClips(layer, all, tr("Move Sound Clip"));
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        m_point = -1;
        m_moveFrom = -1e9;
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        // A new point on the chosen clip's envelope.
        QVector<SoundClip> const all = clips();
        if (clip < 0 || clip >= all.size() || clipUnder(event->position().x()) != clip)
            return;
        QVector<QPointF> envelope = all[clip].envelope;
        envelope.push_back(QPointF(std::round(frameAt(event->position().x()) - all[clip].start), gainAt(event->position().y())));
        setClipEnvelope(envelope);
    }

private:
    Document* m_document;
    int m_point = -1;
    double m_moveFrom = -1e9;
};

SoundEditor::SoundEditor(Document* document, QWidget* parent)
    : QDialog(parent), m_document(document)
{
    setObjectName(QStringLiteral("SoundEditor"));
    setWindowTitle(tr("Sound Element Editor"));
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("SoundEditorName"));

    // The waveform, with its zoom.
    m_wave = new SoundWaveView(document, this);
    m_wave->clipPicked = [this](int c) { setCurrentClip(c); };
    QScrollArea* const scroll = new QScrollArea(this);
    scroll->setWidget(m_wave);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(180);
    QToolButton* const zoomIn = new QToolButton(this);
    zoomIn->setObjectName(QStringLiteral("SoundZoomIn"));
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setToolTip(tr("Zoom in/out Waveform"));
    QToolButton* const zoomOut = new QToolButton(this);
    zoomOut->setObjectName(QStringLiteral("SoundZoomOut"));
    zoomOut->setText(QStringLiteral("-"));
    zoomOut->setToolTip(tr("Zoom in/out Waveform"));
    connect(zoomIn, &QToolButton::clicked, this, [this] { setZoom(zoom() * 1.5); });
    connect(zoomOut, &QToolButton::clicked, this, [this] { setZoom(zoom() / 1.5); });
    QPushButton* const play = new QPushButton(tr("Play"), this);
    play->setObjectName(QStringLiteral("SoundPlay"));
    connect(play, &QPushButton::clicked, this, [this] { emit playLayer(m_layer, m_frame); });
    QHBoxLayout* const top = new QHBoxLayout;
    top->addWidget(m_name, 1);
    top->addWidget(play);
    top->addWidget(zoomOut);
    top->addWidget(zoomIn);

    // The clips.
    QGroupBox* const clipBox = new QGroupBox(tr("Sound Clips"), this);
    m_clips = new QListWidget(clipBox);
    m_clips->setObjectName(QStringLiteral("SoundClips"));
    connect(m_clips, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_filling)
            return;
        m_wave->clip = row;
        fillClip();
        m_wave->update();
    });
    QPushButton* const add = new QPushButton(tr("Import Sound Clip..."), clipBox);
    add->setObjectName(QStringLiteral("SoundAddClip"));
    connect(add, &QPushButton::clicked, this, [this] { emit importClip(m_layer); });
    QPushButton* const remove = new QPushButton(tr("Delete Sound Clip"), clipBox);
    remove->setObjectName(QStringLiteral("SoundDeleteClip"));
    connect(remove, &QPushButton::clicked, this, [this] {
        QVector<SoundClip> clips = clipsHere();
        int const c = currentClip();
        if (c < 0 || c >= clips.size() || clips.size() < 2)
            return;     // a sound layer keeps one clip at least
        clips.remove(c);
        m_document->setSoundClips(m_layer, clips, tr("Delete Sound Clip"));
    });
    QPushButton* const split = new QPushButton(tr("Split Sound Clip"), clipBox);
    split->setObjectName(QStringLiteral("SoundSplitClip"));
    split->setToolTip(tr("The clip playing on the current frame made two there."));
    connect(split, &QPushButton::clicked, this, [this] {
        QString why;
        if (!m_document->splitSoundClip(m_layer, m_frame, &why,currentClip()))
            m_name->setText(why);
    });
    m_start = new QSpinBox(clipBox);
    m_start->setObjectName(QStringLiteral("SoundClipStart"));
    m_start->setRange(-100000, 100000);
    m_stop = new QSpinBox(clipBox);
    m_stop->setObjectName(QStringLiteral("SoundClipStop"));
    m_stop->setRange(0, 100000);
    m_stop->setSpecialValueText(tr("Its end"));
    m_offset = new QDoubleSpinBox(clipBox);
    m_offset->setObjectName(QStringLiteral("SoundClipOffset"));
    m_offset->setRange(0.0, 100000.0);
    m_offset->setDecimals(3);
    m_offset->setSuffix(tr(" s"));
    m_length = new QDoubleSpinBox(clipBox);
    m_length->setObjectName(QStringLiteral("SoundClipLength"));
    m_length->setRange(0.0, 100000.0);
    m_length->setDecimals(3);
    m_length->setSuffix(tr(" s"));
    m_length->setSpecialValueText(tr("To the file's end"));
    m_loops = new QSpinBox(clipBox);
    m_loops->setObjectName(QStringLiteral("SoundClipLoops"));
    m_loops->setRange(1, 1000);
    m_volume = new QDoubleSpinBox(clipBox);
    m_volume->setObjectName(QStringLiteral("SoundClipVolume"));
    m_volume->setRange(0.0, 400.0);
    m_volume->setSuffix(QStringLiteral(" %"));
    for (QAbstractSpinBox* box : {static_cast<QAbstractSpinBox*>(m_start), static_cast<QAbstractSpinBox*>(m_stop), static_cast<QAbstractSpinBox*>(m_offset),
                                  static_cast<QAbstractSpinBox*>(m_length), static_cast<QAbstractSpinBox*>(m_loops), static_cast<QAbstractSpinBox*>(m_volume)}) {
        box->setKeyboardTracking(false);
        connect(box, &QAbstractSpinBox::editingFinished, this, &SoundEditor::clipEdited);
    }
    QFormLayout* const clipForm = new QFormLayout;
    clipForm->addRow(tr("Start Frame:"), m_start);
    clipForm->addRow(tr("Stop Frame:"), m_stop);
    clipForm->addRow(tr("Start Time:"), m_offset);
    clipForm->addRow(tr("Length:"), m_length);
    clipForm->addRow(tr("Loops:"), m_loops);
    clipForm->addRow(tr("Volume:"), m_volume);
    clipForm->addRow(new QLabel(tr("Envelope: double-click the clip for a point, drag points, right-click one to take it away."), clipBox));
    QHBoxLayout* const clipButtons = new QHBoxLayout;
    clipButtons->addWidget(add);
    clipButtons->addWidget(remove);
    clipButtons->addWidget(split);
    QVBoxLayout* const clipLeft = new QVBoxLayout;
    clipLeft->addWidget(m_clips, 1);
    clipLeft->addLayout(clipButtons);
    QHBoxLayout* const clipLayout = new QHBoxLayout(clipBox);
    clipLayout->addLayout(clipLeft, 1);
    clipLayout->addLayout(clipForm, 1);

    // The layer: its volume and its effects.
    QGroupBox* const effectBox = new QGroupBox(tr("Sound Effects"), this);
    m_layerVolume = new QDoubleSpinBox(effectBox);
    m_layerVolume->setObjectName(QStringLiteral("SoundLayerVolume"));
    m_layerVolume->setRange(0.0, 400.0);
    m_layerVolume->setSuffix(QStringLiteral(" %"));
    m_layerVolume->setKeyboardTracking(false);
    connect(m_layerVolume, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_filling)
            m_document->setSoundVolume(m_layer, m_layerVolume->value() / 100.0);
    });
    m_effects = new QListWidget(effectBox);
    m_effects->setObjectName(QStringLiteral("SoundEffects"));
    connect(m_effects, &QListWidget::currentRowChanged, this, [this] {
        if (!m_filling)
            fillEffects();
    });
    m_effectKind = new QComboBox(effectBox);
    m_effectKind->setObjectName(QStringLiteral("SoundEffectKind"));
    for (int t = 0; t < SoundEffect::TypeCount; ++t)
        m_effectKind->addItem(SoundEffect::name(t), t);
    QPushButton* const addEffect = new QPushButton(tr("Add"), effectBox);
    addEffect->setObjectName(QStringLiteral("SoundAddEffect"));
    connect(addEffect, &QPushButton::clicked, this, [this] {
        int const type = m_effectKind->currentData().toInt();
        editEffects([type](QVector<SoundEffect>& effects) { effects.push_back(SoundEffect::make(type)); }, tr("Add Sound Effect"));
        m_effects->setCurrentRow(m_effects->count() - 1);
    });
    QPushButton* const removeEffect = new QPushButton(tr("Remove"), effectBox);
    removeEffect->setObjectName(QStringLiteral("SoundRemoveEffect"));
    connect(removeEffect, &QPushButton::clicked, this, [this] {
        int const row = m_effects->currentRow();
        editEffects([row](QVector<SoundEffect>& effects) {
            if (row >= 0 && row < effects.size())
                effects.remove(row);
        }, tr("Remove Sound Effects"));
    });
    QPushButton* const up = new QPushButton(tr("Up"), effectBox);
    up->setObjectName(QStringLiteral("SoundEffectUp"));
    connect(up, &QPushButton::clicked, this, [this] {
        int const row = m_effects->currentRow();
        editEffects([row](QVector<SoundEffect>& effects) {
            if (row > 0 && row < effects.size())
                effects.swapItemsAt(row, row - 1);
        }, tr("Move Sound Effects"));
        m_effects->setCurrentRow(std::max(0, row - 1));
    });
    m_effectOn = new QCheckBox(tr("On"), effectBox);
    m_effectOn->setObjectName(QStringLiteral("SoundEffectOn"));
    connect(m_effectOn, &QCheckBox::toggled, this, &SoundEditor::effectEdited);
    QFormLayout* const effectForm = new QFormLayout;
    effectForm->addRow(tr("Sequence Volume:"), m_layerVolume);
    effectForm->addRow(m_effectOn);
    for (int i = 0; i < 3; ++i) {
        m_settingNames[i] = new QLabel(effectBox);
        m_settings[i] = new QDoubleSpinBox(effectBox);
        m_settings[i]->setObjectName(QStringLiteral("SoundEffectSetting%1").arg(i + 1));
        m_settings[i]->setKeyboardTracking(false);
        m_settings[i]->setDecimals(1);
        connect(m_settings[i], &QDoubleSpinBox::editingFinished, this, &SoundEditor::effectEdited);
        effectForm->addRow(m_settingNames[i], m_settings[i]);
    }
    QHBoxLayout* const effectButtons = new QHBoxLayout;
    effectButtons->addWidget(m_effectKind, 1);
    effectButtons->addWidget(addEffect);
    effectButtons->addWidget(removeEffect);
    effectButtons->addWidget(up);
    QVBoxLayout* const effectLeft = new QVBoxLayout;
    effectLeft->addWidget(m_effects, 1);
    effectLeft->addLayout(effectButtons);
    QHBoxLayout* const effectLayout = new QHBoxLayout(effectBox);
    effectLayout->addLayout(effectLeft, 1);
    effectLayout->addLayout(effectForm, 1);

    // Lip-sync: Detect, Map, and the mouth on the current frame.
    QGroupBox* const lipBox = new QGroupBox(tr("Lip Sync"), this);
    QHBoxLayout* const lipLayout = new QHBoxLayout(lipBox);
    QPushButton* const detect = new QPushButton(tr("Detect"), lipBox);
    detect->setObjectName(QStringLiteral("SoundDetect"));
    connect(detect, &QPushButton::clicked, this, &SoundEditor::detectLipSync);
    QPushButton* const map = new QPushButton(tr("Map"), lipBox);
    map->setObjectName(QStringLiteral("SoundMap"));
    connect(map, &QPushButton::clicked, this, &SoundEditor::mapLipSync);
    lipLayout->addWidget(detect);
    lipLayout->addWidget(map);
    lipLayout->addSpacing(12);
    lipLayout->addWidget(new QLabel(tr("Mouth Shapes:"), lipBox));
    for (QChar const shape : QStringLiteral("ABCDEFGX")) {
        QToolButton* const button = new QToolButton(lipBox);
        button->setObjectName(QStringLiteral("SoundMouth") + shape);
        button->setText(shape == QLatin1Char('X') ? QStringLiteral("(X)") : QString(shape));
        connect(button, &QToolButton::clicked, this, [this, shape] { emit mouthShape(QString(shape)); });
        lipLayout->addWidget(button);
    }
    lipLayout->addStretch(1);

    QPushButton* const close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(scroll, 1);
    layout->addWidget(clipBox);
    layout->addWidget(effectBox);
    layout->addWidget(lipBox);
    layout->addWidget(close, 0, Qt::AlignRight);
    resize(860, 680);
    connect(document, &Document::changed, this, &SoundEditor::refresh);
}

QVector<SoundClip> SoundEditor::clipsHere() const
{
    Scene const& scene = m_document->scene();
    return m_layer >= 0 && m_layer < scene.layers.size() ? scene.layers[m_layer].clipsOfSound() : QVector<SoundClip>();
}

void SoundEditor::setLayer(int layer)
{
    m_layer = layer;
    m_wave->layer = layer;
    m_wave->clip = 0;
    refresh();
}

void SoundEditor::setFrame(int frame)
{
    m_frame = frame;
    m_wave->frame = frame;
    m_wave->update();
}

int SoundEditor::currentClip() const
{
    return m_wave->clip;
}

void SoundEditor::setCurrentClip(int clip)
{
    m_wave->clip = clip;
    m_filling = true;
    m_clips->setCurrentRow(clip);
    m_filling = false;
    fillClip();
    m_wave->update();
}

double SoundEditor::zoom() const
{
    return m_wave->zoom;
}

void SoundEditor::setZoom(double pixelsPerFrame)
{
    m_wave->zoom = std::clamp(pixelsPerFrame, 0.25, 60.0);
    m_wave->fitWidth();
    m_wave->update();
}

void SoundEditor::refresh()
{
    Scene const& scene = m_document->scene();
    bool const sound = m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].kind == Layer::Sound;
    setEnabled(sound);
    if (!sound) {
        m_name->setText(tr("No Sound Available"));
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    m_name->setText(tr("Current sound: %1").arg(layer.name));
    QVector<SoundClip> const clips = layer.clipsOfSound();
    m_filling = true;
    m_clips->clear();
    for (SoundClip const& clip : clips)
        m_clips->addItem(tr("%1 (frame %2)").arg(QFileInfo(clip.file).fileName()).arg(clip.start + layer.soundStart + 1));
    m_wave->clip = std::clamp(m_wave->clip, clips.isEmpty() ? -1 : 0, int(clips.size()) - 1);
    m_clips->setCurrentRow(m_wave->clip);
    int const effectRow = m_effects->currentRow();
    m_effects->clear();
    for (SoundEffect const& effect : layer.soundEffects)
        m_effects->addItem(SoundEffect::name(effect.type) + (effect.enabled ? QString() : tr(" (off)")));
    m_effects->setCurrentRow(std::min(effectRow, int(m_effects->count()) - 1));
    m_layerVolume->setValue(layer.soundVolume * 100.0);
    m_filling = false;
    fillClip();
    fillEffects();
    m_wave->fitWidth();
    m_wave->update();
}

void SoundEditor::fillClip()
{
    QVector<SoundClip> const clips = clipsHere();
    int const c = currentClip();
    bool const any = c >= 0 && c < clips.size();
    for (QWidget* w : {static_cast<QWidget*>(m_start), static_cast<QWidget*>(m_stop), static_cast<QWidget*>(m_offset), static_cast<QWidget*>(m_length),
                       static_cast<QWidget*>(m_loops), static_cast<QWidget*>(m_volume)})
        w->setEnabled(any);
    if (!any)
        return;
    SoundClip const& clip = clips[c];
    int const start = m_document->scene().layers[m_layer].soundStart;
    m_filling = true;
    m_start->setValue(clip.start + start + 1);
    m_stop->setValue(clip.stop < 0 ? 0 : clip.stop + start + 1);
    m_offset->setValue(clip.offset);
    m_length->setValue(std::max(0.0, clip.length));
    m_loops->setValue(clip.loops);
    m_volume->setValue(clip.volume * 100.0);
    m_filling = false;
}

void SoundEditor::clipEdited()
{
    if (m_filling)
        return;
    QVector<SoundClip> clips = clipsHere();
    int const c = currentClip();
    if (c < 0 || c >= clips.size())
        return;
    int const start = m_document->scene().layers[m_layer].soundStart;
    SoundClip was = clips[c];
    SoundClip& clip = clips[c];
    clip.start = m_start->value() - 1 - start;
    clip.stop = m_stop->value() <= 0 ? -1 : std::max(clip.start + 1, m_stop->value() - 1 - start);
    clip.offset = m_offset->value();
    clip.length = m_length->value() <= 0.0 ? -1.0 : m_length->value();
    clip.loops = m_loops->value();
    clip.volume = m_volume->value() / 100.0;
    QString text = tr("Change Sound Clip Time");
    if (clip.start != was.start)
        text = tr("Change Sound Clip Start Frame");
    else if (clip.stop != was.stop)
        text = tr("Change Sound Clip Stop Frame");
    else if (clip.offset != was.offset)
        text = tr("Change Sound Clip Start Time");
    else if (clip.loops != was.loops)
        text = tr("Change Sound Clip Loops");
    else if (clip.volume != was.volume)
        text = tr("Change Sound Sequence Volume");
    m_document->setSoundClips(m_layer, clips, text);
}

void SoundEditor::fillEffects()
{
    QVector<SoundEffect> const effects = m_layer >= 0 && m_layer < m_document->scene().layers.size() ? m_document->scene().layers[m_layer].soundEffects : QVector<SoundEffect>();
    int const row = m_effects->currentRow();
    bool const any = row >= 0 && row < effects.size();
    m_effectOn->setEnabled(any);
    QVector<SoundEffect::Setting> const settings = any ? SoundEffect::settings(effects[row].type) : QVector<SoundEffect::Setting>();
    m_filling = true;
    m_effectOn->setChecked(any && effects[row].enabled);
    double const values[3] = {any ? effects[row].a : 0.0, any ? effects[row].b : 0.0, any ? effects[row].c : 0.0};
    for (int i = 0; i < 3; ++i) {
        bool const used = i < settings.size();
        m_settingNames[i]->setText(used ? settings[i].name : QString());
        m_settings[i]->setVisible(used);
        m_settingNames[i]->setVisible(used);
        if (used) {
            m_settings[i]->setRange(settings[i].low, settings[i].high);
            m_settings[i]->setValue(values[i]);
        }
    }
    m_filling = false;
}

void SoundEditor::effectEdited()
{
    if (m_filling)
        return;
    int const row = m_effects->currentRow();
    bool const on = m_effectOn->isChecked();
    double const a = m_settings[0]->value(), b = m_settings[1]->value(), c = m_settings[2]->value();
    editEffects([row, on, a, b, c](QVector<SoundEffect>& effects) {
        if (row < 0 || row >= effects.size())
            return;
        SoundEffect& e = effects[row];
        int const used = int(SoundEffect::settings(e.type).size());
        e.enabled = on;
        e.a = used > 0 ? a : e.a;
        e.b = used > 1 ? b : e.b;
        e.c = used > 2 ? c : e.c;
    }, tr("Change Sound Effect"));
}

void SoundEditor::editEffects(std::function<void(QVector<SoundEffect>&)> const& change, QString const& text)
{
    if (m_layer < 0 || m_layer >= m_document->scene().layers.size())
        return;
    QVector<SoundEffect> effects = m_document->scene().layers[m_layer].soundEffects;
    change(effects);
    m_document->setSoundEffects(m_layer, effects, text);
}
