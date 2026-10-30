// Eclipse - a small YAML reader: block maps and sequences, flow maps and lists, tags, quoted and block text.
#include "colour/config/Yaml.h"

#include <QCoreApplication>

namespace {

using Cms::Yaml::Node;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

struct Line {
    int indent = 0;
    QString text;       // without the indentation
    int number = 0;
    bool blank = false; // nothing, or only a comment
};

bool isQuote(QChar c)
{
    return c == QLatin1Char('"') || c == QLatin1Char('\'');
}

// A quote opens a quoted text only where a value can start.
bool opensQuote(QString const& s, int i)
{
    for (int k = i - 1; k >= 0; --k) {
        QChar const c = s[k];
        if (c.isSpace())
            continue;
        return QStringLiteral(":-[{,").contains(c);
    }
    return true;
}

QString withoutComment(QString const& s)
{
    QChar quote;
    for (int i = 0; i < s.size(); ++i) {
        QChar const c = s[i];
        if (!quote.isNull()) {
            if (c == quote) {
                if (quote == QLatin1Char('\'') && i + 1 < s.size() && s[i + 1] == QLatin1Char('\''))
                    ++i;
                else
                    quote = QChar();
            } else if (c == QLatin1Char('\\') && quote == QLatin1Char('"')) {
                ++i;
            }
            continue;
        }
        if (isQuote(c) && opensQuote(s, i)) {
            quote = c;
            continue;
        }
        if (c == QLatin1Char('#') && (i == 0 || s[i - 1].isSpace()))
            return s.left(i).trimmed();
    }
    return s.trimmed();
}

// Brackets still open at the end of the text (outside quotes).
int openBrackets(QString const& s)
{
    int depth = 0;
    QChar quote;
    for (int i = 0; i < s.size(); ++i) {
        QChar const c = s[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else if (c == QLatin1Char('\\') && quote == QLatin1Char('"'))
                ++i;
            continue;
        }
        if (isQuote(c) && opensQuote(s, i))
            quote = c;
        else if (c == QLatin1Char('[') || c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char(']') || c == QLatin1Char('}'))
            --depth;
    }
    return depth;
}

bool startsSequence(QString const& t)
{
    return t == QLatin1String("-") || t.startsWith(QLatin1String("- "));
}

// "key: rest" outside quotes and brackets; false when the text is not a key and a value.
bool splitKey(QString const& t, QString* key, QString* rest)
{
    int depth = 0;
    QChar quote;
    for (int i = 0; i < t.size(); ++i) {
        QChar const c = t[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (isQuote(c) && opensQuote(t, i))
            quote = c;
        else if (c == QLatin1Char('[') || c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char(']') || c == QLatin1Char('}'))
            --depth;
        else if (c == QLatin1Char(':') && depth == 0 && (i + 1 == t.size() || t[i + 1].isSpace())) {
            QString k = t.left(i).trimmed();
            if (k.size() >= 2 && isQuote(k[0]) && k.back() == k[0])
                k = k.mid(1, k.size() - 2);
            *key = k;
            *rest = t.mid(i + 1).trimmed();
            return !k.isEmpty() && !k.startsWith(QLatin1Char('!')) && !k.startsWith(QLatin1Char('{')) && !k.startsWith(QLatin1Char('['));
        }
    }
    return false;
}

QString unquoted(QString const& s, int* pos)
{
    QChar const quote = s[*pos];
    QString out;
    int i = *pos + 1;
    for (; i < s.size(); ++i) {
        QChar const c = s[i];
        if (c == quote) {
            if (quote == QLatin1Char('\'') && i + 1 < s.size() && s[i + 1] == QLatin1Char('\'')) {
                out += c;
                ++i;
                continue;
            }
            ++i;
            break;
        }
        if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < s.size()) {
            QChar const n = s[++i];
            out += n == QLatin1Char('n') ? QChar(QLatin1Char('\n')) : n == QLatin1Char('t') ? QChar(QLatin1Char('\t')) : n;
            continue;
        }
        out += c;
    }
    *pos = i;
    return out;
}

QString tagOf(QString const& s, int* pos)
{
    int i = *pos + 1;
    QString tag;
    if (i < s.size() && s[i] == QLatin1Char('<')) {
        int const close = s.indexOf(QLatin1Char('>'), i);
        int const end = close < 0 ? s.size() : close;
        tag = s.mid(i + 1, end - i - 1);
        i = close < 0 ? s.size() : close + 1;
    } else {
        int const start = i;
        while (i < s.size() && !s[i].isSpace() && s[i] != QLatin1Char('{') && s[i] != QLatin1Char('['))
            ++i;
        tag = s.mid(start, i - start);
    }
    *pos = i;
    return tag;
}

class Parser {
public:
    explicit Parser(QString const& text)
    {
        QStringList const raw = text.split(QLatin1Char('\n'));
        for (int i = 0; i < raw.size(); ++i) {
            QString s = raw[i];
            s.remove(QLatin1Char('\r'));
            Line l;
            l.number = i + 1;
            while (l.indent < s.size() && (s[l.indent] == QLatin1Char(' ') || s[l.indent] == QLatin1Char('\t')))
                ++l.indent;
            l.text = s.mid(l.indent);
            QString const t = l.text.trimmed();
            l.blank = t.isEmpty() || t.startsWith(QLatin1Char('#')) || t == QLatin1String("---") || t == QLatin1String("...") || t.startsWith(QLatin1Char('%'));
            m_lines << l;
        }
    }

