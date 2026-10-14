// Eclipse - Coord. and Control Points: where the current layer is on the current frame, as numbers
// that can be typed (Selection Coordinates), and its keyframes as a table.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class ECLIPSE_UI_API CoordView : public QWidget {
    Q_OBJECT

public:
    explicit CoordView(Document* document, QWidget* parent = nullptr);
    // A distance across (`axis` 0), up (1) or towards the camera (2), written in fields with
    // the letter of its direction: "7.5862 E", "2.1241 S", "3 F".  And read back from that
    // (false when it is not a number); a letter is not needed, the sign will do.
    static QString fieldText(double pixels, int axis, double sceneHeight);
    static bool fieldValue(QString const& text, int axis, double sceneHeight, double* pixels);

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void framePicked(int frame);

private:
    void rebuild();
    void cellEdited(int row, int column);
    // The control points of the stretch of path the frame is on, as a table of their own.
    void rebuildPoints();
    void pointEdited(int row, int column);
    QTableWidget* m_pointTable = nullptr;
    class QPushButton* m_pointLock = nullptr;
    class QPushButton* m_pointRemove = nullptr;
    // The form: shows the layer as it is on the current frame; takes what was typed in one of its boxes.
    void showCoordinates();
    void applyCoordinate(int which);
    QWidget* section(QString const& title, char const* objectName, class QFormLayout** form);
    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    QTableWidget* m_table;
    QLabel* m_title;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_fields[8] = {};       // x, y, z, scale x, scale y, angle, skew - and the depth it comes to (not typed)
    QCheckBox* m_separateScale = nullptr;
    class QDoubleSpinBox* m_shape[3] = {};  // tension, continuity, bias of the stretch of path the frame is on
    class QLabel* m_points = nullptr;       // how many control points that stretch has
    int pathKey() const;                    // the keyframe that stretch starts at, or -1
    bool m_rebuilding = false;
};
