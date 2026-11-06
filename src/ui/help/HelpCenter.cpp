// Eclipse - the help (see HelpCenter.h).
#include "ui/help/HelpCenter.h"

#include "actions/manager/ActionManager.h"
#include "ui/tools/CommandLine.h"
#include "ui/tools/ConfigFile.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>

#include <algorithm>

namespace HelpCenter {

namespace {

QString const kStyle = QStringLiteral(
    "<style>body { background: #2b2b2e; color: #d6d8dc; font-family: 'Segoe UI', sans-serif; font-size: 10pt; margin: 14px; } "
    "h1 { color: #ffffff; font-size: 16pt; } h2 { color: #7fd0de; font-size: 12pt; margin-top: 16px; } a { color: #6fc3d6; } "
    "b { color: #ffffff; } td { padding: 2px 8px 2px 0; vertical-align: top; } .keys { color: #f0c674; }</style>");

QString readText(QString const& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString plain(QString const& html)
{
    QString text = html;
    text.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"), QRegularExpression::DotMatchesEverythingOption));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
    text = QTextDocumentFragment::fromHtml(text).toPlainText();
    return text.simplified();
}

QString escape(QString const& text) { return text.toHtmlEscaped(); }

bool write(QString const& path, QString const& html)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(html.toUtf8());
    return file.commit();
}

// The anchors of a page.
QSet<QString> anchorsOf(QString const& path)
{
    QSet<QString> out;
    QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("(?:name|id)=\"([^\"]+)\"")).globalMatch(readText(path));
    while (it.hasNext())
        out.insert(it.next().captured(1));
    return out;
}

QString pagePath(QString const& page)
{
    QString const name = page.section(QLatin1Char('#'), 0, 0);
    for (QString const& folder : searchPaths())
        if (QFileInfo::exists(QDir(folder).filePath(name)))
            return QDir(folder).filePath(name);
    return QString();
}

void walkMenu(QMenu* menu, QString const& path, QString& html, int& count)
{
    QString rows;
    QList<QPair<QMenu*, QString>> subs;
    for (QAction* action : menu->actions()) {
        if (action->isSeparator())
            continue;
        QString const text = action->text().remove(QLatin1Char('&'));
        if (action->menu()) {
            subs.push_back({action->menu(), path + QStringLiteral(" > ") + text});
            continue;
        }
        QString const id = action->objectName();
        if (id.isEmpty() || id.startsWith(QLatin1String("placeholder:")))
            continue;
        QString const keys = action->shortcut().toString(QKeySequence::NativeText);
        QString const tip = action->toolTip().remove(QLatin1Char('&')) == text ? QString() : action->toolTip();
        rows += QStringLiteral("<tr><td><a name=\"%1\"></a><b>%2</b></td><td class=\"keys\">%3</td><td>%4</td></tr>\n")
                    .arg(escape(id), escape(text), escape(keys), escape(tip));
        ++count;
    }
    if (!rows.isEmpty())
        html += QStringLiteral("<h2>%1</h2>\n<table>\n%2</table>\n").arg(escape(path), rows);
    for (auto const& [sub, name] : subs)
        walkMenu(sub, name, html, count);
}

// What a preference widget is called: its own text, or the label of its row.
QString labelOf(QWidget* widget)
{
    if (auto* const button = qobject_cast<QAbstractButton*>(widget); button && !button->text().isEmpty())
        return button->text().remove(QLatin1Char('&'));
    for (QWidget* p = widget->parentWidget(); p; p = p->parentWidget())
        if (auto* const form = qobject_cast<QFormLayout*>(p->layout()))
            if (QWidget* const label = form->labelForField(widget))
                if (auto* const l = qobject_cast<QLabel*>(label))
                    return l->text().remove(QLatin1Char('&')).remove(QRegularExpression(QStringLiteral(":\\s*$")));
    return QString();
}

QHash<QString, QSet<QString>>& anchorCache()
{
    static QHash<QString, QSet<QString>> cache;
    return cache;
}

}  // namespace

QString englishFolder()
{
    return ActionManager::defaultDirectory() + QStringLiteral("/help");
}

QString folder()
{
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    QString const language = s.value(QStringLiteral("prefs/language")).toString();
    if (!language.isEmpty() && !language.startsWith(QLatin1String("en")) && !s.value(QStringLiteral("prefs/helpInEnglish"), false).toBool()) {
        QString const own = englishFolder() + QLatin1Char('/') + language;
        if (QFileInfo::exists(own + QStringLiteral("/index.html")))
            return own;
    }
    return englishFolder();
}

