#include "model/document/Commands.h"
#include <QUndoCommand>

void Document::beginMacro(QString const& text)
{
    m_editGroups.push_back({text, false});
}

void Document::endMacro()
{
    if (m_editGroups.isEmpty())
        return;
    bool const opened = m_editGroups.last().opened;
    m_editGroups.removeLast();
    if (opened)
        m_undo.endMacro();
}

void Document::pushUndo(QUndoCommand* command)
{
    for (EditGroup& group : m_editGroups)
        if (!group.opened) {
            m_undo.beginMacro(group.text);
            group.opened = true;
        }
    m_undo.push(command);
}
