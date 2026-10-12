// Eclipse - 3D Graph: the layers as they hang together, with where each stands in space.
#pragma once

#include "EclipseExport.h"

#include <QVector>
#include <QWidget>

class Document;
class QTreeWidget;

class ECLIPSE_UI_API Graph3DView : public QWidget {
    Q_OBJECT

public:
    explicit Graph3DView(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void layerPicked(int layer);

private:
    void rebuild();
    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    QTreeWidget* m_tree;
    QVector<int> m_shape;       // the layers and what each hangs under, as the tree was last built
    bool m_rebuilding = false;
};
