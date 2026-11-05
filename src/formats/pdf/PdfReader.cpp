// Eclipse - the PDF reader.
#include "formats/pdf/PdfReader.h"

#include <QFile>
#include <QObject>
#include <QHash>
#include <QPainterPath>
#include <QRegularExpression>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace Pdf {

namespace {

struct Value {
    enum Kind { Null, Bool, Number, String, Name, Array, Dict, Ref, Op, End };
    Kind kind = Null;
    double number = 0.0;
    bool flag = false;
    QByteArray text;                                    // a string, a name, an operator
    std::shared_ptr<std::vector<Value>> items;          // an array
    std::shared_ptr<std::vector<std::pair<QByteArray, Value>>> dict;
    int ref = -1;
    QByteArray stream;                                  // a stream's bytes, as in the file

    bool isName(char const* n) const { return kind == Name && text == n; }
    Value const* get(QByteArray const& key) const
    {
        if (kind != Dict || !dict)
            return nullptr;
        for (auto const& kv : *dict)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }
    std::vector<Value> const& list() const
    {
        static std::vector<Value> const none;
        return items ? *items : none;
    }
};

bool white(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool delimiter(char c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%'; }

struct Lexer {
    QByteArray const& data;
    qsizetype pos = 0;
    explicit Lexer(QByteArray const& d, qsizetype at = 0) : data(d), pos(at) {}
    bool atEnd() const { return pos >= data.size(); }
    char peek(qsizetype ahead = 0) const { return pos + ahead < data.size() ? data[pos + ahead] : '\0'; }
    void skip()
    {
        while (!atEnd()) {
            char const c = data[pos];
            if (white(c)) {
                ++pos;
            } else if (c == '%') {
                while (!atEnd() && data[pos] != '\n' && data[pos] != '\r')
                    ++pos;
            } else {
                break;
            }
        }
    }
    QByteArray word()
    {
        qsizetype const start = pos;
        while (!atEnd() && !white(data[pos]) && !delimiter(data[pos]))
            ++pos;
        return data.mid(start, pos - start);
    }

    Value value()
    {
        skip();
        Value v;
        if (atEnd()) {
            v.kind = Value::End;
            return v;
        }
        char const c = data[pos];
        if (c == '<' && peek(1) == '<') {
            pos += 2;
            v.kind = Value::Dict;
            v.dict = std::make_shared<std::vector<std::pair<QByteArray, Value>>>();
            for (;;) {
                skip();
                if (atEnd())
                    break;
                if (data[pos] == '>' && peek(1) == '>') {
                    pos += 2;
                    break;
                }
                Value key = value();
                if (key.kind == Value::End)
                    break;
                if (key.kind != Value::Name)
                    continue;
                Value item = value();
                v.dict->emplace_back(key.text, item);
            }
            return v;
        }
        if (c == '[') {
            ++pos;
            v.kind = Value::Array;
            v.items = std::make_shared<std::vector<Value>>();
            for (;;) {
                skip();
                if (atEnd())
                    break;
                if (data[pos] == ']') {
                    ++pos;
                    break;
                }
                Value item = value();
                if (item.kind == Value::End)
                    break;
                v.items->push_back(item);
            }
            return v;
        }
        if (c == '(') {
            ++pos;
            v.kind = Value::String;
            int depth = 1;
            while (!atEnd()) {
                char ch = data[pos++];
                if (ch == '\\' && !atEnd()) {
                    char const e = data[pos++];
                    switch (e) {
                    case 'n': v.text += '\n'; break;
                    case 'r': v.text += '\r'; break;
                    case 't': v.text += '\t'; break;
                    case 'b': v.text += '\b'; break;
                    case 'f': v.text += '\f'; break;
                    case '\r': if (peek() == '\n') ++pos; break;
                    case '\n': break;
                    default:
                        if (e >= '0' && e <= '7') {
                            int code = e - '0';
                            for (int k = 0; k < 2 && peek() >= '0' && peek() <= '7'; ++k)
                                code = code * 8 + (data[pos++] - '0');
                            v.text += char(code);
                        } else {
                            v.text += e;
                        }
                    }
                    continue;
                }
                if (ch == '(')
                    ++depth;
                if (ch == ')' && --depth == 0)
                    break;
                v.text += ch;
            }
            return v;
        }
        if (c == '<') {
            ++pos;
            v.kind = Value::String;
            QByteArray hex;
            while (!atEnd() && data[pos] != '>') {
                if (!white(data[pos]))
                    hex += data[pos];
                ++pos;
            }
            ++pos;
            if (hex.size() % 2)
                hex += '0';
            v.text = QByteArray::fromHex(hex);
            return v;
        }
        if (c == '/') {
            ++pos;
            v.kind = Value::Name;
            QByteArray const raw = word();
            for (qsizetype i = 0; i < raw.size(); ++i) {
                if (raw[i] == '#' && i + 2 <= raw.size() - 1) {
                    v.text += char(raw.mid(i + 1, 2).toInt(nullptr, 16));
                    i += 2;
                } else {
                    v.text += raw[i];
                }
            }
            return v;
        }
        if (c == '>' || c == ']' || c == ')' || c == '}' || c == '{') {
            ++pos;
            v.kind = Value::Op;
            v.text = QByteArray(1, c);
            return v;
        }
        QByteArray const w = word();
        if (w.isEmpty()) {
            ++pos;
            return value();
        }
        bool number = false;
        double const n = w.toDouble(&number);
        if (number) {
            v.kind = Value::Number;
            v.number = n;
            // "n g R": a reference.
            qsizetype const keep = pos;
            skip();
            QByteArray const second = word();
            bool whole = false;
            second.toInt(&whole);
            if (whole && w.indexOf('.') < 0 && !w.startsWith('-')) {
                skip();
                if (peek() == 'R' && (pos + 1 >= data.size() || white(peek(1)) || delimiter(peek(1)))) {
                    ++pos;
                    v.kind = Value::Ref;
                    v.ref = int(n);
                    return v;
                }
            }
            pos = keep;
            return v;
        }
        if (w == "true" || w == "false") {
            v.kind = Value::Bool;
            v.flag = w == "true";
        } else if (w == "null") {
            v.kind = Value::Null;
        } else {
            v.kind = Value::Op;
            v.text = w;
        }
        return v;
    }
};

class Reader {
public:
    Reader(QByteArray const& data, Options const& options, Report& report) : m_data(data), m_options(options), m_report(report) {}

    bool read(QVector<Page>& pages, QString* error)
    {
        if (!m_data.startsWith("%PDF")) {
            if (error)
                *error = m_data.startsWith("%!PS") ? QObject::tr("This Illustrator file is from before version 9 (PostScript, not PDF): save it again as a newer version.")
                                                   : QObject::tr("The imported file does not have a supported image format.");
            return false;
        }
        readObjects();
        readObjectStreams();
        QVector<std::pair<Value, Value>> found;     // each page and its inherited resources
        collectPages(found);
        if (found.isEmpty()) {
            if (error)
                *error = QObject::tr("The file has no pages.");
            return false;
        }
        for (auto const& [page, resources] : found)
            pages.push_back(readPage(page, resources));
        return true;
    }

private:
    Value resolve(Value const& v, int depth = 0) const
    {
        if (v.kind != Value::Ref || depth > 32)
            return v;
        auto const it = m_objects.find(v.ref);
        return it == m_objects.end() ? Value() : resolve(it->second, depth + 1);
    }
    Value field(Value const& dict, char const* key) const
    {
        Value const* const v = resolve(dict).get(key);
        return v ? resolve(*v) : Value();
    }
    double numberOf(Value const& v, double fallback = 0.0) const
    {
        Value const r = resolve(v);
        return r.kind == Value::Number ? r.number : fallback;
    }

    void readObjects()
    {
        static QRegularExpression const header(QStringLiteral("(\\d+)\\s+(\\d+)\\s+obj\\b"));
        QString const latin = QString::fromLatin1(m_data);
        qsizetype at = 0;
        for (;;) {
            QRegularExpressionMatch const m = header.match(latin, at);
            if (!m.hasMatch())
                break;
            int const number = m.captured(1).toInt();
            Lexer lex(m_data, m.capturedEnd());
            Value v = lex.value();
            lex.skip();
            if (v.kind == Value::Dict && m_data.mid(lex.pos, 6) == "stream") {
                qsizetype start = lex.pos + 6;
                if (start < m_data.size() && m_data[start] == '\r')
                    ++start;
                if (start < m_data.size() && m_data[start] == '\n')
                    ++start;
                qsizetype end = -1;
                Value const* const length = v.get("Length");
                if (length && length->kind == Value::Number) {
                    qsizetype const guess = start + qsizetype(length->number);
                    Lexer after(m_data, guess);
                    after.skip();
                    if (m_data.mid(after.pos, 9) == "endstream")
                        end = guess;
                }
                qsizetype const close = m_data.indexOf("endstream", end >= 0 ? end : start);
                if (end < 0) {
                    end = close < 0 ? m_data.size() : close;
                    while (end > start && (m_data[end - 1] == '\n' || m_data[end - 1] == '\r'))
                        --end;
                }
                v.stream = m_data.mid(start, end - start);
                lex.pos = close < 0 ? m_data.size() : close + 9;
            }
            m_objects[number] = v;
            if (v.kind == Value::Dict && v.get("Root") && field(v, "Type").isName("XRef"))
                m_root = *v.get("Root");
            at = std::max<qsizetype>(lex.pos, m.capturedEnd());
        }
        // The trailer says where the catalog is.
        qsizetype const trailer = m_data.lastIndexOf("trailer");
        if (trailer >= 0) {
            Lexer lex(m_data, trailer + 7);
            Value const t = lex.value();
            if (Value const* root = t.get("Root"))
                m_root = *root;
        }
    }

    QByteArray decoded(Value const& stream)
    {
        Value const filter = field(stream, "Filter");
        QByteArray bytes = stream.stream;
        std::vector<Value> filters;
        if (filter.kind == Value::Name)
            filters.push_back(filter);
        else
            filters = filter.list();
        for (Value const& f : filters) {
            Value const name = resolve(f);
            if (name.isName("FlateDecode") || name.isName("Fl")) {
                QByteArray sized(4, '\0');
                qToBigEndian<quint32>(quint32(std::min<qsizetype>(bytes.size() * 8 + 1024, 64 * 1024 * 1024)), sized.data());
                QByteArray const out = qUncompress(sized + bytes);
                if (out.isEmpty() && !bytes.isEmpty()) {
                    ++m_report.streamsUnread;
                    return {};
                }
                bytes = out;
            } else {
                ++m_report.streamsUnread;
                return {};
            }
        }
        return bytes;
    }

    void readObjectStreams()
    {
        std::vector<int> streams;
        for (auto const& [number, v] : m_objects)
            if (v.kind == Value::Dict && field(v, "Type").isName("ObjStm"))
                streams.push_back(number);
        for (int number : streams) {
            Value const s = m_objects[number];
            QByteArray const bytes = decoded(s);
            int const count = int(numberOf(*s.get("N")));
            int const first = int(numberOf(s.get("First") ? *s.get("First") : Value()));
            Lexer lex(bytes);
            std::vector<std::pair<int, int>> where;
            for (int k = 0; k < count; ++k) {
                Value const n = lex.value(), o = lex.value();
                where.emplace_back(int(n.number), int(o.number));
            }
            for (auto const& [objectNumber, offset] : where) {
                if (m_objects.count(objectNumber))
                    continue;
                Lexer at(bytes, first + offset);
                m_objects[objectNumber] = at.value();
            }
        }
        // (A cross-reference stream inside an object stream can name the catalog too.)
        if (m_root.kind != Value::Ref)
            for (auto const& [number, v] : m_objects)
                if (field(v, "Type").isName("Catalog")) {
                    m_root.kind = Value::Ref;
                    m_root.ref = number;
                }
    }

    void collectPages(QVector<std::pair<Value, Value>>& found)
    {
        Value const catalog = resolve(m_root);
        Value const tree = field(catalog, "Pages");
        std::function<void(Value const&, Value, Value, int)> walk = [&](Value const& node, Value resources, Value box, int depth) {
            if (depth > 64)
                return;
            Value const n = resolve(node);
            if (Value const r = field(n, "Resources"); r.kind == Value::Dict)
                resources = r;
            if (Value const b = field(n, "MediaBox"); b.kind == Value::Array)
                box = b;
            if (field(n, "Type").isName("Page")) {
                Value page = n;
                page.dict = std::make_shared<std::vector<std::pair<QByteArray, Value>>>(*n.dict);
                page.dict->emplace_back(QByteArray("EclipseMediaBox"), box);
                found.push_back({page, resources});
                return;
            }
            for (Value const& kid : field(n, "Kids").list())
                walk(kid, resources, box, depth + 1);
        };
        if (tree.kind == Value::Dict)
            walk(tree, Value(), Value(), 0);
        if (found.isEmpty()) {
            // No page tree to be found: every page, in the order of their numbers.
            for (auto const& [number, v] : m_objects)
                if (field(v, "Type").isName("Page")) {
                    Value page = v;
                    page.dict = std::make_shared<std::vector<std::pair<QByteArray, Value>>>(*v.dict);
                    page.dict->emplace_back(QByteArray("EclipseMediaBox"), field(v, "MediaBox"));
                    found.push_back({page, field(v, "Resources")});
                }
        }
    }

    // ---- a page's content ----

    struct State {
        QTransform ctm;
        QColor fill = Qt::black, stroke = Qt::black;
        bool fillCmyk = false, strokeCmyk = false;
        double width = 1.0;
        double fillAlpha = 1.0, strokeAlpha = 1.0;
    };

    QColor colourOf(std::vector<Value> const& operands, bool* cmyk) const
    {
        std::vector<double> c;
        for (Value const& v : operands)
            if (v.kind == Value::Number)
                c.push_back(std::clamp(v.number, 0.0, 1.0));
        *cmyk = false;
        if (c.size() >= 4) {
            *cmyk = true;
            size_t const k = c.size() - 4;
            double const key = c[k + 3];
            return QColor::fromRgbF(float((1 - c[k]) * (1 - key)), float((1 - c[k + 1]) * (1 - key)), float((1 - c[k + 2]) * (1 - key)));
        }
        if (c.size() == 3)
            return QColor::fromRgbF(float(c[0]), float(c[1]), float(c[2]));
        if (c.size() >= 1)
            return QColor::fromRgbF(float(c[0]), float(c[0]), float(c[0]));
        return Qt::black;
    }

    Page readPage(Value const& page, Value const& resources)
    {
        Page out;
        Value const box = resolve(*page.get("EclipseMediaBox"));
        std::vector<Value> const b = box.list();
        QRectF media(0, 0, 612, 792);
        if (b.size() == 4)
            media = QRectF(QPointF(numberOf(b[0]), numberOf(b[1])), QPointF(numberOf(b[2]), numberOf(b[3]))).normalized();
        out.size = media.size();
        m_flip = QTransform(1, 0, 0, -1, -media.left(), media.bottom());
        m_page = &out;
        m_order = 0;
        QByteArray content;
        Value const contents = field(page, "Contents");
        if (contents.kind == Value::Array) {
            for (Value const& part : contents.list())
                content += decoded(resolve(part)) + '\n';
        } else if (contents.kind == Value::Dict) {
            content = decoded(contents);
        }
        State start;
        m_layerStack.clear();
        run(content, resources, start, 0);
        m_page = nullptr;
        return out;
    }

    Drawing& layerDrawing()
    {
        QString const name = m_layerStack.isEmpty() ? QString() : m_layerStack.last();
        for (ArtLayer& l : m_page->layers)
            if (l.name == name)
                return l.drawing;
        m_page->layers.push_back({name, Drawing()});
        return m_page->layers.last().drawing;
    }

    void paint(QPainterPath path, State const& s, bool fill, bool stroke, bool evenOdd)
    {
        if (path.isEmpty())
            return;
        Drawing& d = layerDrawing();
        if (fill) {
            if (s.fillCmyk && !m_options.cmyk) {
                ++m_report.cmykLeftOut;
            } else {
                path.setFillRule(evenOdd ? Qt::OddEvenFill : Qt::WindingFill);
                Fill f;
                f.color = s.fill;
                f.color.setAlphaF(float(std::clamp(s.fillAlpha, 0.0, 1.0)));
                for (QPolygonF poly : path.simplified().toFillPolygons()) {
                    if (poly.size() > 1 && poly.first() == poly.last())
                        poly.removeLast();
                    if (poly.size() >= 3)
                        f.outlines.push_back(poly);
                }
                if (!f.outlines.isEmpty()) {
                    f.order = m_order++;
                    f.giveContours(0.25);
                    d.fills.push_back(f);
                }
            }
        }
        if (stroke) {
            if (s.strokeCmyk && !m_options.cmyk) {
                ++m_report.cmykLeftOut;
                return;
            }
            double const scale = std::sqrt(std::abs(s.ctm.determinant()));
            for (QPolygonF poly : path.toSubpathPolygons()) {
                Stroke line;
                line.closed = poly.size() > 2 && poly.first() == poly.last();
                if (line.closed)
                    poly.removeLast();
                if (poly.size() < 2)
                    continue;
                line.color = s.stroke;
                line.color.setAlphaF(float(std::clamp(s.strokeAlpha, 0.0, 1.0)));
                line.width = float(std::max(0.25, s.width * scale));
                for (QPointF const& p : poly)
                    line.points.push_back({p, 1.0f});
                line.order = m_order++;
                d.strokes.push_back(line);
            }
        }
    }

    void run(QByteArray const& content, Value const& resources, State state, int depth)
    {
        if (depth > 16)
            return;
        Lexer lex(content);
        std::vector<Value> operands;
        QVector<State> saved;
        QPainterPath path;
        QPointF current;
        auto at = [&](double x, double y) { return m_flip.map(state.ctm.map(QPointF(x, y))); };
        auto num = [&](size_t i) { return i < operands.size() && operands[i].kind == Value::Number ? operands[i].number : 0.0; };
        QVector<bool> markedIsLayer;
        for (;;) {
            Value v = lex.value();
            if (v.kind == Value::End)
                break;
            if (v.kind != Value::Op) {
                operands.push_back(v);
                continue;
            }
            QByteArray const& op = v.text;
            if (op == "q") {
                saved.push_back(state);
            } else if (op == "Q") {
                if (!saved.isEmpty())
                    state = saved.takeLast();
            } else if (op == "cm" && operands.size() >= 6) {
                state.ctm = QTransform(num(0), num(1), num(2), num(3), num(4), num(5)) * state.ctm;
            } else if (op == "m" && operands.size() >= 2) {
                current = QPointF(num(0), num(1));
                path.moveTo(at(num(0), num(1)));
            } else if (op == "l" && operands.size() >= 2) {
                current = QPointF(num(0), num(1));
                path.lineTo(at(num(0), num(1)));
            } else if (op == "c" && operands.size() >= 6) {
                path.cubicTo(at(num(0), num(1)), at(num(2), num(3)), at(num(4), num(5)));
                current = QPointF(num(4), num(5));
            } else if (op == "v" && operands.size() >= 4) {
                path.cubicTo(at(current.x(), current.y()), at(num(0), num(1)), at(num(2), num(3)));
                current = QPointF(num(2), num(3));
            } else if (op == "y" && operands.size() >= 4) {
                path.cubicTo(at(num(0), num(1)), at(num(2), num(3)), at(num(2), num(3)));
                current = QPointF(num(2), num(3));
            } else if (op == "h") {
                path.closeSubpath();
            } else if (op == "re" && operands.size() >= 4) {
                double const x = num(0), y = num(1), w = num(2), h = num(3);
                path.moveTo(at(x, y));
                path.lineTo(at(x + w, y));
                path.lineTo(at(x + w, y + h));
                path.lineTo(at(x, y + h));
                path.closeSubpath();
                current = QPointF(x, y);
            } else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" || op == "b" || op == "b*" || op == "n") {
                if (op == "s" || op == "b" || op == "b*")
                    path.closeSubpath();
                bool const fill = op.startsWith('f') || op.startsWith('F') || op.startsWith('B') || op.startsWith('b');
                bool const stroke = op == "S" || op == "s" || op.startsWith('B') || op.startsWith('b');
                if (op != "n")
                    paint(path, state, fill, stroke, op.endsWith('*'));
                path = QPainterPath();
            } else if (op == "w" && !operands.empty()) {
                state.width = num(0);
            } else if (op == "g" || op == "rg" || op == "k" || op == "sc" || op == "scn") {
                if (!(op == "scn" && !operands.empty() && operands.back().kind == Value::Name))
                    state.fill = colourOf(operands, &state.fillCmyk);
            } else if (op == "G" || op == "RG" || op == "K" || op == "SC" || op == "SCN") {
                if (!(op == "SCN" && !operands.empty() && operands.back().kind == Value::Name))
                    state.stroke = colourOf(operands, &state.strokeCmyk);
            } else if (op == "cs" || op == "CS") {
                // A colour space: its colour starts black (or, for CMYK, no ink but black).
                bool const cmyk = !operands.empty() && operands[0].isName("DeviceCMYK");
                (op == "cs" ? state.fillCmyk : state.strokeCmyk) = cmyk;
                (op == "cs" ? state.fill : state.stroke) = Qt::black;
            } else if (op == "gs" && !operands.empty()) {
                Value const g = field(field(resources, "ExtGState"), operands[0].text.constData());
                if (Value const* ca = g.get("ca"))
                    state.fillAlpha = numberOf(*ca, 1.0);
                if (Value const* CA = g.get("CA"))
                    state.strokeAlpha = numberOf(*CA, 1.0);
            } else if (op == "BDC" || op == "BMC") {
                // Marked content: a layer of the file (optional content) when it says so.
                bool layer = false;
                if (op == "BDC" && operands.size() >= 2 && operands[0].isName("OC")) {
                    Value group = operands[1].kind == Value::Name ? field(field(resources, "Properties"), operands[1].text.constData()) : resolve(operands[1]);
                    Value const name = field(group, "Name");
                    m_layerStack.push_back(name.kind == Value::String ? QString::fromUtf8(name.text) : QString::fromLatin1(operands[1].text));
                    layer = true;
                }
                markedIsLayer.push_back(layer);
            } else if (op == "EMC") {
                if (!markedIsLayer.isEmpty() && markedIsLayer.takeLast() && !m_layerStack.isEmpty())
                    m_layerStack.removeLast();
            } else if (op == "BT") {
                ++m_report.texts;
            } else if (op == "BI") {
                // An inline picture: past its data.
                qsizetype const end = content.indexOf("EI", lex.pos);
                lex.pos = end < 0 ? content.size() : end + 2;
                ++m_report.images;
            } else if (op == "Do" && !operands.empty()) {
                Value const x = field(field(resources, "XObject"), operands[0].text.constData());
                if (field(x, "Subtype").isName("Form")) {
                    State inner = state;
                    std::vector<Value> const m = field(x, "Matrix").list();
                    if (m.size() == 6)
                        inner.ctm = QTransform(numberOf(m[0]), numberOf(m[1]), numberOf(m[2]), numberOf(m[3]), numberOf(m[4]), numberOf(m[5])) * inner.ctm;
                    Value const own = field(x, "Resources");
                    run(decoded(x), own.kind == Value::Dict ? own : resources, inner, depth + 1);
                } else if (field(x, "Subtype").isName("Image")) {
                    ++m_report.images;
                }
            }
            operands.clear();
        }
    }

    QByteArray const& m_data;
    Options m_options;
    Report& m_report;
    std::map<int, Value> m_objects;
    Value m_root;
    QTransform m_flip;
    Page* m_page = nullptr;
    int m_order = 0;
    QStringList m_layerStack;
};

}  // namespace

bool readData(QByteArray const& data, QVector<Page>& pages, QString* error, Options const& options, Report* report)
{
    Report kept;
    pages.clear();
    Reader reader(data, options, report ? *report : kept);
    return reader.read(pages, error);
}

bool read(QString const& file, QVector<Page>& pages, QString* error, Options const& options, Report* report)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Unable to find the given import file.");
        return false;
    }
    return readData(in.readAll(), pages, error, options, report);
}

}  // namespace Pdf
