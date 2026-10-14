// Eclipse - Pose Controllers: sets of layers with poses kept under names, and a slider that blends between them.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class Document;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class Timeline;

class ECLIPSE_UI_API ControllerView : public QWidget {
    Q_OBJECT

public:
    ControllerView(Document* document, Timeline* timeline, QWidget* parent = nullptr);

    // The controller chosen (its place in the scene's list), or -1.
    int currentController() const;
    // The slider moved to `value` (0: the first state, 1: the second ...): the controller is set on the current frame.
    void setValue(double value);

public slots:
    void newController();
    void deleteController();
    void addState();
    void replaceState();
    void removeState();
    void refresh();

private:
    Document* m_document;
    Timeline* m_timeline;
    QListWidget* m_controllers;
    QListWidget* m_states;
    QSlider* m_slider;
    QLabel* m_value;
    bool m_refreshing = false;
};