    bool document(Node* out)
    {
        int const first = next(0);
        if (first < 0)
            return true;
        if (!block(m_lines[first].indent, out))
            return false;
        int const left = next(m_at);
        if (left >= 0)
            return fail(m_lines[left].number, said("This line is not in its place (look at its indentation)."));
        return true;
    }

    QString error;

private:
    QVector<Line> m_lines;
    int m_at = 0;

    bool fail(int line, QString const& why)
    {
        error = said("Line %1: %2").arg(line).arg(why);
        return false;
    }

    int next(int from) const
    {
        for (int i = from; i < m_lines.size(); ++i)
            if (!m_lines[i].blank)
                return i;
        return -1;
    }

    bool block(int indent, Node* out)
    {
        int const i = next(m_at);
        if (i < 0 || m_lines[i].indent < indent)
            return true;
        QString const t = withoutComment(m_lines[i].text);
        if (startsSequence(t))
            return sequence(m_lines[i].indent, out);
        QString key, rest;
        if (splitKey(t, &key, &rest))
            return map(m_lines[i].indent, out);
        m_at = i + 1;
        return value(t, m_lines[i].indent - 1, m_lines[i].number, out);
    }

    bool map(int indent, Node* out)
    {
        out->kind = Node::Map;
        int i;
        while ((i = next(m_at)) >= 0) {
            Line const& l = m_lines[i];
            if (l.indent < indent)
                break;
            if (l.indent > indent)
                return fail(l.number, said("This line is indented more than the lines before it."));
            QString const t = withoutComment(l.text);
            if (startsSequence(t))
                break;
            QString key, rest;
            if (!splitKey(t, &key, &rest))
                return fail(l.number, said("A \"name: value\" was expected."));
            if (out->line == 0)
                out->line = l.number;
            m_at = i + 1;
            Node child;
            if (!value(rest, indent, l.number, &child))
                return false;
            out->entries.push_back({key, child});
        }
        return true;
    }

    bool sequence(int indent, Node* out)
    {
        out->kind = Node::Sequence;
        int i;
        while ((i = next(m_at)) >= 0) {
            Line& l = m_lines[i];
            if (l.indent < indent)
                break;
            if (l.indent > indent)
                return fail(l.number, said("This line is indented more than the lines before it."));
            QString const t = withoutComment(l.text);
            if (!startsSequence(t))
                break;
            if (out->line == 0)
                out->line = l.number;
            int skip = 1;
            while (skip < l.text.size() && l.text[skip] == QLatin1Char(' '))
                ++skip;
            QString const content = t.mid(1).trimmed();
            Node item;
            QString key, rest;
            if (!content.isEmpty() && !content.startsWith(QLatin1Char('!')) && splitKey(content, &key, &rest)) {
                // "- name: value": a map starting on the line of its dash.
                l.indent += skip;
                l.text = l.text.mid(skip);
                m_at = i;
                if (!map(l.indent, &item))
                    return false;
            } else {
                m_at = i + 1;
                if (!value(content, indent, l.number, &item))
                    return false;
            }
            out->items.push_back(item);
        }
        return true;
    }

