// Eclipse - the Camera view: the box the Text tool is typed into.
#include "canvas/TextEntry.h"

#include <algorithm>

#include <QKeyEvent>
#include <QTextCursor>

TextEntry::TextEntry(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(true);
    document()->setDocumentMargin(2.0);
}

void TextEntry::setText(QString const& words)
{
    setPlainText(words);
    moveCursor(QTextCursor::End);
}

void TextEntry::setCursorPosition(int position)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(std::clamp(position, 0, int(toPlainText().size())));
    setTextCursor(cursor);
}

void TextEntry::fit(int least)
{
    QFontMetrics const metrics(font());
    int const lines = std::max(1, int(document()->blockCount()));
    int widest = 0;
    for (QString const& line : toPlainText().split(QLatin1Char('\n')))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    resize(std::max(least, widest + metrics.averageCharWidth() * 3 + 8), lines * metrics.lineSpacing() + 6);
}

void TextEntry::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // Enter: the words are done.  With Shift: a new line of them.
        if (event->modifiers() & Qt::ShiftModifier)
            insertPlainText(QStringLiteral("\n"));
        else
            emit returnPressed();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}
