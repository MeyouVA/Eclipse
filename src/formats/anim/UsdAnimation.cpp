// Eclipse - layers' keyframes in USD files (see UsdAnimation.h).
#include "formats/anim/UsdAnimation.h"

#include "model/scene/Scene.h"
#include "platform/ThirdParty.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace UsdAnimation {

namespace {

QString tr(char const* text, char const* note = nullptr, int n = -1)
{
    return QCoreApplication::translate("UsdAnimation", text, note, n);
}

QString nodeName(Layer const& layer)
{
    QString name = layer.name.simplified();
    // (A prim's name is letters, digits and _, not starting with a digit.)
    for (QChar& c : name)
        if (!c.isLetterOrNumber() && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    if (name.isEmpty() || name[0].isDigit())
        name.prepend(QLatin1Char('_'));
    return name;
}

bool animatable(Layer const& layer)
{
    return layer.kind == Layer::DrawingLayer || layer.kind == Layer::Peg || layer.kind == Layer::Camera || layer.kind == Layer::Bone
           || layer.kind == Layer::Quadmap;
}

QString number(double v)
{
    return QString::number(v + 0.0, 'g', 10);
}

// ---- the computer's Python with the USD package ----

// A Python to run: the program, and what goes before the code (the launcher's choice of version).
struct Interpreter {
    QString program;
    QStringList prefix;
};

int run(Interpreter const& python, QString const& code, QStringList const& arguments, QString* err, int wait = 120000)
{
    QTemporaryDir quiet;    // (run where nothing of its own lies about to be imported by mistake)
    QProcess process;
    process.setWorkingDirectory(quiet.path());
    process.start(python.program, python.prefix + QStringList{QStringLiteral("-c"), code} + arguments);
    if (!process.waitForStarted(10000))
        return -1;
    if (!process.waitForFinished(wait)) {
        process.kill();
        process.waitForFinished(2000);
        return 1;
    }
    if (err)
        *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

// The Pythons there are: the one Eclipse may come with, those on the path, and each version the launcher knows.
QVector<Interpreter> pythons()
{
    QVector<Interpreter> found;
    if (QString const bundled = ThirdParty::executable(QStringLiteral("python"), QStringLiteral("python")); !bundled.isEmpty())
        found.push_back({bundled, {}});
    for (char const* name : {"python", "python3"}) {
        QString const program = QStandardPaths::findExecutable(QLatin1String(name));
        // (Windows' own stand-in that only offers to install Python from the Store is no Python.)
        if (!program.isEmpty() && !program.contains(QLatin1String("WindowsApps"), Qt::CaseInsensitive))
            found.push_back({program, {}});
    }
    if (QString const launcher = QStandardPaths::findExecutable(QStringLiteral("py")); !launcher.isEmpty()) {
        found.push_back({launcher, {}});
        QProcess list;
        list.start(launcher, {QStringLiteral("--list")});
        if (list.waitForFinished(10000)) {
            static QRegularExpression const kVersion(QStringLiteral(R"(-V:(\S+))"));
            for (auto it = kVersion.globalMatch(QString::fromUtf8(list.readAllStandardOutput())); it.hasNext();)
                found.push_back({launcher, {QStringLiteral("-V:") + it.next().captured(1)}});
        }
    }
    return found;
}

// The first Python that has the USD package (looked for once); its program is empty when none has.
Interpreter const& usdPython()
{
    static Interpreter const chosen = [] {
        for (Interpreter const& python : pythons())
            if (run(python, QStringLiteral("import pxr.Usd"), {}, nullptr, 30000) == 0)
                return python;
        return Interpreter();
    }();
    return chosen;
}

// Runs a few lines of Python with the USD package; its exit code (-1: no such Python), what it printed to `err`.
int runPython(QString const& code, QStringList const& arguments, QString* err)
{
    Interpreter const& python = usdPython();
    if (python.program.isEmpty())
        return -1;
    return run(python, code, arguments, err);
}

QString const kNoPackage = QStringLiteral("NO_USD_PACKAGE");

QString needsPackage()
{
    return tr("USD files other than .usda need the USD package for Python (install it with: pip install usd-core).");
}

// ---- writing ----

struct Sample {
    int frame;
    Pose pose;
};

}  // namespace

bool isUsd(QString const& path)
{
    QString const suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("usd") || suffix == QLatin1String("usda") || suffix == QLatin1String("usdc") || suffix == QLatin1String("usdz");
}

bool hasUsdPython()
{
    return runPython(QStringLiteral("import pxr.Usd"), {}, nullptr) == 0;
}

QString writeText(Scene const& scene, AtomAnimation::Options const& options, AtomAnimation::Result* result)
{
    double const field = options.fields ? scene.width / 12.0 : 1.0;
    double const scale = options.scale > 0.0 ? options.scale : 1.0;
    int first = scene.frameCount(), last = 0;
    QString body;
    QTextStream out(&body);
    QSet<QString> used;
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        if ((options.selectedOnly && !options.layers.contains(l)) || layer.keys.isEmpty() || !animatable(layer))
            continue;
        QString name = nodeName(layer);
        for (int n = 2; used.contains(name); ++n)
            name = QStringLiteral("%1_%2").arg(nodeName(layer)).arg(n);
        used.insert(name);
        // Each op sampled where a keyframe sets one of its parts, all three of its values as the layer has them there.
        auto samples = [&](int parts) {
            QVector<Sample> at;
            for (Keyframe const& k : layer.keys)
                if (k.parts & parts)
                    at.push_back({k.frame, layer.poseAt(k.frame)});
            return at;
        };
        QVector<Sample> const moves = samples(Keyframe::PartX | Keyframe::PartY | Keyframe::PartZ);
        QVector<Sample> const turns = samples(Keyframe::PartAngle | Keyframe::PartRest);
        QVector<Sample> const sizes = samples(Keyframe::PartScaleX | Keyframe::PartScaleY);
        auto write = [&](char const* type, char const* op, QVector<Sample> const& at, auto value) {
            out << "        " << type << " xformOp:" << op << ".timeSamples = {\n";
            for (Sample const& s : at) {
                out << "            " << (s.frame + 1) << ": " << value(s.pose) << ",\n";
                first = std::min(first, s.frame);
                last = std::max(last, s.frame);
                if (result)
                    ++result->keys;
            }
            out << "        }\n";
        };
        out << "    def Xform \"" << name << "\"\n    {\n";
        QStringList order;
        if (!moves.isEmpty()) {
            write("double3", "translate", moves, [&](Pose const& p) {
                return QStringLiteral("(%1, %2, %3)").arg(number(p.x / field / scale), number(-p.y / field / scale), number(p.z / field / scale));
            });
            order << QStringLiteral("\"xformOp:translate\"");
        }
        if (!turns.isEmpty()) {
            write("float3", "rotateXYZ", turns, [](Pose const& p) { return QStringLiteral("(%1, %2, %3)").arg(number(p.rotX), number(p.rotY), number(-p.rotation)); });
            order << QStringLiteral("\"xformOp:rotateXYZ\"");
        }
        if (!sizes.isEmpty()) {
            write("float3", "scale", sizes, [](Pose const& p) { return QStringLiteral("(%1, %2, 1)").arg(number(p.scaleX), number(p.scaleY)); });
            order << QStringLiteral("\"xformOp:scale\"");
        }
        out << "        uniform token[] xformOpOrder = [" << order.join(QStringLiteral(", ")) << "]\n    }\n";
        if (result)
            ++result->layers;
    }
    out.flush();
    QString text;
    QTextStream head(&text);
    head << "#usda 1.0\n(\n    defaultPrim = \"Scene\"\n    doc = \"Written by Eclipse\"\n"
         << "    startTimeCode = " << (first <= last ? first + 1 : 1) << "\n    endTimeCode = " << (first <= last ? last + 1 : 1) << "\n"
         << "    timeCodesPerSecond = " << number(scene.fps) << "\n    framesPerSecond = " << number(scene.fps) << "\n"
         << "    upAxis = \"Y\"\n)\n\ndef Xform \"Scene\"\n{\n";
    head.flush();
    return text + body + QStringLiteral("}\n");
}

AtomAnimation::Result write(Scene const& scene, QString const& path, AtomAnimation::Options const& options)
{
    AtomAnimation::Result result;
    if (path.trimmed().isEmpty()) {
        result.error = tr("No file has been selected for export.");
        return result;
    }
    if (options.selectedOnly && options.layers.isEmpty()) {
        result.error = tr("Could not get selected layers in scene.");
        return result;
    }
    if (QFileInfo::exists(path) && !options.overwrite) {
        result.error = tr("File '%1' already exists. Overwrite was not enabled!").arg(QFileInfo(path).fileName());
        return result;
    }
    QString const text = UsdAnimation::writeText(scene, options, &result);
    QString const suffix = QFileInfo(path).suffix().toLower();
    bool const text_ = suffix == QLatin1String("usda");
    QTemporaryDir work;
    QString const plain = text_ ? path : QDir(work.path()).filePath(QStringLiteral("animation.usda"));
    QSaveFile file(plain);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        result = AtomAnimation::Result();
        result.error = tr("Could not write %1.").arg(QDir::toNativeSeparators(path));
        return result;
    }
    if (!text_) {
        // .usd and .usdc: the binary form; .usdz: a package of it.
        QString err;
        int const code = runPython(QStringLiteral(
                                       "import sys\n"
                                       "try:\n    from pxr import Usd, UsdUtils, Sdf\n"
                                       "except ImportError:\n    sys.stderr.write('NO_USD_PACKAGE'); sys.exit(3)\n"
                                       "source, target = sys.argv[1], sys.argv[2]\n"
                                       "if target.lower().endswith('.usdz'):\n"
                                       "    crate = source[:-5] + '.usdc'\n"
                                       "    Sdf.Layer.FindOrOpen(source).Export(crate)\n"
                                       "    ok = UsdUtils.CreateNewUsdzPackage(Sdf.AssetPath(crate), target)\n"
                                       "else:\n"
                                       "    ok = Sdf.Layer.FindOrOpen(source).Export(target, args={'format': 'usdc'})\n"
                                       "sys.exit(0 if ok else 4)\n"),
                                   {plain, path}, &err);
        if (code != 0) {
            result = AtomAnimation::Result();
            bool const package = suffix == QLatin1String("usdz");
            result.error = code == -1 || err.contains(kNoPackage) ? needsPackage()
                           : code == 4 && package                ? tr("Failed to create USDZ package.")
                                                       : tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), err.section(QLatin1Char('\n'), -1));
            return result;
        }
    }
    if (result.layers == 0)
        result.warnings << tr("No layer chosen has keyframes: the file holds no animation.");
    if (std::abs(options.scale - 1.0) > 1e-9 && options.scale > 0.0)
        result.notes << tr("All 3D Elements will receive an additional scale of %1 when being exported.").arg(options.scale);
    if (options.fields)
        result.notes << tr("Exported position X,Y,Z values will be converted to align with Eclipse Field units.");
    result.ok = true;
    return result;
}

