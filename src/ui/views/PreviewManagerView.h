// Eclipse - Preview Manager: the frames of the scene rendered once and kept, to play back smoothly.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Canvas;
class Scene;
class QTimer;
class Document;
class QCheckBox;
class QComboBox;
class QLabel;

class ECLIPSE_UI_API PreviewManagerView : public QWidget {
    Q_OBJECT

public:
    PreviewManagerView(Document* document, Canvas* canvas, QWidget* parent = nullptr);
    int render(bool everything = false);
    void clear();
    // Stops a render under way (what was rendered so far is kept for the next one).
    void cancel() { m_cancel = true; }
    bool rendering() const { return m_rendering; }
    // Rendered again by itself, a moment after each change to the scene.
    void setAutoRender(bool on);
    bool autoRender() const { return m_auto; }
    // What a frame of the scene shows, as a number: the same number, the same picture.
    static quint64 fingerprint(Scene const& scene, int frame);
    // How big the frames are kept: 100, 50 or 25 (per cent of the scene's size).
    void setSizePercent(int percent);
    int sizePercent() const;
    // The sound of the scene, mixed when the frames were rendered and kept with them: what plays with the preview.
    QVector<qint16> const& sound() const { return m_sound; }

private:
    void showStatus(bool stale = false);
    Document* m_document;
    Canvas* m_canvas;
    QComboBox* m_size;
    QCheckBox* m_use;
    QLabel* m_status;
    bool m_rendering = false;
    bool m_cancel = false;
    bool m_auto = false;
    quint64 m_revision = 0;
    QTimer* m_autoTimer = nullptr;
    QVector<QImage> m_frames;          // what was rendered last, frame by frame (kept when the preview goes stale)
    QVector<quint64> m_prints;         // and the fingerprint of each
    QVector<qint16> m_sound;           // the sound mixed with the last complete render
};
