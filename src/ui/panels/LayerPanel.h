// Eclipse - the Layer Properties view: the name of the current layer and, in numbers, where
// it is on the current frame.  Changing a number sets a keyframe there, as the transform
// tool does.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class EffectsPanel;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

class ECLIPSE_UI_API LayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);
    // Animating off: a number typed moves the layer as a whole, in all its keyframes alike.
    void setAnimate(bool on) { m_animate = on; }

private:
    void refresh();
    void applyPose();
    void applyPivot();
    void applyEase();
    QDoubleSpinBox* number(double min, double max, double step, int decimals);

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    bool m_refreshing = false;
    bool m_animate = true;

    QLineEdit* m_name;
    QLabel* m_kind;
    QLabel* m_parent;
    QComboBox* m_bone;
    QLabel* m_key;
    QDoubleSpinBox* m_x;
    QDoubleSpinBox* m_y;
    QDoubleSpinBox* m_z;
    QDoubleSpinBox* m_rotX;         // turned in space
    QDoubleSpinBox* m_rotY;
    class QLayout* m_turnRow = nullptr;
    QDoubleSpinBox* m_fov;          // a camera: the angle it sees
    QDoubleSpinBox* m_scaleX;
    QDoubleSpinBox* m_scaleY;
    QDoubleSpinBox* m_skew = nullptr;
    QDoubleSpinBox* m_angle;
    QDoubleSpinBox* m_pivotX;
    QDoubleSpinBox* m_pivotY;
    QDoubleSpinBox* m_easeIn;
    QDoubleSpinBox* m_easeOut;
    QDoubleSpinBox* m_opacity;
    class QPushButton* m_card;      // a colour-card: its colour
    QDoubleSpinBox* m_length;       // a bone: how long it is
    class QFormLayout* m_form;
    EffectsPanel* m_effects;
};
