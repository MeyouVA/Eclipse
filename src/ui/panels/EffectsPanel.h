// Eclipse - the panel that edits the effects of a layer, and its colour curve.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include "model/scene/Scene.h"
#include "render/effects/Effects.h"

class Document;
class QPlainTextEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

class ECLIPSE_UI_API ColourCurveEdit : public QWidget {
    Q_OBJECT

public:
    explicit ColourCurveEdit(QWidget* parent = nullptr);
    void setPoints(QVector<QPointF> const& points);
    // The points, both ends included.
    QVector<QPointF> points() const { return m_points; }
    QSize sizeHint() const override { return QSize(160, 160); }

signals:
    // The curve has been changed (once, when the mouse lets go).
    void edited(QVector<QPointF> const& points);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF box() const;
    QPointF toWidget(QPointF const& level) const;
    QPointF toLevel(QPointF const& at) const;
    int pointAt(QPointF const& at) const;

    QVector<QPointF> m_points;
    int m_dragged = -1;
};

class ECLIPSE_UI_API EffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

private:
    // The constructor's parts: the settings of the kinds of effect that have more, and the editors wired up.
    void addAdjustSettings(class QVBoxLayout* layout);
    void addMoreSettings(class QVBoxLayout* layout);
    void addParticleSettings(class QVBoxLayout* layout);
    void connectEditors();
    void rebuild();
    void showEditors();
    void add(Effect::Type type);
    void removeCurrent();
    // The effect picked (none picked: all of them) copied; the effects copied added to the layer, after its own.
public:
    void copyEffects();
    void pasteEffects();
    static bool hasCopiedEffects() { return !s_copied.isEmpty(); }

private:
    static QVector<Effect> s_copied;
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
    QPushButton* m_endColor = nullptr;     // a gradient's second colour
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
    // Channel Selector's channel; Crop's area (left, top, width, height).
    QWidget* m_more = nullptr;
    class QFormLayout* m_moreForm = nullptr;
    QComboBox* m_channel = nullptr;
    QDoubleSpinBox* m_cropBox[4] = {};
    // Colour Curves: which curve (all three colours, red, green, blue) and the curve.
    QComboBox* m_curveWhich = nullptr;
    ColourCurveEdit* m_curve = nullptr;
    // Script and External: the script (or the command line), put to use by Apply.
    QPlainTextEdit* m_text = nullptr;
    QPushButton* m_textApply = nullptr;
    QLabel* m_textSays = nullptr;
};
