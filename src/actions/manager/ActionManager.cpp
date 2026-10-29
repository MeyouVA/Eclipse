#include "actions/manager/ActionManager.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QMetaMethod>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QXmlStreamReader>

#include <QMouseEvent>

#include "actions/dialogs/ToolbarDialog.h"
#include "actions/theme/Theme.h"

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}

QString ActionManager::defaultDirectory()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
}

void ActionManager::registerResponder(QString const& name, QObject* responder)
{
    m_responders.insert(name, responder);
}

// ---- reading ------------------------------------------------------------------------------

ActionManager::Node ActionManager::readNode(QXmlStreamReader& xml)
{
    static QHash<QString, Node::Type> const types{
        {QStringLiteral("menu"), Node::Menu},           {QStringLiteral("item"), Node::Item},
        {QStringLiteral("separator"), Node::Separator}, {QStringLiteral("placeholder"), Node::Placeholder},
        {QStringLiteral("submenu"), Node::Submenu},     {QStringLiteral("multibutton"), Node::MultiButton},
        {QStringLiteral("toolbar"), Node::Toolbar}};
    Node node;
    node.type = types.value(xml.name().toString(), Node::Item);
    for (QXmlStreamAttribute const& a : xml.attributes())
        node.attr.insert(a.name().toString(), a.value().toString());
    while (xml.readNextStartElement())
        node.children.push_back(readNode(xml));
    return node;
}

bool ActionManager::readFile(QString const& path, QString const& root, Node& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != root) {
        if (error)
            *error = tr("%1 is not a <%2> file.").arg(path, root);
        return false;
    }
    out = readNode(xml);
    if (xml.hasError()) {
        if (error)
            *error = tr("%1, line %2: %3").arg(path).arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    return true;
}

bool ActionManager::loadShortcuts(QString const& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot read %1: %2").arg(path, file.errorString());
        return false;
    }
    // The keys the user changed, kept apart from the file so that it can be replaced.
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    settings.beginGroup(QStringLiteral("shortcuts"));

    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("shortcuts")) {
        if (error)
            *error = tr("%1 is not a <shortcuts> file.").arg(path);
        return false;
    }
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("category")) {
            xml.skipCurrentElement();
            continue;
        }
        QString const category = xml.attributes().value(QLatin1String("text")).toString();
        m_categories << category;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("shortcut")) {
                Shortcut s;
                s.id = xml.attributes().value(QLatin1String("id")).toString();
                s.text = xml.attributes().value(QLatin1String("text")).toString();
                s.category = category;
                s.defaultKey = QKeySequence(xml.attributes().value(QLatin1String("value")).toString(), QKeySequence::PortableText);
                s.key = settings.contains(s.id) ? QKeySequence(settings.value(s.id).toString(), QKeySequence::PortableText) : s.defaultKey;
                m_shortcuts.push_back(s);
            }
            xml.skipCurrentElement();
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = tr("%1, line %2: %3").arg(path).arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    return true;
}

void ActionManager::defineItems(Node const& node)
{
    if (node.type == Node::Item && node.attr.contains(QStringLiteral("slot")))
        m_items.insert(node.value("id"), node);
    for (Node const& child : node.children)
        defineItems(child);
}

bool ActionManager::load(QString const& directory, QString* error)
{
    m_directory = directory;
    Node menus, toolbars;
    if (!loadShortcuts(directory + QStringLiteral("/shortcuts.xml"), error)
        || !readFile(directory + QStringLiteral("/menus.xml"), QStringLiteral("menus"), menus, error)
        || !readFile(directory + QStringLiteral("/toolbars.xml"), QStringLiteral("toolbars"), toolbars, error))
        return false;
    for (Node const& menu : menus.children)
        m_menus.insert(menu.value("id"), menu);
    for (Node const& toolbar : toolbars.children) {
        m_toolbars.insert(toolbar.value("id"), toolbar);
        m_toolbarOrder << toolbar.value("id");
    }
    defineItems(menus);
    defineItems(toolbars);
    for (Node const& item : std::as_const(m_items))
        actionFor(item);
    return true;
}

// ---- commands -----------------------------------------------------------------------------