    // What follows a key's colon or an item's dash.
    bool value(QString text, int indent, int lineNumber, Node* out)
    {
        out->line = lineNumber;
        text = text.trimmed();
        QString tag;
        if (text.startsWith(QLatin1Char('!'))) {
            int pos = 0;
            tag = tagOf(text, &pos);
            text = text.mid(pos).trimmed();
        }
        if (text.isEmpty()) {
            int const j = next(m_at);
            if (j >= 0) {
                QString const t = withoutComment(m_lines[j].text);
                if (m_lines[j].indent > indent)
                    if (!block(m_lines[j].indent, out))
                        return false;
                if (m_lines[j].indent == indent && startsSequence(t) && out->kind == Node::Null)
                    if (!sequence(indent, out))
                        return false;
            }
            out->tag = tag;
            return true;
        }
        if (text.startsWith(QLatin1Char('|')) || text.startsWith(QLatin1Char('>'))) {
            blockText(text[0] == QLatin1Char('>'), indent, out);
            out->tag = tag;
            return true;
        }
        if (text.startsWith(QLatin1Char('[')) || text.startsWith(QLatin1Char('{')) || isQuote(text[0])) {
            // (A flow list or map may go on over the lines after.)
            while (openBrackets(text) > 0) {
                int const j = next(m_at);
                if (j < 0)
                    return fail(lineNumber, said("A bracket is not closed."));
                text += QLatin1Char(' ') + withoutComment(m_lines[j].text);
                m_at = j + 1;
            }
            int pos = 0;
            if (!flow(text, &pos, out, lineNumber))
                return false;
            if (!tag.isEmpty())
                out->tag = tag;
            return true;
        }
        out->kind = Node::Scalar;
        out->text = text;
        out->tag = tag;
        return true;
    }

    void blockText(bool folded, int indent, Node* out)
    {
        out->kind = Node::Scalar;
        QStringList kept;
        int inner = -1;
        int i = m_at;
        for (; i < m_lines.size(); ++i) {
            Line const& l = m_lines[i];
            bool const empty = l.text.trimmed().isEmpty();
            if (!empty && l.indent <= indent)
                break;
            if (!empty && inner < 0)
                inner = l.indent;
            kept << (empty ? QString() : QString(l.indent - std::max(inner, 0), QLatin1Char(' ')) + l.text);
        }
        m_at = i;
        while (!kept.isEmpty() && kept.back().isEmpty())
            kept.removeLast();
        out->text = folded ? kept.join(QLatin1Char(' ')).simplified() : kept.join(QLatin1Char('\n'));
    }

