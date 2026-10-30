// Eclipse - the Layer Properties view: the name of the current layer and, in numbers, where
// it is on the current frame.  Changing a number sets a keyframe there, as the transform
// tool does.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class EffectsPanel;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

class ECLIPSE_UI_API LayerPanel : public QWidget {
    Q_OBJECT

public:
    explicit LayerPanel(Document* document, QWidget* parent = nullptr);
    void refreshFovLimits();

signals:
    // A value's function button: clicked (show it in the Function view), Alt-clicked (its column in the Function Editor).
    void functionShown(int layer, int value);
    void functionEditorWanted(int column);
    void message(QString const& text);
    // Attribute Information... from a value's button: the attributes of a layer described.
    void attributeInformationWanted(int layer);

public slots:
    void setCurrentCell(int layer, int frame);
    // Animating off: a number typed moves the layer as a whole, in all its keyframes alike.
    void setAnimate(bool on) { m_animate = on; }
    // Publish Attribute Mode: a click on a value's button publishes it on the group the layer is in (or takes it off).
    void setPublishMode(bool on);

private:
    void refresh();
    void applyPose();
    void applyPivot();
    void applyEase();
    void buildTransformOptions();
    void refreshTransformOptions(struct Layer const& layer);
    void buildCameraOptions();
    void refreshCameraOptions(struct Layer const& layer);
public:
    // Attribute Controls: a window to set which values of the current layer follow values of other layers.
    void editDrivers();
    bool publishMode() const { return m_publishMode; }
    // A value of a layer shown on the group it is in, or taken off it again.
    void togglePublished(int layer, int value);
    // An attribute of the user's own on the current layer (false: no name, a name it has, or a name with / or :).
    bool addCustomAttribute(QString const& name);
    static QString valueName(int value);
    // The group a layer is in (its nearest group above), or -1.
    int groupOf(int layer) const;
private:
    QDoubleSpinBox* number(double min, double max, double step, int decimals);
    // The button beside a value (Driver::Value) that shows its function and links it to a column.
    class QToolButton* functionButton(int value);
    void fillFunctionMenu(class QMenu* menu, int value);
    void refreshFunctionButtons(struct Layer const& layer);

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    bool m_refreshing = false;
    bool m_animate = true;

    QLineEdit* m_name;
    QLabel* m_kind;
    QLabel* m_parent;
    QComboBox* m_bone;
    QComboBox* m_element = nullptr;     // Advanced Element Mode: whose drawings the layer shows
    QLabel* m_key;
    QDoubleSpinBox* m_x;
    QDoubleSpinBox* m_y;
    QDoubleSpinBox* m_z;
    QDoubleSpinBox* m_rotX;         // turned in space
    QDoubleSpinBox* m_rotY;
    class QLayout* m_turnRow = nullptr;
    QDoubleSpinBox* m_fov;          // a camera: the angle it sees
    class QLayout* m_fovRow = nullptr;
    QWidget* m_clipRow = nullptr;
    QDoubleSpinBox* m_nearPlane = nullptr;
    class QCheckBox* m_cameraAvailable = nullptr;
    QDoubleSpinBox* m_farPlane = nullptr;
    class QToolButton* m_functionButtons[9] = {};
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
    QWidget* m_ikRow = nullptr;     // a bone: its IK constraints
    QWidget* m_artRow = nullptr;    // a drawing layer: which of its art layers are in the picture
    QCheckBox* m_bitmap = nullptr;  // a drawing layer: painted in pixels
    QCheckBox* m_bend = nullptr;    // a drawing layer on Game Bones: bends along them
    QComboBox* m_blend = nullptr;   // how the layer's picture is mixed with what is under it
    QDoubleSpinBox* m_smooth = nullptr;     // how far round a joint of its bones the drawing bends
    QPushButton* m_drivers = nullptr;       // Attribute Controls...
    QCheckBox* m_artShown[4] = {nullptr, nullptr, nullptr, nullptr};
    QCheckBox* m_ikExcluded = nullptr;
    QCheckBox* m_ikNail = nullptr;          // IK Nail
    QCheckBox* m_ikHold = nullptr;          // IK Hold Orientation
    QCheckBox* m_ikLimited = nullptr;
    QDoubleSpinBox* m_ikMin = nullptr;
    QDoubleSpinBox* m_ikMax = nullptr;
    QWidget* m_transformRow = nullptr;      // separate or locked values, angle limits, the drawing pivot
    QCheckBox* m_separatePosition = nullptr;
    QCheckBox* m_separateScale = nullptr;
    QCheckBox* m_ignoreParentScale = nullptr;
    QCheckBox* m_animatable = nullptr;
    QCheckBox* m_angleLimited = nullptr;
    QDoubleSpinBox* m_minAngle = nullptr;
    QDoubleSpinBox* m_maxAngle = nullptr;
    QComboBox* m_embeddedPivot = nullptr;
    QComboBox* m_cameraAlignment = nullptr;
    class QFormLayout* m_form;
    void buildAttributeSections();
    void refreshAttributeSections(struct Layer const& layer);
    void addAttributeCommands(class QMenu* menu, int value);
    bool m_publishMode = false;
    QWidget* m_drawingBox = nullptr;
    QCheckBox* m_enable3d = nullptr;
    QComboBox* m_alignment = nullptr;
    QCheckBox* m_turnBefore = nullptr;
    QCheckBox* m_flipH = nullptr;
    QCheckBox* m_flipV = nullptr;
    QCheckBox* m_clipping = nullptr;
    QDoubleSpinBox* m_clipX = nullptr;
    QComboBox* m_antialiasing = nullptr;
    QDoubleSpinBox* m_antialiasExponent = nullptr;
    QComboBox* m_textureFilter = nullptr;
    QComboBox* m_colourSpace = nullptr;
    QDoubleSpinBox* m_clipY = nullptr;
    QWidget* m_publishedBox = nullptr;
    QWidget* m_customBox = nullptr;
    EffectsPanel* m_effects;
};
