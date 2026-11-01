// Eclipse - the Sound Editor: a sound layer's clips on its waveform, their times, loops, volume and envelope, the layer's effects, lip-sync.
#pragma once

#include "EclipseExport.h"

#include <QDialog>
#include <QPointer>

#include <functional>

#include "model/scene/Layer.h"

class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QSpinBox;
class SoundWaveView;

class ECLIPSE_UI_API SoundEditor : public QDialog {
    Q_OBJECT

public:
    explicit SoundEditor(Document* document, QWidget* parent = nullptr);

    void setLayer(int layer);
    int layer() const { return m_layer; }
    void setFrame(int frame);
    int currentClip() const;
    void setCurrentClip(int clip);
    // The zoom of the waveform: pixels a frame.
    double zoom() const;
    void setZoom(double pixelsPerFrame);

signals:
    // Detect, Map, a mouth shape put on the frame, Play: the window does them.
    void detectLipSync();
    void mapLipSync();
    void mouthShape(QString const& shape);
    void playLayer(int layer, int frame);
    void importClip(int layer);

private:
    QVector<SoundClip> clipsHere() const;
    void refresh();
    void fillClip();
    void clipEdited();
    void fillEffects();
    void effectEdited();
    void editEffects(std::function<void(QVector<SoundEffect>&)> const& change, QString const& text);

    Document* m_document;
    int m_layer = -1;
    int m_frame = 0;
    bool m_filling = false;
    QLabel* m_name;
    SoundWaveView* m_wave;
    QListWidget* m_clips;
    QSpinBox* m_start;
    QSpinBox* m_stop;
    QDoubleSpinBox* m_offset;
    QDoubleSpinBox* m_length;
    QSpinBox* m_loops;
    QDoubleSpinBox* m_volume;
    QDoubleSpinBox* m_layerVolume;
    QListWidget* m_effects;
    QComboBox* m_effectKind;
    QCheckBox* m_effectOn;
    QLabel* m_settingNames[3];
    QDoubleSpinBox* m_settings[3];
};