    bool flow(QString const& s, int* pos, Node* out, int lineNumber, QString const& stops = {})
    {
        out->line = lineNumber;
        auto skip = [&] {
            while (*pos < s.size() && s[*pos].isSpace())
                ++*pos;
        };
        skip();
        if (*pos < s.size() && s[*pos] == QLatin1Char('!')) {
            out->tag = tagOf(s, pos);
            skip();
        }
        if (*pos >= s.size() || (stops.contains(s[*pos]))) {
            out->kind = Node::Null;
            return true;
        }
        QChar const c = s[*pos];
        if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
            bool const isMap = c == QLatin1Char('{');
            QChar const close = isMap ? QLatin1Char('}') : QLatin1Char(']');
            out->kind = isMap ? Node::Map : Node::Sequence;
            ++*pos;
            for (;;) {
                skip();
                if (*pos >= s.size())
                    return fail(lineNumber, said("A bracket is not closed."));
                if (s[*pos] == close) {
                    ++*pos;
                    return true;
                }
                if (isMap) {
                    QString key;
                    if (isQuote(s[*pos])) {
                        key = unquoted(s, pos);
                    } else {
                        int const start = *pos;
                        while (*pos < s.size() && !(s[*pos] == QLatin1Char(':') && (*pos + 1 >= s.size() || s[*pos + 1].isSpace() || s[*pos + 1] == QLatin1Char(',') || s[*pos + 1] == close))
                               && s[*pos] != QLatin1Char(',') && s[*pos] != close)
                            ++*pos;
                        key = s.mid(start, *pos - start).trimmed();
                    }
                    skip();
                    Node child;
                    if (*pos < s.size() && s[*pos] == QLatin1Char(':')) {
                        ++*pos;
                        if (!flow(s, pos, &child, lineNumber, QString(QLatin1Char(',')) + close))
                            return false;
                    }
                    out->entries.push_back({key, child});
                } else {
                    Node child;
                    if (!flow(s, pos, &child, lineNumber, QString(QLatin1Char(',')) + close))
                        return false;
                    out->items.push_back(child);
                }
                skip();
                if (*pos < s.size() && s[*pos] == QLatin1Char(','))
                    ++*pos;
                else if (*pos < s.size() && s[*pos] != close)
                    return fail(lineNumber, said("A comma or a closing bracket was expected."));
            }
        }
        out->kind = Node::Scalar;
        if (isQuote(c)) {
            out->text = unquoted(s, pos);
            return true;
        }
        int const start = *pos;
        while (*pos < s.size() && !stops.contains(s[*pos]))
            ++*pos;
        out->text = s.mid(start, *pos - start).trimmed();
        return true;
    }
};

}  // namespace

namespace Cms::Yaml {

Node const* Node::get(QString const& key) const
{
    for (auto const& e : entries)
        if (e.first == key)
            return &e.second;
    return nullptr;
}

QString Node::str(QString const& key, QString const& fallback) const
{
    Node const* const n = get(key);
    return n && n->kind == Scalar ? n->text : fallback;
}

double Node::num(QString const& key, double fallback) const
{
    Node const* const n = get(key);
    if (!n || n->kind != Scalar)
        return fallback;
    bool ok = false;
    double const v = n->text.toDouble(&ok);
    return ok ? v : fallback;
}

bool Node::flag(QString const& key, bool fallback) const
{
    Node const* const n = get(key);
    if (!n || n->kind != Scalar)
        return fallback;
    QString const t = n->text.toLower();
    if (t == QLatin1String("true") || t == QLatin1String("yes") || t == QLatin1String("on"))
        return true;
    if (t == QLatin1String("false") || t == QLatin1String("no") || t == QLatin1String("off"))
        return false;
    return fallback;
}

QStringList Node::list(QString const& key) const
{
    QStringList out;
    Node const* const n = get(key);
    if (!n)
        return out;
    if (n->kind == Scalar) {
        for (QString const& part : n->text.split(QLatin1Char(','), Qt::SkipEmptyParts))
            out << part.trimmed();
        return out;
    }
    for (Node const& item : n->items)
        if (item.kind == Scalar && !item.text.isEmpty())
            out << item.text;
    return out;
}

QVector<double> Node::numbers(QString const& key, int count, double fallback) const
{
    QVector<double> out;
    Node const* const n = get(key);
    if (n && n->kind == Scalar) {
        bool ok = false;
        double const v = n->text.toDouble(&ok);
        out.fill(ok ? v : fallback, count);
        return out;
    }
    if (n && n->kind == Sequence)
        for (Node const& item : n->items)
            out << item.text.toDouble();
    if (out.isEmpty())
        out.fill(fallback, count);
    return out;
}

bool parse(QString const& text, Node* out, QString* error)
{
    Parser p(text);
    *out = Node();
    if (!p.document(out)) {
        if (error)
            *error = p.error;
        return false;
    }
    return true;
}

}  // namespace Cms::Yaml
