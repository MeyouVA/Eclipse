// Eclipse - the Function Editor: a function column edited by itself - its points, its expression, its 3D path, its velocity.
#pragma once

#include "EclipseExport.h"

#include <QDialog>

class Document;
class FunctionGraph;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

class ECLIPSE_UI_API FunctionEditor : public QDialog {
    Q_OBJECT

public:
    FunctionEditor(Document* document, int column, QWidget* parent = nullptr);
    int column() const { return m_id; }
    void setCurrentFrame(int frame);
    QTableWidget* points() const { return m_points; }
    QTableWidget* velocity() const { return m_velocity; }
    QPlainTextEdit* expression() const { return m_expression; }
    FunctionGraph* graph() const { return m_graph; }
    // Lock/Unlock Point: the 3D path point chosen kept to the current frame, or let go.
    void toggleLock();

private:
    void refresh();
    void commitPoints();
    void commitVelocity();
    void commitExpression();
    void addPoint();
    void removePoint();
    void addVelocityPoint();
    void removeVelocityPoint();
    void convertVelocity();

    Document* m_document;
    int m_id;
    int m_shownKind = 0;
    int m_frame = 0;
    bool m_filling = false;
    QLineEdit* m_name;
    QLabel* m_kind;
    QLabel* m_message;
    FunctionGraph* m_graph = nullptr;
    QTableWidget* m_points = nullptr;
    QTableWidget* m_velocity = nullptr;
    QPlainTextEdit* m_expression = nullptr;
    QCheckBox* m_constantZ = nullptr;
};
