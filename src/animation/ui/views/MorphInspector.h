// Eclipse - the Morphing Inspector: the options of the morphing on the current cell - quality, flatten, eases, the type of
// hint placed, its morphing layers with their own timing, and the deformation paths they can follow.
#pragma once

#include "EclipseExport.h"

#include <QWidget>
#include <functional>

class Canvas;
class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
struct Morph;

class ECLIPSE_UI_API MorphInspector : public QWidget {
    Q_OBJECT

public:
    MorphInspector(Document* document, Canvas* canvas, QWidget* parent = nullptr);
    void refresh();

signals:
    // Switch Between Drawings: the window goes to the other key drawing.
    void switchRequested();

private:
    // The morphing on the current cell (its layer and number), or false.
    bool current(int& layer, int& index) const;
    void change(QString const& text, std::function<void(Morph&)> const& edit);

    Document* m_document;
    Canvas* m_canvas;
    bool m_filling = false;
    QLabel* m_current = nullptr;
    QLabel* m_time = nullptr;
    QDoubleSpinBox* m_quality = nullptr;
    QCheckBox* m_flatten = nullptr;
    QDoubleSpinBox* m_easeIn = nullptr;
    QDoubleSpinBox* m_easeOut = nullptr;
    QComboBox* m_hintType = nullptr;
    QCheckBox* m_hide = nullptr;
    QCheckBox* m_inPlace = nullptr;
    QTableWidget* m_layers = nullptr;
    QTableWidget* m_paths = nullptr;
    QWidget* m_body = nullptr;
};