AtomAnimation::Result read(Scene& scene, QString const& path, AtomAnimation::Options const& options)
{
    AtomAnimation::Result result;
    if (path.trimmed().isEmpty()) {
        result.error = tr("No file has been selected for import.");
        return result;
    }
    if (!QFileInfo::exists(path)) {
        result.error = tr("The selected file does not exist.");
        return result;
    }
    // With the USD package: the stage opened and put together (references, layers) and read as text; without it, a .usda as it is.
    QTemporaryDir work;
    QString const flat = QDir(work.path()).filePath(QStringLiteral("flat.usda"));
    QString err;
    int const code = runPython(QStringLiteral(
                                   "import sys\n"
                                   "try:\n    from pxr import Usd\n"
                                   "except ImportError:\n    sys.stderr.write('NO_USD_PACKAGE'); sys.exit(3)\n"
                                   "stage = Usd.Stage.Open(sys.argv[1])\n"
                                   "if not stage:\n    sys.exit(5)\n"
                                   "open(sys.argv[2], 'w', encoding='utf-8').write(stage.Flatten().ExportToString())\n"),
                               {path, flat}, &err);
    QString source = code == 0 ? flat : QString();
    if (code != 0) {
        bool const plain = QFileInfo(path).suffix().compare(QLatin1String("usda"), Qt::CaseInsensitive) == 0;
        if (code == 5 || (code > 0 && !err.contains(kNoPackage))) {
            result.error = tr("Unable to open USD stage from selected file-path.");
            return result;
        }
        if (!plain) {
            result.error = needsPackage();
            return result;
        }
        source = path;
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("Unable to open USD stage from selected file-path.");
        return result;
    }
    return UsdAnimation::readText(scene, QString::fromUtf8(file.readAll()), options);
}

