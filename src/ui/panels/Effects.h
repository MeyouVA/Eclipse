// Eclipse - what the effects of a layer do to its picture, and the panel that edits them.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
#include <QWidget>

#include "model/scene/Scene.h"

class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Effects {

// Softens a picture (premultiplied ARGB) by about `radius` pixels.
void blur(QImage& image, double radius);
// Moves every colour towards `color` by `amount` (0..1), leaving the shape as it is.
void tint(QImage& image, QColor const& color, double amount);

// A soft copy of the shape of the picture in one colour, `radius` pixels soft, moved by
// `offset` pixels, put under the picture (a shadow) - or with no offset, around it (a glow).
void halo(QImage& image, QColor const& color, double radius, QPointF offset);

// Makes the picture `amount` (0..1) see-through.
void transparency(QImage& image, double amount);
// Smears the picture `length` pixels along a direction (degrees, 0: across, 90: up and down).
void directionalBlur(QImage& image, double length, double degrees);
// Turns the hue by `hue` degrees, and raises the saturation and the lightness by so many percent; `amount` of it (0..1).
void hueSaturation(QImage& image, double hue, double saturation, double lightness, double amount);
// So many percent brighter and more contrast; `amount` of it (0..1).
void brightnessContrast(QImage& image, double brightness, double contrast, double amount);
// A grain over the picture (where it is painted), `amount` strong (0..1); `seed` says which grain.
void grain(QImage& image, double amount, quint32 seed);
// Each point smeared along the line from the middle of what is painted, `amount` (0..1) of how far it is from it.
void radialBlur(QImage& image, double amount);
// Each colour multiplied by `scale` (its red, green and blue as shares), `amount` of the way (0..1).
void colourScale(QImage& image, QColor const& scale, double amount);
// The colours turned to their opposites, `amount` of the way (0..1).
void negate(QImage& image, double amount);
// Each point the middle colour (and opacity) of those within `radius` pixels: specks go, edges stay.
void median(QImage& image, double radius);
// The rows of the picture rocked side to side by waves `height` pixels high and `length` pixels apart, moved on by `frame`.
void ripple(QImage& image, double height, double length, int frame);

QString name(Effect const& effect, Scene const& scene);

}

// The effects of the current layer, in the order they are applied: tick to switch one on
// or off, pick one to change it.
class ECLIPSE_UI_API EffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

private:
    void rebuild();
    void showEditors();
    void add(Effect::Type type);
    void removeCurrent();
    // Replaces effect `row` by what `change` makes of it.
    template <class Change> void edit(Change change);

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    bool m_rebuilding = false;

    QListWidget* m_list;
    QLabel* m_amountLabel;
    QDoubleSpinBox* m_amount;
    QPushButton* m_color;
    QLabel* m_matteLabel;
    QComboBox* m_matte;
    QCheckBox* m_invert;
    QWidget* m_particles = nullptr;
    // Hue-Saturation, Brightness-Contrast and Directional Blur: hue, saturation, lightness, brightness, contrast, direction.
    QWidget* m_adjust = nullptr;
    class QFormLayout* m_adjustForm = nullptr;
    QDoubleSpinBox* m_adjustBox[6] = {};
    // life, speed, spread, direction, gravity, seed, size at the end, wind, spin, region width and height, floor, bounce
    QDoubleSpinBox* m_particleBox[14] = {};
    QPushButton* m_particleColour = nullptr;
    class QComboBox* m_particleShape = nullptr;
    class QCheckBox* m_particleRecolour = nullptr;
    class QCheckBox* m_particleFollow = nullptr;
    QCheckBox* m_animated;
    QLabel* m_offsetLabel;
    QDoubleSpinBox* m_offsetX;
    QDoubleSpinBox* m_offsetY;
};