void ActionManager::applyShortcut(QAction* action, QString const& shortcutId)
{
    QKeySequence key;
    for (Shortcut const& s : std::as_const(m_shortcuts))
        if (s.id == shortcutId)
            key = s.key;
    action->setShortcut(key);
    // A button shows only its icon: its tip names the command and the key for it.
    QString tip = action->text().remove(QLatin1Char('&')).remove(QStringLiteral("..."));
    if (!key.isEmpty())
        tip += QStringLiteral("  (%1)").arg(key.toString(QKeySequence::NativeText));
    action->setToolTip(tip);
}

QAction* ActionManager::actionFor(Node const& reference)
{
    QString const id = reference.value("id");
    if (QAction* const existing = m_actions.value(id))
        return existing;
    // A bare <item id="..."/> stands for the command defined elsewhere.
    auto const found = m_items.constFind(id);
    if (found == m_items.constEnd())
        return nullptr;
    Node const item = *found;

    QAction* const action = new QAction(item.value("text"), this);
    action->setObjectName(id);
    if (item.attr.contains(QStringLiteral("icon")))
        action->setIcon(Theme::icon(m_directory, item.value("icon")));
    if (item.attr.contains(QStringLiteral("tooltip")))
        action->setToolTip(item.value("tooltip"));
    if (item.value("checkable") == QLatin1String("true")) {
        action->setCheckable(true);
        action->setChecked(item.value("checked") == QLatin1String("true"));
    }
    QString const group = item.value("group");
    if (!group.isEmpty()) {
        QActionGroup*& g = m_groups[group];
        if (!g)
            g = new QActionGroup(this);
        g->addAction(action);
    }
    QString const shortcut = item.value("shortcut");
    if (!shortcut.isEmpty())
        m_actionShortcut.insert(id, shortcut);
    applyShortcut(action, shortcut);
    if (item.attr.contains(QStringLiteral("tooltip")))
        action->setToolTip(item.value("tooltip"));
    connect(action, &QAction::triggered, this, [this, item, id](bool checked) {
        QElapsedTimer clock;
        clock.start();
        invoke(item, checked, nullptr);
        emit commandRun(id, clock.elapsed());
    });
    m_actions.insert(id, action);
    return action;
}

QObject* ActionManager::resolve(QString const& responders, QByteArray const& slot) const
{
    QStringList names = responders.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (names.isEmpty())
        names << QStringLiteral("window");
    for (QString const& raw : std::as_const(names)) {
        QString const name = raw.trimmed();
        if (name == QLatin1String("focus")) {
            // The view in use comes first: the responder that sits in it.
            if (m_activeView) {
                for (QPointer<QObject> const& r : m_responders) {
                    QWidget* const w = qobject_cast<QWidget*>(r.data());
                    if (w && (w == m_activeView || m_activeView->isAncestorOf(w)) && w->metaObject()->indexOfMethod(slot.constData()) >= 0)
                        return w;
                }
            }
            for (QWidget* w = QApplication::focusWidget(); w; w = w->parentWidget()) {
                for (QPointer<QObject> const& r : m_responders)
                    if (r == w && w->metaObject()->indexOfMethod(slot.constData()) >= 0)
                        return w;
            }
            continue;
        }
        QObject* const target = m_responders.value(name);
        if (target && target->metaObject()->indexOfMethod(slot.constData()) >= 0)
            return target;
    }
    return nullptr;
}

void ActionManager::invoke(Node const& item, bool checked, QMenu* menu)
{
    QByteArray const slot = QMetaObject::normalizedSignature(item.value(menu ? "aboutToShowSlot" : "slot").toLatin1().constData());
    QObject* const target = resolve(item.value("responder"), slot);
    if (!target)
        return;
    QMetaMethod const method = target->metaObject()->method(target->metaObject()->indexOfMethod(slot.constData()));
    if (method.parameterCount() == 0) {
        method.invoke(target, Qt::DirectConnection);
        return;
    }
    QByteArray const type = method.parameterTypes().first();
    QString const parameter = item.value("itemParameter");
    if (type == "bool")
        method.invoke(target, Qt::DirectConnection, Q_ARG(bool, checked));
    else if (type == "int")
        method.invoke(target, Qt::DirectConnection, Q_ARG(int, parameter.toInt()));
    else if (type == "QString")
        method.invoke(target, Qt::DirectConnection, Q_ARG(QString, parameter));
    else if (type == "QMenu*")
        method.invoke(target, Qt::DirectConnection, Q_ARG(QMenu*, menu));
}

