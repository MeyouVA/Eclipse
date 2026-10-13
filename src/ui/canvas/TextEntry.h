// Eclipse - the Camera view: the box the Text tool is typed into, its lines one under the other.
#pragma once

#include <QPlainTextEdit>

#include "EclipseExport.h"

class ECLIPSE_UI_API TextEntry : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit TextEntry(QWidget* parent = nullptr);

    QString text() const { return toPlainText(); }
    void setText(QString const& words);
    // Where the next letter typed goes: so many letters into the words.
    void setCursorPosition(int position);
    // As high as its lines need, at least `least` wide.
    void fit(int least);

signals:
    void returnPressed();       // Enter: the words are done

protected:
    void keyPressEvent(QKeyEvent* event) override;
};
