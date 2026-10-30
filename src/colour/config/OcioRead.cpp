// Eclipse - reading an OpenColorIO configuration (.ocio): its colour spaces, displays, views, looks and rules.
#include "colour/config/Yaml.h"
#include "colour/luts/Luts.h"
#include "colour/spaces/ColourConfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <functional>

namespace {

using namespace Cms;
using Yaml::Node;

QString said(char const* text)
{
    return QCoreApplication::translate("Colour", text);
}

std::array<double, 3> three(Node const& n, QString const& key, double fallback)
{
    QVector<double> const v = n.numbers(key, 3, fallback);
    return {v.value(0, fallback), v.value(1, fallback), v.value(2, fallback)};
}

// "a:b" and lists: the folders searched for files (a drive letter's colon kept).
QStringList searchPathsOf(Node const* n)
{
    QStringList out;
    if (!n)
        return out;
    if (n->kind == Node::Sequence) {
        for (Node const& item : n->items)
            if (!item.text.isEmpty())
                out << item.text;
        return out;
    }
    QString part;
    QString const s = n->text;
    for (int i = 0; i < s.size(); ++i) {
        bool const drive = s[i] == QLatin1Char(':') && part.size() == 1 && part[0].isLetter() && i + 1 < s.size()
                           && (s[i + 1] == QLatin1Char('/') || s[i + 1] == QLatin1Char('\\'));
        if (s[i] == QLatin1Char(':') && !drive) {
            if (!part.isEmpty())
                out << part;
            part.clear();
        } else {
            part += s[i];
        }
    }
    if (!part.isEmpty())
        out << part;
    return out;
}

class Compiler {
public:
    explicit Compiler(Config& config) : c(config) {}

    Config& c;
    struct SpaceNodes {
        Node to, from;
        bool hasTo = false, hasFrom = false;
    };
    QVector<SpaceNodes> spaceNodes;
    QVector<int> state;         // 0 not yet, 1 being made, 2 made

    bool space(int i, QString* error)
    {
        if (state[i] == 2) {
            if (c.spaces[i].problem.isEmpty())
                return true;
            if (error)
                *error = c.spaces[i].problem;
            return false;
        }
        if (state[i] == 1) {
            if (error)
                *error = said("The colour space '%1' is defined through itself.").arg(c.spaces[i].name);
            return false;
        }
        state[i] = 1;
        Space& s = c.spaces[i];
        QString why;
        bool ok = true;
        if (spaceNodes[i].hasTo) {
            ok = chain(spaceNodes[i].to, &s.toReference, &why);
            s.hasTo = ok;
        }
        if (ok && spaceNodes[i].hasFrom) {
            ok = chain(spaceNodes[i].from, &s.fromReference, &why);
            s.hasFrom = ok;
        }
        Space& again = c.spaces[i];
        if (!ok) {
            again.problem = said("The colour space '%1' cannot be used: %2").arg(again.name, why);
            again.hasTo = again.hasFrom = false;
        }
        state[i] = 2;
        if (!ok && error)
            *error = again.problem;
        return ok;
    }

    bool needSpace(QString const& name, QString* error)
    {
        Space const* const s = c.space(name);
        if (!s) {
            if (error)
                *error = said("Cannot find the colour space named '%1'.").arg(name);
            return false;
        }
        return space(int(s - c.spaces.constData()), error);
    }

    bool needAll(QString* error)
    {
        for (int i = 0; i < c.spaces.size(); ++i)
            if (state[i] == 0)
                space(i, nullptr);
        Q_UNUSED(error);
        return true;
    }

    bool chain(Node const& n, Chain* out, QString* error)
    {
        out->clear();
        if (n.kind == Node::Sequence && n.tag.isEmpty()) {
            for (Node const& child : n.items) {
                Chain part;
                if (!chain(child, &part, error))
                    return false;
                *out += part;
            }
            return true;
        }
        Chain made;
        if (!one(n, &made, error))
            return false;
        if (n.str(QStringLiteral("direction")).compare(QLatin1String("inverse"), Qt::CaseInsensitive) == 0) {
            Chain back;
            if (!invert(made, &back, error))
                return false;
            made = back;
        }
        *out = made;
        return true;
    }

