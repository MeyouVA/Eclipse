// Eclipse - a collapse handle: the pair of arrows that stands on the line between two things in the
// window.  Each arrow pushes that line its own way: it folds away what lies on the side it points
// to, or brings back what was folded away on the other.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class QToolButton;

class ECLIPSE_UI_API CollapseHandle : public QWidget {
    Q_OBJECT
public:
    explicit CollapseHandle(QWidget* parent = nullptr);

    void setStacked(bool stacked);
    bool stacked() const { return m_stacked; }
    // The size it wants, the way it lies now.
    QSize handleSize() const;
    // Which of the two arrows has something to do just now.
    void setArrowsEnabled(bool first, bool second);

    QToolButton* first() const { return m_first; }      // points left, or up
    QToolButton* second() const { return m_second; }    // points right, or down

signals:
    void pressed(bool second);

private:
    QToolButton* m_first;
    QToolButton* m_second;
    bool m_stacked = false;
};
