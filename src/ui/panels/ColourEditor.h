// Eclipse - the Colour Editor: a window that edits the colour in hand as you go (a wheel, sliders, alpha, its value
// as text, and a tint); or, asked for one colour, a chooser that gives it back.
#pragma once

#include "EclipseExport.h"

#include <QDialog>

class Document;
class ColourWheel;
class QLabel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QToolButton;
class QTimer;
class QPushButton;
class QComboBox;

class ECLIPSE_UI_API ColourEditor : public QDialog {
    Q_OBJECT

public:
    // With a document: edits its colours (setSwatch); without: only chooses one.
    explicit ColourEditor(Document* document, QWidget* parent = nullptr);
    ~ColourEditor() override;

    // The colour (by id) whose value is edited, live; -1: none ("This colour is not stored in a palette").
    void setSwatch(int id);
    int swatch() const { return m_swatch; }
    QColor colour() const { return m_colour; }
    // Shown in the editor (and, while a colour is edited, given to it).
    void setColour(QColor const& colour);
    bool multiwheel() const;
    void setMultiwheel(bool on);
    // The sliders as hue, saturation and value (else red, green and blue).
    bool hsvSliders() const { return m_hsv; }
    void setHsvSliders(bool on);
    // A colour chosen with a modal editor of its own; invalid when cancelled.
    static QColor getColour(QColor const& initial, QWidget* parent, QString const& title);

signals:
    void colourChanged(QColor const& colour);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void build();
    void showColour();
    void fromSliders();
    void give(QColor const& colour);
    void finishEdit();

    Document* m_document;
    int m_swatch = -1;
    QColor m_colour = Qt::black;
    QColor m_was = Qt::black;
    bool m_hsv = true;
    bool m_showing = false;
    bool m_editOpen = false;
    ColourWheel* m_wheel = nullptr;
    QSlider* m_value = nullptr;
    QSlider* m_sliders[4] = {};
    QSpinBox* m_spins[4] = {};
    QLabel* m_names[3] = {};
    QLineEdit* m_hex = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_note = nullptr;
    QToolButton* m_multiButton = nullptr;
    QToolButton* m_hsvButton = nullptr;
    QTimer* m_settle = nullptr;
    QWidget* m_tintBox = nullptr;
};