    bool one(Node const& n, Chain* out, QString* error)
    {
        QString const tag = n.tag;
        auto fail = [&](QString const& why) {
            if (error)
                *error = why;
            return false;
        };
        if (tag == QLatin1String("GroupTransform")) {
            Node const* const children = n.get(QStringLiteral("children"));
            if (!children)
                return true;
            for (Node const& child : children->items) {
                Chain part;
                if (!chain(child, &part, error))
                    return false;
                *out += part;
            }
            return true;
        }
        if (tag == QLatin1String("MatrixTransform")) {
            QVector<double> m = n.numbers(QStringLiteral("matrix"), 0, 0.0);
            if (m.size() != 16)
                m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            QVector<double> o = n.numbers(QStringLiteral("offset"), 4, 0.0);
            if (o.size() < 3)
                o = {0, 0, 0, 0};
            *out << matrixOp({m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]}, {o[0], o[1], o[2]});
            return true;
        }
        if (tag == QLatin1String("ExponentTransform")) {
            Op op;
            op.kind = Op::Exponent;
            op.a = three(n, QStringLiteral("value"), 1.0);
            QString const style = n.str(QStringLiteral("style")).toLower();
            op.style = style == QLatin1String("mirror") ? 1 : style == QLatin1String("pass_thru") ? 2 : 0;
            *out << op;
            return true;
        }
        if (tag == QLatin1String("ExponentWithLinearTransform")) {
            Op op;
            op.kind = Op::MonCurve;
            op.a = three(n, QStringLiteral("gamma"), 1.0);
            op.b = three(n, QStringLiteral("offset"), 0.0);
            for (size_t i = 0; i < 3; ++i)
                if (op.a[i] <= 1.0 || op.b[i] <= 0.0)
                    return fail(said("ExponentWithLinearTransform needs a gamma over 1 and an offset over 0."));
            op.style = n.str(QStringLiteral("style")).toLower() == QLatin1String("mirror") ? 1 : 0;
            *out << op;
            return true;
        }
        if (tag == QLatin1String("LogTransform")) {
            Op op;
            op.kind = Op::Log;
            op.base = n.num(QStringLiteral("base"), 2.0);
            *out << op;
            return true;
        }
        if (tag == QLatin1String("LogAffineTransform") || tag == QLatin1String("LogCameraTransform")) {
            Op op;
            op.kind = tag == QLatin1String("LogAffineTransform") ? Op::LogAffine : Op::LogCamera;
            op.base = n.num(QStringLiteral("base"), 2.0);
            op.a = three(n, QStringLiteral("log_side_slope"), 1.0);
            op.b = three(n, QStringLiteral("log_side_offset"), 0.0);
            op.c = three(n, QStringLiteral("lin_side_slope"), 1.0);
            op.d = three(n, QStringLiteral("lin_side_offset"), 0.0);
            if (op.kind == Op::LogCamera) {
                if (!n.has(QStringLiteral("lin_side_break")))
                    return fail(said("LogCameraTransform needs lin_side_break."));
                op.e = three(n, QStringLiteral("lin_side_break"), 0.0);
                op.f = three(n, QStringLiteral("linear_slope"), 0.0);
            }
            *out << op;
            return true;
        }
        if (tag == QLatin1String("CDLTransform")) {
            Op op;
            op.kind = Op::Cdl;
            op.a = three(n, QStringLiteral("slope"), 1.0);
            op.b = three(n, QStringLiteral("offset"), 0.0);
            op.c = three(n, QStringLiteral("power"), 1.0);
            op.e = {n.num(QStringLiteral("sat"), 1.0), 0, 0};
            op.style = n.str(QStringLiteral("style")).toLower() == QLatin1String("noclamp") ? 1 : 0;
            *out << op;
            return true;
        }
        if (tag == QLatin1String("RangeTransform")) {
            bool const hasMin = n.has(QStringLiteral("min_in_value")), hasMax = n.has(QStringLiteral("max_in_value"));
            if (!hasMin && !hasMax)
                return true;
            Op op = rangeOp(n.num(QStringLiteral("min_in_value"), 0.0), n.num(QStringLiteral("max_in_value"), 1.0), n.num(QStringLiteral("min_out_value"), 0.0),
                            n.num(QStringLiteral("max_out_value"), 1.0), n.str(QStringLiteral("style")).toLower() != QLatin1String("noclamp"));
            op.style = hasMin && hasMax ? 0 : hasMin ? 1 : 2;
            *out << op;
            return true;
        }
        if (tag == QLatin1String("FixedFunctionTransform")) {
            QString const style = n.str(QStringLiteral("style")).toUpper();
            Op op;
            op.kind = Op::Fixed;
            if (style == QLatin1String("RGB_TO_HSV"))
                op.style = int(FixedStyle::RgbToHsv);
            else if (style == QLatin1String("XYZ_TO_XYY"))
                op.style = int(FixedStyle::XyzToXyy);
            else if (style == QLatin1String("XYZ_TO_UVY"))
                op.style = int(FixedStyle::XyzToUvy);
            else if (style == QLatin1String("XYZ_TO_LUV"))
                op.style = int(FixedStyle::XyzToLuv);
            else if (style == QLatin1String("REC2100_SURROUND")) {
                op.style = int(FixedStyle::Rec2100Surround);
                op.a = {n.numbers(QStringLiteral("params"), 1, 1.0).value(0, 1.0), 0, 0};
            } else
                return fail(said("Eclipse cannot do the fixed function '%1'.").arg(n.str(QStringLiteral("style"))));
            *out << op;
            return true;
        }
        if (tag == QLatin1String("ExposureContrastTransform")) {
            Op op;
            op.kind = Op::Exposure;
            op.a = {n.num(QStringLiteral("exposure"), 0.0), n.num(QStringLiteral("contrast"), 1.0), n.num(QStringLiteral("gamma"), 1.0)};
            op.b = {n.num(QStringLiteral("pivot"), 0.18), 0, 0};
            QString const style = n.str(QStringLiteral("style")).toLower();
            op.style = style == QLatin1String("log") ? 2 : style == QLatin1String("video") ? 1 : 0;
            *out << op;
            return true;
        }
        if (tag == QLatin1String("AllocationTransform")) {
            Op op;
            op.kind = Op::Allocation;
            bool const lg2 = n.str(QStringLiteral("allocation")).toLower() == QLatin1String("lg2");
            QVector<double> const vars = n.numbers(QStringLiteral("vars"), 0, 0.0);
            op.style = lg2 ? 1 : 0;
            double const lo = vars.value(0, lg2 ? -10.0 : 0.0), hi = vars.value(1, lg2 ? 6.0 : 1.0);
            op.inLow = {lo, lo, lo};
            op.inHigh = {hi, hi, hi};
            op.a = {vars.value(2, 0.0), 0, 0};
            *out << op;
            return true;
        }
        if (tag == QLatin1String("BuiltinTransform"))
            return builtinTransform(n.str(QStringLiteral("style")), out, error);
        if (tag == QLatin1String("FileTransform")) {
            QString const src = n.str(QStringLiteral("src"));
            if (src.isEmpty())
                return fail(said("A FileTransform names no file."));
            QString const path = c.resolvePath(src);
            if (!Luts::read(path, out, error, n.str(QStringLiteral("cccid"))))
                return false;
            QString const how = n.str(QStringLiteral("interpolation")).toLower();
            for (Op& op : *out)
                if (op.kind == Op::Lut3D)
                    op.tetrahedral = how != QLatin1String("linear") && how != QLatin1String("nearest");
            return true;
        }
        if (tag == QLatin1String("ColorSpaceTransform")) {
            QString const src = n.str(QStringLiteral("src")), dst = n.str(QStringLiteral("dst"));
            if (src.isEmpty())
                return fail(said("ColorSpaceTransform: the source colour space is not named."));
            if (dst.isEmpty())
                return fail(said("ColorSpaceTransform: the destination colour space is not named."));
            return needSpace(src, error) && needSpace(dst, error) && c.convert(src, dst, out, error);
        }
        if (tag == QLatin1String("LookTransform")) {
            QString const src = n.str(QStringLiteral("src")), dst = n.str(QStringLiteral("dst"));
            if (src.isEmpty() || dst.isEmpty())
                return fail(said("LookTransform: a colour space is not named."));
            return needSpace(src, error) && needSpace(dst, error) && needAll(error) && c.withLooks(src, dst, n.str(QStringLiteral("looks")), out, error);
        }
        if (tag == QLatin1String("DisplayViewTransform")) {
            QString const src = n.str(QStringLiteral("src"));
            if (src.isEmpty())
                return fail(said("DisplayViewTransform: the source colour space is not named."));
            return needSpace(src, error) && needAll(error)
                   && c.toDisplay(src, n.str(QStringLiteral("display")), n.str(QStringLiteral("view")), out, error);
        }
        if (tag.startsWith(QLatin1String("Grading")))
            return fail(said("Eclipse cannot do %1 (a grading transform).").arg(tag));
        return fail(tag.isEmpty() ? said("A transform has no kind.") : said("Eclipse does not know the transform %1.").arg(tag));
    }
};

