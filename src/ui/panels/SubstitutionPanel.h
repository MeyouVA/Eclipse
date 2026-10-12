// Eclipse - the Drawing Substitution view: every drawing of the current layer as a small
// picture.  Clicking one shows it on the current frame in place of the drawing there -
// how a cut-out character changes hand, mouth or eye without redrawing.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QListWidget;
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

    Document* m_document;
    QListWidget* m_list;
    int m_layer = 0;
    int m_frame = 0;
    bool m_rebuilding = false;
};