void ActionManager::setChecked(QString const& id, bool checked)
{
    if (QAction* const a = m_actions.value(id))
        a->setChecked(checked);
}

void ActionManager::trigger(QString const& id)
{
    if (QAction* const a = m_actions.value(id))
        a->trigger();
}

// ---- building -----------------------------------------------------------------------------

void ActionManager::fillMenu(QMenu* menu, Node const& node)
{
    for (Node const& child : node.children) {
        switch (child.type) {
        case Node::Item:
            if (QAction* const a = actionFor(child))
                menu->addAction(a);
            break;
        case Node::Separator:
            menu->addSeparator();
            break;
        case Node::Menu: {
            QMenu* const sub = menu->addMenu(child.value("text"));
            fillMenu(sub, child);
            if (child.attr.contains(QStringLiteral("aboutToShowSlot")))
                connect(sub, &QMenu::aboutToShow, this, [this, child, sub] { invoke(child, false, sub); });
            break;
        }
        case Node::Submenu: {
            auto const shared = m_menus.constFind(child.value("id"));
            if (shared != m_menus.constEnd()) {
                QMenu* const sub = menu->addMenu(child.value("text"));
                fillMenu(sub, *shared);
                if (shared->attr.contains(QStringLiteral("aboutToShowSlot")))
                    connect(sub, &QMenu::aboutToShow, this, [this, node = *shared, sub] { invoke(node, false, sub); });
            }
            break;
        }
        case Node::Placeholder:
            if (child.attr.contains(QStringLiteral("aboutToShowSlot"))) {
                connect(menu, &QMenu::aboutToShow, this, [this, child, menu] {
                    QList<QAction*> const old = menu->findChildren<QAction*>(QStringLiteral("placeholder:") + child.value("id"));
                    for (QAction* a : old) {
                        menu->removeAction(a);
                        a->deleteLater();
                    }
                    QList<QAction*> const before = menu->actions();
                    invoke(child, false, menu);
                    for (QAction* a : menu->actions()) {
                        if (!before.contains(a)) {
                            a->setParent(menu);
                            a->setObjectName(QStringLiteral("placeholder:") + child.value("id"));
                        }
                    }
                });
            }
            break;
        default:
            break;
        }
    }
}

void ActionManager::populateMenuBar(QString const& menuId, QMenuBar* bar)
{
    auto const found = m_menus.constFind(menuId);
    if (found == m_menus.constEnd())
        return;
    for (Node const& child : found->children) {
        if (child.type != Node::Menu)
            continue;
        QMenu* const menu = bar->addMenu(child.value("text"));
        menu->menuAction()->setProperty("title", child.value("text"));
        fillMenu(menu, child);
        if (child.attr.contains(QStringLiteral("aboutToShowSlot")))
            connect(menu, &QMenu::aboutToShow, this, [this, child, menu] { invoke(child, false, menu); });
    }
    m_menuBar = bar;
    refreshMnemonics();
}

void ActionManager::refreshMnemonics()
{
    if (!m_menuBar)
        return;
    QSet<QKeySequence> taken;
    for (QAction const* a : std::as_const(m_actions))
        for (QKeySequence const& key : a->shortcuts())
            taken.insert(key);
    for (QAction* title : m_menuBar->actions()) {
        QString const text = title->property("title").toString();
        int const at = text.indexOf(QLatin1Char('&'));
        if (text.isEmpty() || at < 0 || at + 1 >= text.size())
            continue;
        bool const clashes = taken.contains(QKeySequence(QStringLiteral("Alt+") + text.at(at + 1).toUpper()));
        title->setText(clashes ? QString(text).remove(at, 1) : text);
    }
}

QMenu* ActionManager::createMenu(QString const& menuId, QWidget* parent)
{
    auto const found = m_menus.constFind(menuId);
    if (found == m_menus.constEnd())
        return nullptr;
    QMenu* const menu = new QMenu(parent);
    fillMenu(menu, *found);
    if (found->attr.contains(QStringLiteral("aboutToShowSlot")))
        connect(menu, &QMenu::aboutToShow, this, [this, node = *found, menu] { invoke(node, false, menu); });
    return menu;
}