Space spaceOf(Node const& n, bool display, Compiler::SpaceNodes* nodes)
{
    Space s;
    s.name = n.str(QStringLiteral("name"));
    s.aliases = n.list(QStringLiteral("aliases"));
    s.family = n.str(QStringLiteral("family"));
    s.description = n.str(QStringLiteral("description")).trimmed();
    s.encoding = n.str(QStringLiteral("encoding"));
    s.bitDepth = n.str(QStringLiteral("bitdepth"));
    s.categories = n.list(QStringLiteral("categories"));
    s.isData = n.flag(QStringLiteral("isdata"), false);
    s.displayReferred = display;
    QString const to = display ? QStringLiteral("to_display_reference") : QStringLiteral("to_scene_reference");
    QString const from = display ? QStringLiteral("from_display_reference") : QStringLiteral("from_scene_reference");
    for (QString const& key : {to, QStringLiteral("to_reference")})
        if (Node const* t = n.get(key); t && !nodes->hasTo) {
            nodes->to = *t;
            nodes->hasTo = true;
        }
    for (QString const& key : {from, QStringLiteral("from_reference")})
        if (Node const* f = n.get(key); f && !nodes->hasFrom) {
            nodes->from = *f;
            nodes->hasFrom = true;
        }
    return s;
}

