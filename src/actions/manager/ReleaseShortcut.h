#pragma once
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QShortcutEvent>

class ReleaseShortcut final : public QObject {
public:
    explicit ReleaseShortcut(QAction* action):QObject(action),m_action(action)
    {
        action->setAutoRepeat(false); qApp->installEventFilter(this);
        connect(action,&QAction::triggered,this,[this] { m_armed=false; });
    }
protected:
    bool eventFilter(QObject* watched,QEvent* event) override
    {
        if(watched==m_action && event->type()==QEvent::Shortcut) {
            auto* shortcut=static_cast<QShortcutEvent*>(event);
            if(shortcut->isAmbiguous() || shortcut->key().isEmpty()) return false;
            m_sequence=shortcut->key(); m_key=int(m_sequence[m_sequence.count()-1].key()); m_window=QApplication::activeWindow(); m_armed=true; return true;
        }
        if(!m_armed) return false;
        if(event->type()==QEvent::ApplicationDeactivate || (event->type()==QEvent::WindowDeactivate && watched==m_window)) m_armed=false;
        if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) m_armed=false;
        if(event->type()==QEvent::KeyRelease) {
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==m_key && !key->isAutoRepeat()) {
                m_armed=false;
                if(m_action && m_action->isEnabled() && m_action->shortcut()==m_sequence && QApplication::activeWindow()==m_window) m_action->trigger();
                return true;
            }
        }
        return false;
    }
private:
    QPointer<QAction> m_action;
    QPointer<QWidget> m_window;
    QKeySequence m_sequence;
    int m_key=0;
    bool m_armed=false;
};