namespace {

// The text read a piece at a time: what the parser needs of USD's text form.
struct Reader {
    QString const& t;
    int at = 0;
    int line() const { return int(t.left(at).count(QLatin1Char('\n'))) + 1; }
    void space()
    {
        while (at < t.size()) {
            if (t[at].isSpace())
                ++at;
            else if (t[at] == QLatin1Char('#')) {
                while (at < t.size() && t[at] != QLatin1Char('\n'))
                    ++at;
            } else
                break;
        }
    }
    // A quoted string (", ' or triple quotes), passed by.
    void skipString()
    {
        QChar const q = t[at];
        bool const triple = t.mid(at, 3) == QString(3, q);
        at += triple ? 3 : 1;
        while (at < t.size()) {
            if (t[at] == QLatin1Char('\\')) {
                at += 2;
                continue;
            }
            if (triple ? t.mid(at, 3) == QString(3, q) : t[at] == q) {
                at += triple ? 3 : 1;
                return;
            }
            ++at;
        }
    }
    // A bracketed group ( [ { passed by, with what it holds.
    bool skipGroup()
    {
        QString const open = QStringLiteral("([{"), close = QStringLiteral(")]}");
        int depth = 0;
        while (at < t.size()) {
            QChar const c = t[at];
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                skipString();
                continue;
            }
            if (open.contains(c))
                ++depth;
            else if (close.contains(c) && --depth == 0) {
                ++at;
                return true;
            }
            ++at;
        }
        return false;
    }
};