View viewOf(Node const& n)
{
    View v;
    v.name = n.str(QStringLiteral("name"));
    v.colourSpace = n.str(QStringLiteral("colorspace"));
    v.viewTransform = n.str(QStringLiteral("view_transform"));
    v.displayColourSpace = n.str(QStringLiteral("display_colorspace"));
    v.looks = n.str(QStringLiteral("looks"));
    v.description = n.str(QStringLiteral("description"));
    return v;
}

}  // namespace

namespace Cms {

bool readConfig(QString const& path, Config* out, QString* error)
{
    auto fail = [error](QString const& why) {
        if (error)
            *error = why;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(said("The file cannot be opened."));
    Node root;
    QString why;
    if (!Yaml::parse(QString::fromUtf8(f.readAll()), &root, &why))
        return fail(why);
    if (root.kind != Node::Map)
        return fail(said("This is not a colour configuration (no settings found)."));
    Config c;
    c.file = QFileInfo(path).absoluteFilePath();
    QString const versionText = root.str(QStringLiteral("ocio_profile_version"));
    if (versionText.isEmpty())
        return fail(said("This is not a colour configuration (it has no ocio_profile_version)."));
    c.version = versionText.toDouble();
    if (c.version < 1.0 || c.version >= 3.0)
        return fail(said("This configuration is version %1; Eclipse reads versions 1 and 2.").arg(versionText));
    c.title = root.str(QStringLiteral("name"));
    c.description = root.str(QStringLiteral("description")).trimmed();
    c.searchPaths = searchPathsOf(root.get(QStringLiteral("search_path")));
    if (Node const* l = root.get(QStringLiteral("luma")); l && l->items.size() == 3)
        c.luma = three(root, QStringLiteral("luma"), 0.0);
    if (Node const* env = root.get(QStringLiteral("environment")))
        for (auto const& e : env->entries)
            c.environment.insert(e.first, e.second.text);
    if (Node const* roles = root.get(QStringLiteral("roles")))
        for (auto const& e : roles->entries)
            c.roles.insert(e.first, e.second.text);
    c.activeDisplays = root.list(QStringLiteral("active_displays"));
    c.activeViews = root.list(QStringLiteral("active_views"));
    c.inactiveSpaces = root.list(QStringLiteral("inactive_colorspaces"));
    c.defaultViewTransform = root.str(QStringLiteral("default_view_transform"));
    if (Node const* rules = root.get(QStringLiteral("file_rules")))
        for (Node const& r : rules->items) {
            FileRule rule{r.str(QStringLiteral("name")), r.str(QStringLiteral("colorspace")), r.str(QStringLiteral("pattern")), r.str(QStringLiteral("extension")),
                          r.str(QStringLiteral("regex"))};
            if (rule.colourSpace.isEmpty() && rule.name != QLatin1String("ColorSpaceNamePathSearch"))
                return fail(said("File rules: a rule names no colour space."));
            c.fileRules << rule;
        }
    if (Node const* shared = root.get(QStringLiteral("shared_views")))
        for (Node const& v : shared->items)
            c.sharedViews << viewOf(v);
    if (Node const* displays = root.get(QStringLiteral("displays")))
        for (auto const& e : displays->entries) {
            Display d;
            d.name = e.first;
            for (Node const& v : e.second.items) {
                if (v.tag == QLatin1String("Views"))
                    for (Node const& name : v.items)
                        d.sharedViews << name.text;
                else
                    d.views << viewOf(v);
            }
            c.displays << d;
        }
    Compiler compiler(c);
    QSet<QString> names;
    for (QString const& listKey : {QStringLiteral("display_colorspaces"), QStringLiteral("colorspaces")}) {
        Node const* const list = root.get(listKey);
        if (!list)
            continue;
        for (Node const& n : list->items) {
            Compiler::SpaceNodes nodes;
            Space s = spaceOf(n, listKey == QLatin1String("display_colorspaces"), &nodes);
            if (s.name.isEmpty())
                return fail(said("Line %1: a colour space has no name.").arg(n.line));
            if (names.contains(s.name.toLower())) {
                c.warnings << said("The colour space '%1' is defined twice; the first is kept.").arg(s.name);
                continue;
            }
            names.insert(s.name.toLower());
            c.spaces << s;
            compiler.spaceNodes << nodes;
        }
    }
    if (c.spaces.isEmpty())
        return fail(said("The configuration has no colour spaces."));
    compiler.state.fill(0, c.spaces.size());
    QVector<QPair<Node, Node>> vtNodes;
    if (Node const* vts = root.get(QStringLiteral("view_transforms")))
        for (Node const& n : vts->items) {
            ViewTransform vt;
            vt.name = n.str(QStringLiteral("name"));
            vt.description = n.str(QStringLiteral("description")).trimmed();
            vt.family = n.str(QStringLiteral("family"));
            Node from, to;
            if (Node const* f2 = n.get(QStringLiteral("from_scene_reference")) ? n.get(QStringLiteral("from_scene_reference")) : n.get(QStringLiteral("from_reference"))) {
                from = *f2;
                vt.hasFrom = true;
            }
            if (Node const* t2 = n.get(QStringLiteral("to_scene_reference")) ? n.get(QStringLiteral("to_scene_reference")) : n.get(QStringLiteral("to_reference"))) {
                to = *t2;
                vt.hasTo = true;
            }
            c.viewTransforms << vt;
            vtNodes << qMakePair(from, to);
        }
    QVector<QPair<Node, Node>> lookNodes;
    if (Node const* looks = root.get(QStringLiteral("looks")))
        for (Node const& n : looks->items) {
            Look l;
            l.name = n.str(QStringLiteral("name"));
            l.processSpace = n.str(QStringLiteral("process_space"));
            l.description = n.str(QStringLiteral("description")).trimmed();
            Node t, inv;
            if (Node const* t2 = n.get(QStringLiteral("transform"))) {
                t = *t2;
                l.hasTransform = true;
            }
            if (Node const* i2 = n.get(QStringLiteral("inverse_transform"))) {
                inv = *i2;
                l.hasInverse = true;
            }
            c.looks << l;
            lookNodes << qMakePair(t, inv);
        }
    // The spaces first (they may lean on one another), then what leans on them.
    for (int i = 0; i < c.spaces.size(); ++i)
        compiler.space(i, nullptr);
    for (int i = 0; i < c.viewTransforms.size(); ++i) {
        ViewTransform& vt = c.viewTransforms[i];
        QString why2;
        bool ok = (!vt.hasFrom || compiler.chain(vtNodes[i].first, &vt.fromSceneReference, &why2))
                  && (!vt.hasTo || compiler.chain(vtNodes[i].second, &vt.toSceneReference, &why2));
        if (!ok)
            c.viewTransforms[i].problem = said("The view transform '%1' cannot be used: %2").arg(c.viewTransforms[i].name, why2);
    }
    for (int i = 0; i < c.looks.size(); ++i) {
        Look& l = c.looks[i];
        QString why2;
        bool ok = (!l.hasTransform || compiler.chain(lookNodes[i].first, &l.transform, &why2))
                  && (!l.hasInverse || compiler.chain(lookNodes[i].second, &l.inverse, &why2));
        if (!ok)
            c.looks[i].problem = said("The look '%1' cannot be used: %2").arg(c.looks[i].name, why2);
    }
    if (Node const* nts = root.get(QStringLiteral("named_transforms")))
        for (Node const& n : nts->items) {
            NamedTransform nt;
            nt.name = n.str(QStringLiteral("name"));
            nt.aliases = n.list(QStringLiteral("aliases"));
            nt.description = n.str(QStringLiteral("description")).trimmed();
            nt.family = n.str(QStringLiteral("family"));
            QString why2;
            if (Node const* t = n.get(QStringLiteral("transform")))
                nt.hasForward = compiler.chain(*t, &nt.forward, &why2);
            if (Node const* t = n.get(QStringLiteral("inverse_transform")))
                nt.hasInverse = compiler.chain(*t, &nt.inverse, &why2);
            if (!why2.isEmpty())
                nt.problem = why2;
            c.namedTransforms << nt;
        }
    for (Space const& s : c.spaces)
        if (!s.problem.isEmpty())
            c.warnings << s.problem;
    for (ViewTransform const& vt : c.viewTransforms)
        if (!vt.problem.isEmpty())
            c.warnings << vt.problem;
    for (Display const& d : c.displays)
        for (View const& v : d.views)
            if (v.colourSpace.isEmpty() && v.viewTransform.isEmpty())
                c.warnings << said("The view '%1' of the display '%2' names no colour space.").arg(v.name, d.name);
    *out = std::move(c);
    return true;
}

}  // namespace Cms
