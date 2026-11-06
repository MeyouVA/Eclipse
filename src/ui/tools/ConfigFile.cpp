// Eclipse - a configuration file (see ConfigFile.h).
#include "ui/tools/ConfigFile.h"

#include "actions/manager/ActionManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

bool ConfigFile::load(QString const& path, QString* error)
{
    m_path = path;
    m_lines.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    parse(QString::fromUtf8(file.readAll()));
    return true;
}

void ConfigFile::parse(QString const& text)
{
    m_lines.clear();
    QString section;
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (QString raw : std::as_const(lines)) {
        if (raw.endsWith(QLatin1Char('\r')))
            raw.chop(1);
        Line line;
        line.raw = raw;
        QString const t = raw.trimmed();
        if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
            section = t.mid(1, t.size() - 2).trimmed();
            line.kind = Line::Section;
        } else if (!t.startsWith(QLatin1Char('#')) && !t.startsWith(QLatin1Char(';')) && t.indexOf(QLatin1Char('=')) > 0) {
            qsizetype const eq = t.indexOf(QLatin1Char('='));
            line.kind = Line::Key;
            line.key = t.left(eq).trimmed();
            line.value = t.mid(eq + 1).trimmed();
        }
        line.section = section;
        m_lines.push_back(line);
    }
}

QString ConfigFile::text() const
{
    QString out;
    for (Line const& line : m_lines)
        out += line.raw + QLatin1Char('\n');
    return out;
}

bool ConfigFile::save(QString const& path, QString* error)
{
    QString const to = path.isEmpty() ? m_path : path;
    QDir().mkpath(QFileInfo(to).absolutePath());
    QSaveFile file(to);
    QByteArray const bytes = text().toUtf8();
    if (to.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = to.isEmpty() ? QObject::tr("No file to save to.") : file.errorString();
        return false;
    }
    m_path = to;
    return true;
}

bool ConfigFile::isEmpty() const
{
    return std::none_of(m_lines.begin(), m_lines.end(), [](Line const& l) { return l.kind != Line::Other; });
}

QStringList ConfigFile::sections() const
{
    QStringList out;
    for (Line const& line : m_lines)
        if ((line.kind == Line::Section || line.kind == Line::Key) && !out.contains(line.section))
            out << line.section;
    return out;
}

QStringList ConfigFile::keys(QString const& section) const
{
    QStringList out;
    for (Line const& line : m_lines)
        if (line.kind == Line::Key && line.section.compare(section, Qt::CaseInsensitive) == 0 && !out.contains(line.key))
            out << line.key;
    return out;
}

int ConfigFile::find(QString const& section, QString const& key) const
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].kind == Line::Key && m_lines[i].section.compare(section, Qt::CaseInsensitive) == 0 && m_lines[i].key.compare(key, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

bool ConfigFile::contains(QString const& section, QString const& key) const
{
    return find(section, key) >= 0;
}

QString ConfigFile::value(QString const& section, QString const& key, QString const& fallback) const
{
    int const at = find(section, key);
    return at < 0 ? fallback : m_lines[at].value;
}

int ConfigFile::intValue(QString const& section, QString const& key, int fallback) const
{
    bool ok = false;
    int const v = value(section, key).toInt(&ok);
    return ok ? v : fallback;
}

bool ConfigFile::boolValue(QString const& section, QString const& key, bool fallback) const
{
    QString const v = value(section, key).toLower();
    if (v == QLatin1String("true") || v == QLatin1String("yes") || v == QLatin1String("on") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("false") || v == QLatin1String("no") || v == QLatin1String("off") || v == QLatin1String("0"))
        return false;
    return fallback;
}

void ConfigFile::setValue(QString const& section, QString const& key, QString const& value)
{
    int const at = find(section, key);
    if (at >= 0) {
        Line& line = m_lines[at];
        line.value = value;
        // The spacing round the = kept as it was.
        qsizetype const eq = line.raw.indexOf(QLatin1Char('='));
        QString const after = line.raw.mid(eq + 1);
        QString const space = after.startsWith(QLatin1Char(' ')) ? QStringLiteral(" ") : QString();
        line.raw = line.raw.left(eq + 1) + space + value;
        return;
    }
    Line line;
    line.kind = Line::Key;
    line.section = section;
    line.key = key;
    line.value = value;
    line.raw = QStringLiteral("%1 = %2").arg(key, value);
    // After the last line of the section (its last key, or its heading).
    int last = -1;
    for (int i = 0; i < m_lines.size(); ++i)
        if (m_lines[i].section.compare(section, Qt::CaseInsensitive) == 0 && m_lines[i].kind != Line::Other)
            last = i;
    if (last < 0 && !section.isEmpty()) {
        if (!m_lines.isEmpty() && !m_lines.last().raw.trimmed().isEmpty())
            m_lines.push_back(Line{QString(), m_lines.last().section});
        Line heading;
        heading.kind = Line::Section;
        heading.section = section;
        heading.raw = QStringLiteral("[%1]").arg(section);
        m_lines.push_back(heading);
        m_lines.push_back(line);
        return;
    }
    if (last < 0) {
        // A key of no section goes before the first section.
        int first = 0;
        while (first < m_lines.size() && m_lines[first].kind != Line::Section)
            ++first;
        m_lines.insert(first, line);
        return;
    }
    m_lines.insert(last + 1, line);
}

bool ConfigFile::remove(QString const& section, QString const& key)
{
    int const at = find(section, key);
    if (at < 0)
        return false;
    m_lines.removeAt(at);
    return true;
}

void ConfigFile::removeSection(QString const& section)
{
    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(), [&](Line const& l) {
                      return l.kind != Line::Other && l.section.compare(section, Qt::CaseInsensitive) == 0;
                  }),
                  m_lines.end());
}

QString ConfigFile::note(QString const& section, QString const& key) const
{
    int const at = find(section, key);
    QStringList notes;
    for (int i = at - 1; i >= 0 && m_lines[i].kind == Line::Other; --i) {
        QString const t = m_lines[i].raw.trimmed();
        if (!t.startsWith(QLatin1Char('#')) && !t.startsWith(QLatin1Char(';')))
            break;
        notes.prepend(t.mid(1).trimmed());
    }
    return notes.join(QLatin1Char('\n'));
}

QString ConfigFile::managerFile()
{
    return ActionManager::defaultDirectory() + QStringLiteral("/manager.conf");
}

QString ConfigFile::manager(QString const& section, QString const& key, QString const& fallback)
{
    QString const value = ConfigFile(managerFile()).value(section, key);
    return value.isEmpty() ? fallback : value;
}
