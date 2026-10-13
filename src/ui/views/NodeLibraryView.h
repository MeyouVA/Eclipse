// Eclipse - Node Library: the kinds of layer and effect there are, to add from.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

class QLineEdit;
class QListWidget;

class ECLIPSE_UI_API NodeLibraryView : public QWidget {
    Q_OBJECT

public:
    explicit NodeLibraryView(QWidget* parent = nullptr);
    // Adds what is named: a kind of layer ("InsertPeg" ...) or an effect ("effect:0" ...).
    void add(QString const& what);

signals:
    // A command of the program is asked for (the layers are added by their Insert commands).
    void actionRequested(QString const& actionId);
    // An effect (Effect::Type) is to be added to the current layer.
    void effectRequested(int type);

private:
    void filter(QString const& text);
    QLineEdit* m_search;
    QListWidget* m_list;
    QListWidget* m_categories = nullptr;
};
