// Eclipse - Preview Manager: the frames of the scene rendered once and kept, to play back smoothly.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Canvas;
class Document;
class QCheckBox;
class QComboBox;
class QLabel;

class ECLIPSE_UI_API PreviewManagerView : public QWidget {
    Q_OBJECT

public:
    PreviewManagerView(Document* document, Canvas* canvas, QWidget* parent = nullptr);
    // Renders every frame of the scene at the size chosen and hands them to the Camera view
    // to play from.  The number of frames rendered.
    int render();
    void clear();

private:
    void showStatus(bool stale = false);
    Document* m_document;
    Canvas* m_canvas;
    QComboBox* m_size;
    QCheckBox* m_use;
    QLabel* m_status;
    bool m_rendering = false;
};
