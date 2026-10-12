// Eclipse - Guides: straight lines across the picture to draw and snap to.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Canvas;
class Document;
class QCheckBox;
class QDoubleSpinBox;
class QListWidget;

class ECLIPSE_UI_API GuidesView : public QWidget {
    Q_OBJECT

public:
    GuidesView(Document* document, Canvas* canvas, QWidget* parent = nullptr);
    // A guide across (`vertical` false) or down the picture, at that place.  Its place in the list.
    int addGuide(bool vertical, double at);
    // A guide to draw along (Scene::Guide::Kind), set out for the scene.  Its place in the list.
    int addDrawingGuide(int kind);

private:
    void rebuild();
    Document* m_document;
    Canvas* m_canvas;
    QListWidget* m_list;
    QDoubleSpinBox* m_position;
    QCheckBox* m_show;
    bool m_rebuilding = false;
};