QString const ActionManager::kSeparatorEntry = QStringLiteral("|");

QString ActionManager::toolbarText(QString const& toolbarId) const
{
    return m_toolbars.value(toolbarId).value("text");
}

bool ActionManager::toolbarCustomizable(QString const& toolbarId) const
{
    return m_toolbars.contains(toolbarId) && m_toolbars.value(toolbarId).value("customizable") != QLatin1String("false");
}

ActionManager::Node const* ActionManager::toolbarChild(QString const& toolbarId, QString const& entry) const
{
    auto const found = m_toolbars.constFind(toolbarId);
    if (found == m_toolbars.constEnd())
        return nullptr;
    for (Node const& child : found->children)
        if (child.type != Node::Separator && child.value("id") == entry)
            return &child;
    return nullptr;
}

QStringList ActionManager::toolbarDefaultEntries(QString const& toolbarId) const
{
    QStringList out;
    for (Node const& child : m_toolbars.value(toolbarId).children) {
        if (child.type == Node::Separator)
            out << kSeparatorEntry;
        else if (child.value("visible") != QLatin1String("false"))
            out << child.value("id");
    }
    return out;
}

QStringList ActionManager::toolbarAvailable(QString const& toolbarId) const
{
    QStringList out;
    for (Node const& child : m_toolbars.value(toolbarId).children)
        if (child.type != Node::Separator)
            out << child.value("id");
    return out;
}

QStringList ActionManager::toolbarEntries(QString const& toolbarId) const
{
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QString const key = QStringLiteral("toolbars/") + toolbarId;
    if (!settings.contains(key))
        return toolbarDefaultEntries(toolbarId);
    // What the user chose, less anything a newer definition file no longer has.
    QStringList out;
    for (QString const& entry : settings.value(key).toStringList())
        if (entry == kSeparatorEntry || toolbarChild(toolbarId, entry))
            out << entry;
    return out;
}

QString ActionManager::toolbarEntryText(QString const& toolbarId, QString const& entry) const
{
    if (entry == kSeparatorEntry)
        return tr("--- separator ---");
    Node const* const child = toolbarChild(toolbarId, entry);
    if (!child)
        return entry;
    auto text = [this](Node const& item) {
        QAction* const a = m_actions.value(item.value("id"));
        return a ? a->text().remove(QLatin1Char('&')).remove(QStringLiteral("...")) : item.value("id");
    };
    if (child->type != Node::MultiButton)
        return text(*child);
    QStringList names;
    for (Node const& tool : child->children)
        names << text(tool);
    return names.join(QStringLiteral(" / "));
}

QIcon ActionManager::toolbarEntryIcon(QString const& toolbarId, QString const& entry) const
{
    Node const* child = toolbarChild(toolbarId, entry);
    if (child && child->type == Node::MultiButton && !child->children.isEmpty())
        child = &child->children.first();
    QAction* const a = child ? m_actions.value(child->value("id")) : nullptr;
    return a ? a->icon() : QIcon();
}

void ActionManager::setToolbarEntries(QString const& toolbarId, QStringList const& entries)
{
    QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QString const key = QStringLiteral("toolbars/") + toolbarId;
    if (entries == toolbarDefaultEntries(toolbarId))
        settings.remove(key);
    else
        settings.setValue(key, entries);
    for (QPointer<QToolBar> const& bar : std::as_const(m_liveToolbars))
        if (bar && bar->objectName() == toolbarId)
            populateToolbar(bar);
}

void ActionManager::customizeToolbar(QString const& toolbarId, QWidget* parent)
{
    ToolbarDialog dialog(this, toolbarId, parent);
    dialog.exec();
}

namespace {
class GroupButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && menu() && defaultAction() && defaultAction()->isChecked()) {
            setDown(false);
            showMenu();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
};
}  // namespace

