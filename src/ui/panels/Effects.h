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
    QDoubleSpinBox* m_particleBox[6] = {};
    QCheckBox* m_animated;
    QLabel* m_offsetLabel;
    QDoubleSpinBox* m_offsetX;
    QDoubleSpinBox* m_offsetY;
};
