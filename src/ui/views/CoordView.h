// Eclipse - Coord. and Control Points: the keyframes of the current layer as a table of numbers.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QLabel;
class QTableWidget;

class ECLIPSE_UI_API CoordView : public QWidget {
    Q_OBJECT

public:
    explicit CoordView(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void framePicked(int frame);

private:
    void rebuild();
    void cellEdited(int row, int column);
    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    QTableWidget* m_table;
    QLabel* m_title;
    bool m_rebuilding = false;
};