void ActionManager::populateToolbar(QToolBar* bar)
{
    QString const toolbarId = bar->objectName();
    static char const* const kOurs = "eclipseToolbarEntry";
    QAction* before = nullptr;
    for (QAction* a : bar->actions()) {
        if (a->property(kOurs).toBool()) {
            bar->removeAction(a);
            if (a->parent() == bar)
                a->deleteLater();  // a separator or the holder of a group button; commands live on
        }
    }
    // Commands are shared with the menus, so they cannot carry the mark: remove them by list.
    for (QAction* a : bar->actions())
        if (m_actions.value(a->objectName()) == a)
            bar->removeAction(a);
    if (!bar->actions().isEmpty())
        before = bar->actions().first();

    auto prepare = [this](Node const& n) -> QAction* {
        QAction* const a = actionFor(n);
        // What a button shows when it has no icon; menus keep the full text.
        if (a && n.attr.contains(QStringLiteral("toolbarText")))
            a->setIconText(n.value("toolbarText"));
        return a;
    };
    for (QString const& entry : toolbarEntries(toolbarId)) {
        if (entry == kSeparatorEntry) {
            bar->insertSeparator(before)->setProperty(kOurs, true);
            continue;
        }
        Node const* const child = toolbarChild(toolbarId, entry);
        if (!child)
            continue;
        if (child->type == Node::Item) {
            if (QAction* const a = prepare(*child))
                bar->insertAction(before, a);
            continue;
        }
        QList<QAction*> tools;
        for (Node const& tool : child->children)
            if (QAction* const a = tool.value("visible") != QLatin1String("false") ? prepare(tool) : nullptr)
                tools << a;
        if (tools.isEmpty())
            continue;
        if (tools.size() == 1) {
            bar->insertAction(before, tools.first());
            continue;
        }
        // One button for several tools: it shows the one used last; clicking it again, or holding it, offers the others.
        QToolButton* const button = new GroupButton(bar);
        button->setObjectName(QStringLiteral("toolGroup:") + child->value("id"));
        QMenu* const menu = new QMenu(button);
        menu->addActions(tools);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::DelayedPopup);
        // The tool in use, if it is one of these; else the first.
        QAction* current = tools.first();
        for (QAction* a : std::as_const(tools))
            if (a->isChecked())
                current = a;
        button->setDefaultAction(current);
        for (QAction* a : std::as_const(tools))
            connect(a, &QAction::triggered, button, [button, a] { button->setDefaultAction(a); });
        bar->insertWidget(before, button)->setProperty(kOurs, true);
    }
}

QToolBar* ActionManager::createToolbar(QString const& toolbarId, QWidget* parent)
{
    auto const found = m_toolbars.constFind(toolbarId);
    if (found == m_toolbars.constEnd())
        return nullptr;
    QToolBar* const bar = new QToolBar(found->value("text"), parent);
    bar->setObjectName(toolbarId);
    if (found->value("orientation") == QLatin1String("vertical"))
        bar->setOrientation(Qt::Vertical);
    m_liveToolbars << bar;
    populateToolbar(bar);
    if (toolbarCustomizable(toolbarId)) {
        // Right-click: change what the toolbar shows.
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QWidget::customContextMenuRequested, this, [this, bar, toolbarId](QPoint const& pos) {
            QMenu menu(bar);
            QAction* const customize = menu.addAction(tr("Customize..."));
            if (menu.exec(bar->mapToGlobal(pos)) == customize)
                customizeToolbar(toolbarId, bar->window());
        });
    }
    return bar;
}

void ActionManager::installShortcuts(QWidget* window)
{
    for (QAction* a : std::as_const(m_actions))
        window->addAction(a);
}

// ---- shortcuts ----------------------------------------------------------------------------

void ActionManager::setShortcut(QString const& id, QKeySequence const& key)
{
    for (Shortcut& s : m_shortcuts) {
        if (s.id != id)
            continue;
        s.key = key;
        QSettings settings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
        settings.beginGroup(QStringLiteral("shortcuts"));
        if (key == s.defaultKey)
            settings.remove(id);
        else
            settings.setValue(id, key.toString(QKeySequence::PortableText));
    }
    for (auto it = m_actionShortcut.constBegin(); it != m_actionShortcut.constEnd(); ++it)
        if (it.value() == id)
            applyShortcut(m_actions.value(it.key()), id);
    refreshMnemonics();
}
