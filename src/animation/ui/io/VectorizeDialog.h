// Eclipse - Vectorization Parameters: a style to start from, the main settings, the whole line of options, and a preview of
// what a picture becomes with them.
#pragma once

#include "EclipseExport.h"
#include "model/geometry/VectorizeOptions.h"

#include <QDialog>
#include <QImage>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class Scene;

class ECLIPSE_UI_API VectorizeDialog : public QDialog {
    Q_OBJECT

public:
    VectorizeDialog(Scene const& scene, QString const& line, QImage const& preview, QWidget* parent = nullptr);
    QString line() const;
    Vectorize::Options options() const { return m_options; }
    void setPreviewImage(QImage const& image);
    // Vectorize Image: the preview made with the settings as they are (false, with the reason shown, when it cannot be).
    bool runPreview();
    int previewShapes() const { return m_previewShapes; }

private:
    void fromWidgets();
    void toWidgets();
    bool fromLine();

    Scene const& m_scene;
    Vectorize::Options m_options;
    QImage m_image;
    bool m_filling = false;
    int m_previewShapes = 0;
    QComboBox* m_style = nullptr;
    QRadioButton* m_blackWhite = nullptr;
    QRadioButton* m_centre = nullptr;
    QRadioButton* m_colour = nullptr;
    QCheckBox* m_twoPass = nullptr;
    QDoubleSpinBox* m_threshLine = nullptr;
    QDoubleSpinBox* m_threshColour = nullptr;
    QDoubleSpinBox* m_holes = nullptr;
    QDoubleSpinBox* m_dirt = nullptr;
    QDoubleSpinBox* m_fit = nullptr;
    QSpinBox* m_smooth = nullptr;
    QCheckBox* m_noRed = nullptr;
    QCheckBox* m_noGreen = nullptr;
    QCheckBox* m_noBlue = nullptr;
    QCheckBox* m_rgb = nullptr;
    QDoubleSpinBox* m_gap = nullptr;
    QCheckBox* m_frame = nullptr;
    QCheckBox* m_flatten = nullptr;
    QCheckBox* m_register = nullptr;
    QComboBox* m_pegSide = nullptr;
    QSpinBox* m_dpi = nullptr;
    QLineEdit* m_line = nullptr;
    QLabel* m_problem = nullptr;
    QLabel* m_before = nullptr;
    QLabel* m_after = nullptr;
};
