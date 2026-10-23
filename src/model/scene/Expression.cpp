// Eclipse - expression columns: reading an expression and working out its value on a frame.
#include "model/scene/FunctionColumn.h"

#include <QCoreApplication>

#include <cmath>
#include <numbers>

namespace {

constexpr int kDeepest = 64;    // how deep an expression may nest (and columns name one another)
constexpr double kDegree = std::numbers::pi / 180.0;

QString tr(char const* text)
{
    return QCoreApplication::translate("Expression", text);
}

// A value met while reading: a number, or a name (a column's) still to be looked up.
struct Value {
    double number = 0.0;
    QString name;
    bool isName = false;
};

class Reader {
public:
    Reader(QString const& text, double frame, int frameCount, std::function<bool(QString const&, double, double&)> const& lookup)
        : m_text(text), m_frame(frame), m_frameCount(frameCount), m_lookup(lookup)
    {
    }

    bool read(double& out)
    {
        Value v;
        if (!orExpr(v) || !number(v))
            return false;
        skip();
        if (m_at < m_text.size())
            return fail(tr("Expected expression while reading"));
        out = v.number;
        return true;
    }
    QString error;

private:
    bool fail(QString const& why)
    {
        if (error.isEmpty())
            error = why;
        return false;
    }
    void skip()
    {
        while (m_at < m_text.size() && m_text[m_at].isSpace())
            ++m_at;
    }
    bool peek(QString const& token)
    {
        skip();
        return m_text.mid(m_at, token.size()) == token;
    }
    bool take(QString const& token)
    {
        if (!peek(token))
            return false;
        m_at += int(token.size());
        return true;
    }
    // A name met where a number is wanted: the column of that name, on this frame.
    bool number(Value& v)
    {
        if (!v.isName)
            return true;
        double found = 0.0;
        if (!m_lookup || !m_lookup(v.name, m_frame, found))
            return fail(tr("Expression is not a number: %1").arg(v.name));
        v = Value{found, QString(), false};
        return true;
    }
    bool orExpr(Value& v)
    {
        if (!andExpr(v))
            return false;
        while (take(QStringLiteral("||"))) {
            Value r;
            if (!number(v) || !andExpr(r) || !number(r))
                return false;
            v.number = (v.number != 0.0 || r.number != 0.0) ? 1.0 : 0.0;
        }
        return true;
    }
    bool andExpr(Value& v)
    {
        if (!compare(v))
            return false;
        while (take(QStringLiteral("&&"))) {
            Value r;
            if (!number(v) || !compare(r) || !number(r))
                return false;
            v.number = (v.number != 0.0 && r.number != 0.0) ? 1.0 : 0.0;
        }
        return true;
    }
    bool compare(Value& v)
    {
        if (!sum(v))
            return false;
        for (QString const op : {QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("=="), QStringLiteral("!="), QStringLiteral("<"), QStringLiteral(">")}) {
            if (!take(op))
                continue;
            Value r;
            if (!number(v) || !sum(r) || !number(r))
                return false;
            double const a = v.number, b = r.number;
            bool const yes = op == QLatin1String("<=") ? a <= b : op == QLatin1String(">=") ? a >= b : op == QLatin1String("==") ? a == b
                             : op == QLatin1String("!=") ? a != b : op == QLatin1String("<") ? a < b : a > b;
            v.number = yes ? 1.0 : 0.0;
            return true;
        }
        return true;
    }
    bool sum(Value& v)
    {
        if (!product(v))
            return false;
        for (;;) {
            bool const plus = peek(QStringLiteral("+")), minus = !plus && peek(QStringLiteral("-"));
            if (!plus && !minus)
                return true;
            ++m_at;
            Value r;
            if (!number(v) || !product(r) || !number(r))
                return false;
            v.number = plus ? v.number + r.number : v.number - r.number;
        }
    }
    bool product(Value& v)
    {
        if (!unary(v))
            return false;
        for (;;) {
            QChar op;
            for (QChar c : {QLatin1Char('*'), QLatin1Char('/'), QLatin1Char('%')})
                if (op.isNull() && peek(QString(c)))
                    op = c;
            if (op.isNull())
                return true;
            ++m_at;
            Value r;
            if (!number(v) || !unary(r) || !number(r))
                return false;
            v.number = op == QLatin1Char('*') ? v.number * r.number : op == QLatin1Char('/') ? v.number / r.number : std::fmod(v.number, r.number);
        }
    }
    bool unary(Value& v)
    {
        if (++m_depth > kDeepest)
            return fail(tr("Expression too long to compute"));
        bool ok = true;
        if (take(QStringLiteral("-"))) {
            ok = unary(v) && number(v);
            v.number = -v.number;
        } else if (take(QStringLiteral("+"))) {
            ok = unary(v) && number(v);
        } else if (peek(QStringLiteral("!")) && !peek(QStringLiteral("!="))) {
            ++m_at;
            ok = unary(v) && number(v);
            v.number = v.number == 0.0 ? 1.0 : 0.0;
        } else {
            ok = power(v);
        }
        --m_depth;
        return ok;
    }
    bool power(Value& v)
    {
        if (!primary(v))
            return false;
        if (!take(QStringLiteral("^")))
            return true;
        Value r;
        if (!number(v) || !unary(r) || !number(r))
            return false;
        v.number = std::pow(v.number, r.number);
        return true;
    }
    bool primary(Value& v)
    {
        skip();
        if (m_at >= m_text.size())
            return fail(tr("Expected expression while reading"));
        QChar const c = m_text[m_at];
        if (take(QStringLiteral("("))) {
            if (!orExpr(v))
                return false;
            return take(QStringLiteral(")")) || fail(tr("Expected expression while reading"));
        }
        if (c.isDigit() || c == QLatin1Char('.')) {
            int const from = m_at;
            while (m_at < m_text.size() && (m_text[m_at].isDigit() || m_text[m_at] == QLatin1Char('.')))
                ++m_at;
            if (m_at < m_text.size() && (m_text[m_at] == QLatin1Char('e') || m_text[m_at] == QLatin1Char('E'))) {
                int const back = m_at;
                ++m_at;
                if (m_at < m_text.size() && (m_text[m_at] == QLatin1Char('+') || m_text[m_at] == QLatin1Char('-')))
                    ++m_at;
                if (m_at < m_text.size() && m_text[m_at].isDigit())
                    while (m_at < m_text.size() && m_text[m_at].isDigit())
                        ++m_at;
                else
                    m_at = back;
            }
            bool ok = false;
            v = Value{m_text.mid(from, m_at - from).toDouble(&ok), QString(), false};
            return ok || fail(tr("Expression is not a number: %1").arg(m_text.mid(from, m_at - from)));
        }
        if (c == QLatin1Char('"')) {
            // A column named in quotes (names with spaces).
            int const close = int(m_text.indexOf(QLatin1Char('"'), m_at + 1));
            if (close < 0)
                return fail(tr("Expected expression while reading"));
            v = Value{0.0, m_text.mid(m_at + 1, close - m_at - 1), true};
            m_at = close + 1;
            return true;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int const from = m_at;
            while (m_at < m_text.size() && (m_text[m_at].isLetterOrNumber() || m_text[m_at] == QLatin1Char('_')))
                ++m_at;
            QString const name = m_text.mid(from, m_at - from);
            if (take(QStringLiteral("(")))
                return call(name, v);
            QString const lower = name.toLower();
            if (lower == QLatin1String("currentframe") || lower == QLatin1String("frame"))
                v = Value{m_frame + 1.0, QString(), false};    // (frames are counted from 1, as they are shown)
            else if (lower == QLatin1String("numframes"))
                v = Value{double(m_frameCount), QString(), false};
            else if (lower == QLatin1String("pi"))
                v = Value{std::numbers::pi, QString(), false};
            else
                v = Value{0.0, name, true};
            return true;
        }
        return fail(tr("Expected expression while reading"));
    }
    bool call(QString const& name, Value& v)
    {
        QVector<Value> args;
        if (!take(QStringLiteral(")"))) {
            for (;;) {
                Value a;
                if (!orExpr(a))
                    return false;
                args.push_back(a);
                if (take(QStringLiteral(")")))
                    break;
                if (!take(QStringLiteral(",")))
                    return fail(tr("Expected expression while reading"));
            }
        }
        QString const f = name.toLower();
        // column("name") names a column; value(column, frame) is its value on another frame.
        if (f == QLatin1String("column")) {
            if (args.size() != 1 || !args[0].isName)
                return fail(tr("Expected expression while reading"));
            v = args[0];
            return true;
        }
        if (f == QLatin1String("value")) {
            if (args.isEmpty() || !args[0].isName)
                return fail(tr("Expected expression while reading"));
            double at = m_frame;
            if (args.size() > 1) {
                if (!number(args[1]))
                    return false;
                at = args[1].number - 1.0;
            }
            double found = 0.0;
            if (!m_lookup || !m_lookup(args[0].name, at, found))
                return fail(tr("Expression is not a number: %1").arg(args[0].name));
            v = Value{found, QString(), false};
            return true;
        }
        for (Value& a : args)
            if (!number(a))
                return false;
        auto need = [&](int n) { return args.size() == n || fail(tr("Expected expression while reading")); };
        double const a = args.value(0).number, b = args.value(1).number;
        double r = 0.0;
        if (f == QLatin1String("sin") && need(1)) r = std::sin(a * kDegree);
        else if (f == QLatin1String("cos") && need(1)) r = std::cos(a * kDegree);
        else if (f == QLatin1String("tan") && need(1)) r = std::tan(a * kDegree);
        else if (f == QLatin1String("asin") && need(1)) r = std::asin(a) / kDegree;
        else if (f == QLatin1String("acos") && need(1)) r = std::acos(a) / kDegree;
        else if (f == QLatin1String("atan") && need(1)) r = std::atan(a) / kDegree;
        else if (f == QLatin1String("atan2") && need(2)) r = std::atan2(a, b) / kDegree;
        else if (f == QLatin1String("sqrt") && need(1)) r = std::sqrt(a);
        else if (f == QLatin1String("abs") && need(1)) r = std::abs(a);
        else if (f == QLatin1String("exp") && need(1)) r = std::exp(a);
        else if (f == QLatin1String("log") && need(1)) r = std::log(a);
        else if (f == QLatin1String("log10") && need(1)) r = std::log10(a);
        else if (f == QLatin1String("floor") && need(1)) r = std::floor(a);
        else if (f == QLatin1String("ceil") && need(1)) r = std::ceil(a);
        else if ((f == QLatin1String("round") || f == QLatin1String("int")) && need(1)) r = f == QLatin1String("int") ? std::trunc(a) : std::round(a);
        else if (f == QLatin1String("sign") && need(1)) r = a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0;
        else if (f == QLatin1String("pow") && need(2)) r = std::pow(a, b);
        else if (f == QLatin1String("min") && !args.isEmpty()) {
            r = a;
            for (Value const& x : args)
                r = std::min(r, x.number);
        } else if (f == QLatin1String("max") && !args.isEmpty()) {
            r = a;
            for (Value const& x : args)
                r = std::max(r, x.number);
        } else if (f == QLatin1String("clamp") && need(3)) r = std::clamp(a, std::min(b, args[2].number), std::max(b, args[2].number));
        else if (f == QLatin1String("if") && need(3)) r = a != 0.0 ? b : args[2].number;
        else if (!error.isEmpty()) return false;
        else return fail(tr("Expression is not a number: %1").arg(name));
        if (!error.isEmpty())
            return false;
        v = Value{r, QString(), false};
        return true;
    }

    QString const& m_text;
    int m_at = 0;
    int m_depth = 0;
    double m_frame;
    int m_frameCount;
    std::function<bool(QString const&, double, double&)> const& m_lookup;
};

}  // namespace

bool ExpressionValue::evaluate(QString const& text, double frame, int frameCount, std::function<bool(QString const&, double, double&)> const& lookup,
                               double& out, QString* error)
{
    // (Columns that name one another round in a ring go no deeper than this.)
    static thread_local int depth = 0;
    if (depth >= 16) {
        if (error)
            *error = tr("Expression too long to compute");
        return false;
    }
    ++depth;
    Reader reader(text, frame, frameCount, lookup);
    double v = 0.0;
    bool ok = reader.read(v);
    --depth;
    if (ok && !std::isfinite(v)) {
        reader.error = tr("Result of expression is not a number: %1").arg(text.trimmed());
        ok = false;
    }
    if (error)
        *error = ok ? QString() : reader.error;
    if (ok)
        out = v;
    return ok;
}
