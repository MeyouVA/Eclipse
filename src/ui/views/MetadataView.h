// Eclipse - Metadata: names and values kept with the scene or with a layer.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QComboBox;
class QTableWidget;

class ECLIPSE_UI_API MetadataView : public QWidget {
    Q_OBJECT

public:
    explicit MetadataView(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

private:
    void rebuild();
    // The layer the table is about, or -1 for the scene.
    int subject() const;
    void store();
    Document* m_document;
    int m_layer = 0;
    QComboBox* m_of;
    QTableWidget* m_table;
    bool m_rebuilding = false;
};