QString generatedFolder()
{
    return CommandLine::dataFolder() + QStringLiteral("/help");
}

QStringList searchPaths()
{
    QStringList paths{folder()};
    if (paths.first() != englishFolder())
        paths << englishFolder();
    paths << generatedFolder();
    return paths;
}

bool pageExists(QString const& page)
{
    return !pagePath(page).isEmpty();
}

QString writeCommandsPage(QMenuBar* bar)
{
    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"><title>Commands</title>%1</head><body>\n<h1>Commands</h1>\n"
                                  "<p>Every command of the menus: its name, its keys (Edit > Keyboard Shortcuts changes them) and what it does.</p>\n")
                       .arg(kStyle);
    int count = 0;
    if (bar)
        for (QAction* top : bar->actions())
            if (top->menu())
                walkMenu(top->menu(), top->text().remove(QLatin1Char('&')), html, count);
    html += QStringLiteral("<p><a href=\"index.html\">Contents</a></p></body></html>\n");
    QString const path = generatedFolder() + QStringLiteral("/commands.html");
    write(path, html);
    anchorCache().remove(path);
    return path;
}

QString writePreferencesPage(QWidget* dialog)
{
    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"><title>Preferences</title>%1</head><body>\n<h1>Preferences</h1>\n"
                                  "<p>Every preference (Edit > Preferences), page by page.</p>\n")
                       .arg(kStyle);
    QTabWidget* const tabs = dialog ? dialog->findChild<QTabWidget*>(QStringLiteral("PreferencesTabs")) : nullptr;
    if (tabs) {
        for (int t = 0; t < tabs->count(); ++t) {
            QString rows;
            QSet<QString> seen;
            for (QWidget* w : tabs->widget(t)->findChildren<QWidget*>()) {
                QString const name = w->objectName();
                bool const control = qobject_cast<QAbstractButton*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w)
                                     || qobject_cast<QLineEdit*>(w) || qobject_cast<QSlider*>(w);
                if (!control || name.isEmpty() || name.startsWith(QLatin1String("qt_")) || seen.contains(name))
                    continue;
                seen.insert(name);
                QString const label = labelOf(w);
                if (label.isEmpty() && w->toolTip().isEmpty())
                    continue;
                rows += QStringLiteral("<tr><td><a name=\"%1\"></a><b>%2</b></td><td>%3</td></tr>\n")
                            .arg(escape(name), escape(label.isEmpty() ? name : label), escape(w->toolTip()));
            }
            if (!rows.isEmpty())
                html += QStringLiteral("<h2>%1</h2>\n<table>\n%2</table>\n").arg(escape(tabs->tabText(t).remove(QLatin1Char('&'))), rows);
        }
    }
    html += QStringLiteral("<p><a href=\"index.html\">Contents</a></p></body></html>\n");
    QString const path = generatedFolder() + QStringLiteral("/preferences.html");
    write(path, html);
    anchorCache().remove(path);
    return path;
}

QHash<QString, QString> topics()
{
    QHash<QString, QString> out;
    QFile file(englishFolder() + QStringLiteral("/topics.xml"));
    if (!file.open(QIODevice::ReadOnly))
        return out;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd())
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("topic")) {
            QString const page = xml.attributes().value(QStringLiteral("page")).toString();
            for (QString const& id : xml.attributes().value(QStringLiteral("id")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts))
                out.insert(id, page);
        }
    return out;
}

QString topicFor(QString const& id)
{
    if (id.isEmpty())
        return QString();
    static QHash<QString, QString> const known = topics();
    if (auto it = known.constFind(id); it != known.constEnd())
        return it.value();
    for (QString const& page : {QStringLiteral("commands.html"), QStringLiteral("preferences.html")}) {
        QString const path = generatedFolder() + QLatin1Char('/') + page;
        if (!anchorCache().contains(path))
            anchorCache().insert(path, anchorsOf(path));
        if (anchorCache().value(path).contains(id))
            return page + QLatin1Char('#') + id;
    }
    return QString();
}

QString topicOfAction(QAction* action)
{
    return action ? topicFor(action->objectName()) : QString();
}

