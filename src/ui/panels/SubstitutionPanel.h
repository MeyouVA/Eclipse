// Eclipse - the Drawing Substitution view: every drawing of the current layer as a small
// picture.  Clicking one shows it on the current frame in place of the drawing there -
// how a cut-out character changes hand, mouth or eye without redrawing.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QListWidgetItem;

class ECLIPSE_UI_API SubstitutionPanel : public QWidget {
    Q_OBJECT

public:
    explicit SubstitutionPanel(Document* document, QWidget* parent = nullptr);

    int count() const;

public slots:
    void setCurrentCell(int layer, int frame);

private:
    void rebuild();
    void filter();

    Document* m_document;
    QListWidget* m_list;
    QLineEdit* m_search = nullptr;      // only the drawings whose name has what is typed
    QSlider* m_size = nullptr;          // how big the pictures are
    QLabel* m_name = nullptr;           // the layer they are the drawings of
    int m_layer = 0;
    int m_frame = 0;
    bool m_rebuilding = false;
};
