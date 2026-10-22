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
    // Symmetry set out across (0), down (1) or both ways at once (2).
    void addSymmetry(int way);
    QVector<int> selectedRows() const;
    bool renameGuide(int row, QString const& name);
    // Cut, Copy and Paste of the guides selected (kept by the Guides view, for any scene).
    int copyGuides(bool cut);
    int pasteGuides();
    // Guides kept in a file of their own: how many were written, or taken in (-1: the file could not be).
    int exportGuides(QString const& path, bool selectedOnly);
    int importGuides(QString const& path, QString* message = nullptr);

private:
    void rebuild();
    void showMenu(QPoint const& at);
    Document* m_document;
    Canvas* m_canvas;
    QListWidget* m_list;
    QDoubleSpinBox* m_position;
    QCheckBox* m_show;
    bool m_rebuilding = false;
};