QString topicAt(QWidget* widget, QString* found)
{
    // A command under the pointer in an open menu.
    if (auto* const menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
        if (QAction* const action = menu->activeAction(); action && !action->menu())
            if (QString const topic = topicOfAction(action); !topic.isEmpty()) {
                if (found)
                    *found = action->objectName();
                return topic;
            }
    for (QWidget* w = widget; w; w = w->parentWidget()) {
        if (QString const topic = topicFor(w->objectName()); !topic.isEmpty()) {
            if (found)
                *found = w->objectName();
            return topic;
        }
    }
    return QString();
}

QString summary(QString const& topic)
{
    QString const html = readText(pagePath(topic));
    if (html.isEmpty())
        return QString();
    QString const part = topic.section(QLatin1Char('#'), 1);
    qsizetype start = 0;
    if (!part.isEmpty()) {
        QRegularExpression const at(QStringLiteral("(?:name|id)=\"%1\"").arg(QRegularExpression::escape(part)));
        QRegularExpressionMatch const m = at.match(html);
        if (m.hasMatch())
            start = m.capturedEnd();
    } else {
        start = std::max<qsizetype>(0, html.indexOf(QLatin1String("</h1>")));
    }
    // To the next part: a row of a table, the next anchor or heading.
    QRegularExpression const end(QStringLiteral("</tr>|<h2|(?:name|id)=\""));
    QRegularExpressionMatch const m = end.match(html, start + 1);
    QString const text = plain(html.mid(start, (m.hasMatch() ? m.capturedStart() : html.size()) - start).section(QLatin1Char('>'), 1));
    return text.left(400);
}

QString link(QString const& name)
{
    return ConfigFile(ActionManager::defaultDirectory() + QStringLiteral("/links.conf")).value(QStringLiteral("links"), name);
}

QVector<Hit> search(QString const& words)
{
    QStringList const terms = words.toLower().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
    QVector<Hit> hits;
    if (terms.isEmpty())
        return hits;
    QSet<QString> pages;
    for (QString const& folderPath : searchPaths())
        for (QString const& name : QDir(folderPath).entryList({QStringLiteral("*.html")}, QDir::Files))
            if (!pages.contains(name)) {
                pages.insert(name);
                QString const html = readText(QDir(folderPath).filePath(name));
                QString const title = plain(html.section(QLatin1String("<title>"), 1).section(QLatin1String("</title>"), 0, 0));
                // Each part: from one heading (or row of a table) to the next.
                QStringList const parts = html.split(QRegularExpression(QStringLiteral("(?=<h2)|(?=<tr>)")));
                for (QString const& part : parts) {
                    QString const text = plain(part);
                    QString const lower = text.toLower();
                    int score = 0;
                    bool all = true;
                    for (QString const& t : terms) {
                        int const n = int(lower.count(t));
                        all = all && n > 0;
                        score += n;
                    }
                    if (!all)
                        continue;
                    Hit hit;
                    QRegularExpressionMatch const anchor = QRegularExpression(QStringLiteral("(?:name|id)=\"([^\"]+)\"")).match(part);
                    hit.page = anchor.hasMatch() ? name + QLatin1Char('#') + anchor.captured(1) : name;
                    QRegularExpressionMatch const heading = QRegularExpression(QStringLiteral("<h2[^>]*>(.*?)</h2>|<b>(.*?)</b>")).match(part);
                    hit.title = heading.hasMatch() ? QStringLiteral("%1: %2").arg(title, plain(heading.captured(1) + heading.captured(2))) : title;
                    qsizetype const at = std::max<qsizetype>(0, lower.indexOf(terms.first()) - 60);
                    hit.text = text.mid(at, 180);
                    hit.score = score + (lower.left(80).contains(terms.first()) ? 5 : 0);
                    hits << hit;
                }
            }
    std::stable_sort(hits.begin(), hits.end(), [](Hit const& a, Hit const& b) { return a.score > b.score; });
    return hits;
}

QVector<QPair<QString, QString>> contents()
{
    QVector<QPair<QString, QString>> out;
    QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("<a href=\"([^\"#]+\\.html)\">([^<]+)</a>")).globalMatch(readText(pagePath(QStringLiteral("index.html"))));
    while (it.hasNext()) {
        QRegularExpressionMatch const m = it.next();
        out.push_back({m.captured(1), m.captured(2)});
    }
    return out;
}

}  // namespace HelpCenter