QVector<double> numbersIn(QString const& text)
{
    static QRegularExpression const kNumber(QStringLiteral(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|[-+]?inf|nan)"));
    QVector<double> values;
    for (auto it = kNumber.globalMatch(text); it.hasNext();)
        values.push_back(it.next().captured(0).toDouble());
    return values;
}

struct Op {
    QString name;                       // translate, rotateXYZ, rotateX ..., scale
    QMap<double, QVector<double>> samples;
};

struct Prim {
    QString name;
    QVector<Op> ops;
    QStringList order;
};

}  // namespace

AtomAnimation::Result readText(Scene& scene, QString const& text, AtomAnimation::Options const& options)
{
    AtomAnimation::Result result;
    if (!text.trimmed().startsWith(QLatin1String("#usda"))) {
        result.error = tr("Unable to open USD stage from selected file-path.");
        return result;
    }
    Reader r{text};
    r.at = int(text.indexOf(QLatin1Char('\n')));
    double timeCodes = 24.0, start = 1.0;
    QVector<Prim> prims;
    QVector<int> scopes;                // the prims open (their place in `prims`), -1 for braces that are not a prim's
    QString pendingName;                // a prim's header read, its { not yet
    static QRegularExpression const kPrim(QStringLiteral(R"re(\G(?:def|over|class)\b(?:\s+\w+)?\s+"([^"]+)")re"));
    static QRegularExpression const kAttribute(QStringLiteral(R"(\G(?:custom\s+)?(?:uniform\s+)?[\w\[\]]+\s+(xformOp:[\w:]+)(\.timeSamples)?\s*=\s*)"));
    static QRegularExpression const kOrder(QStringLiteral(R"(\G(?:uniform\s+)?token\[\]\s+xformOpOrder\s*=\s*)"));
    bool header = true;
    while (true) {
        r.space();
        if (r.at >= text.size())
            break;
        QChar const c = text[r.at];
        if (header && c == QLatin1Char('(')) {
            // The stage's own values: its time codes a second and where it starts.
            int const from = r.at;
            if (!r.skipGroup()) {
                result.error = tr("USD syntax error at line: %1").arg(r.line());
                return result;
            }
            QString const meta = text.mid(from, r.at - from);
            static QRegularExpression const kMeta(QStringLiteral(R"((timeCodesPerSecond|startTimeCode)\s*=\s*([-+0-9.eE]+))"));
            for (auto it = kMeta.globalMatch(meta); it.hasNext();) {
                auto const m = it.next();
                (m.captured(1) == QLatin1String("timeCodesPerSecond") ? timeCodes : start) = m.captured(2).toDouble();
            }
            header = false;
            continue;
        }
        header = false;
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            r.skipString();
            continue;
        }
        if (auto const m = kPrim.match(text, r.at); m.hasMatch()) {
            pendingName = m.captured(1);
            r.at = int(m.capturedEnd());
            continue;
        }
        if (c == QLatin1Char('(') || c == QLatin1Char('[')) {
            if (!r.skipGroup()) {
                result.error = tr("USD syntax error at line: %1").arg(r.line());
                return result;
            }
            continue;
        }
        if (c == QLatin1Char('{')) {
            if (!pendingName.isEmpty()) {
                prims.push_back(Prim{pendingName, {}, {}});
                scopes.push_back(int(prims.size()) - 1);
                pendingName.clear();
            } else {
                scopes.push_back(-1);
            }
            ++r.at;
            continue;
        }
        if (c == QLatin1Char('}')) {
            if (scopes.isEmpty()) {
                result.error = tr("USD syntax error at line: %1").arg(r.line());
                return result;
            }
            scopes.removeLast();
            ++r.at;
            continue;
        }
        int const prim = scopes.isEmpty() ? -1 : scopes.last();
        if (prim >= 0) {
            if (auto const m = kOrder.match(text, r.at); m.hasMatch()) {
                r.at = int(m.capturedEnd());
                r.space();
                int const from = r.at;
                if (r.at >= text.size() || text[r.at] != QLatin1Char('[') || !r.skipGroup()) {
                    result.error = tr("USD syntax error at line: %1").arg(r.line());
                    return result;
                }
                static QRegularExpression const kQuoted(QStringLiteral(R"re("([^"]*)")re"));
                for (auto it = kQuoted.globalMatch(text.mid(from, r.at - from)); it.hasNext();)
                    prims[prim].order << it.next().captured(1);
                continue;
            }
            if (auto const m = kAttribute.match(text, r.at); m.hasMatch()) {
                Op op;
                op.name = m.captured(1).mid(8);    // after "xformOp:"
                bool const sampled = !m.captured(2).isEmpty();
                r.at = int(m.capturedEnd());
                r.space();
                int const from = r.at;
                if (r.at < text.size() && (text[r.at] == QLatin1Char('{') || text[r.at] == QLatin1Char('(') || text[r.at] == QLatin1Char('['))) {
                    if (!r.skipGroup()) {
                        result.error = tr("USD syntax error at line: %1").arg(r.line());
                        return result;
                    }
                } else {
                    while (r.at < text.size() && text[r.at] != QLatin1Char('\n'))
                        ++r.at;
                }
                QString const value = text.mid(from, r.at - from);
                if (sampled) {
                    // { time: value, time: value, ... }
                    QString const inside = value.mid(1, value.size() - 2);
                    static QRegularExpression const kEntry(QStringLiteral(R"(([-+0-9.eE]+)\s*:\s*(\([^)]*\)|[^,\n]+))"));
                    for (auto it = kEntry.globalMatch(inside); it.hasNext();) {
                        auto const e = it.next();
                        op.samples.insert(e.captured(1).toDouble(), numbersIn(e.captured(2)));
                    }
                } else {
                    op.samples.insert(start, numbersIn(value));
                }
                // (A value of its own beside samples: the samples win.)
                bool merged = false;
                for (Op& other : prims[prim].ops)
                    if (other.name == op.name) {
                        if (sampled || other.samples.isEmpty())
                            other.samples = op.samples;
                        merged = true;
                    }
                if (!merged)
                    prims[prim].ops.push_back(op);
                continue;
            }
        }
        // Anything else: a word passed by.
        while (r.at < text.size() && !text[r.at].isSpace() && !QStringLiteral("()[]{}\"'").contains(text[r.at]))
            ++r.at;
        if (r.at < text.size() && QStringLiteral(")]").contains(text[r.at]))
            ++r.at;
    }
    if (!scopes.isEmpty()) {
        result.error = tr("USD syntax error at line: %1").arg(r.line());
        return result;
    }

    // The prims onto the layers of their names.
    double const field = options.fields ? scene.width / 12.0 : 1.0;
    double const scale = options.scale > 0.0 ? options.scale : 1.0;
    QStringList missing, notAnimatable, notRead, inverse;
    QSet<int> touched;
    for (Prim const& prim : std::as_const(prims)) {
        bool const moving = !prim.ops.isEmpty();
        if (!moving)
            continue;
        int layerAt = -1;
        for (int l = 0; l < scene.layers.size() && layerAt < 0; ++l)
            if (scene.layers[l].name == prim.name || nodeName(scene.layers[l]) == prim.name)
                layerAt = l;
        if (layerAt < 0) {
            missing << prim.name;
            continue;
        }
        Layer& layer = scene.layers[layerAt];
        if (!animatable(layer)) {
            notAnimatable << prim.name;
            continue;
        }
        // An op that is only undone in the order (its pair missing) cannot be given values.
        for (QString const& entry : prim.order)
            if (entry.startsWith(QLatin1String("!invert!"))) {
                QString const pair = entry.mid(8);
                if (!prim.order.contains(pair))
                    inverse << pair;
            }
        auto keyAt = [&](int frame) -> Keyframe& {
            int i = 0;
            while (i < layer.keys.size() && layer.keys[i].frame < frame)
                ++i;
            if (i >= layer.keys.size() || layer.keys[i].frame != frame) {
                Keyframe k;
                k.frame = frame;
                k.pose = layer.poseAt(frame);
                k.parts = 0;
                k.tween = true;
                layer.keys.insert(i, k);
            }
            return layer.keys[i];
        };
        for (Op const& op : prim.ops) {
            // Pivots (translate:pivot with its inverse) move nothing on their own.
            if (op.name.contains(QLatin1Char(':')))
                continue;
            bool const known = op.name == QLatin1String("translate") || op.name == QLatin1String("rotateXYZ") || op.name == QLatin1String("rotateX")
                               || op.name == QLatin1String("rotateY") || op.name == QLatin1String("rotateZ") || op.name == QLatin1String("scale");
            if (!known) {
                notRead << QStringLiteral("%1.xformOp:%2").arg(prim.name, op.name);
                continue;
            }
            for (auto it = op.samples.begin(); it != op.samples.end(); ++it) {
                QVector<double> const& v = it.value();
                int const frame = std::max(0, int(std::lround((it.key() - start) * scene.fps / std::max(1e-6, timeCodes))));
                Keyframe& k = keyAt(frame);
                if (op.name == QLatin1String("translate") && v.size() >= 3) {
                    k.pose.x = v[0] * field * scale;
                    k.pose.y = -v[1] * field * scale;
                    k.pose.z = v[2] * field * scale;
                    k.parts |= Keyframe::PartX | Keyframe::PartY | Keyframe::PartZ;
                } else if (op.name == QLatin1String("rotateXYZ") && v.size() >= 3) {
                    k.pose.rotX = v[0];
                    k.pose.rotY = v[1];
                    k.pose.rotation = -v[2];
                    k.parts |= Keyframe::PartAngle | Keyframe::PartRest;
                } else if (op.name == QLatin1String("rotateZ") && !v.isEmpty()) {
                    k.pose.rotation = -v[0];
                    k.parts |= Keyframe::PartAngle;
                } else if ((op.name == QLatin1String("rotateX") || op.name == QLatin1String("rotateY")) && !v.isEmpty()) {
                    (op.name == QLatin1String("rotateX") ? k.pose.rotX : k.pose.rotY) = v[0];
                    k.parts |= Keyframe::PartRest;
                } else if (op.name == QLatin1String("scale") && v.size() >= 2) {
                    k.pose.scaleX = v[0];
                    k.pose.scaleY = v[1];
                    k.parts |= Keyframe::PartScaleX | Keyframe::PartScaleY;
                } else {
                    continue;
                }
                ++result.keys;
            }
        }
        touched.insert(layerAt);
    }
    result.layers = int(touched.size());
    if (!missing.isEmpty())
        result.warnings << tr("The following nodes were not found: %1").arg(missing.join(QStringLiteral(", ")));
    for (QString const& node : std::as_const(notAnimatable))
        result.warnings << tr("Animation for node '%1' could not be imported, because this node can not be animated directly.\nTry to use an animated Peg instead.").arg(node);
    if (!notRead.isEmpty())
        result.warnings << tr("These transforms could not be imported (only translate, rotate and scale are): %1").arg(notRead.join(QStringLiteral(", ")));
    for (QString const& op : std::as_const(inverse))
        result.warnings << tr("Cannot set a value on the inverse xformOp '%1'. Please set value on the paired non-inverse xformOp instead.").arg(op);
    if (std::abs(options.scale - 1.0) > 1e-9 && options.scale > 0.0)
        result.notes << tr("All 3D Elements will receive an additional scale of %1 when being imported.").arg(options.scale);
    if (options.fields)
        result.notes << tr("Imported position X,Y,Z values will be converted to align with Eclipse Field units.");
    result.ok = true;
    return result;
}

}  // namespace UsdAnimation
