// Eclipse - layers' keyframes in ATOM files (see AtomAnimation.h).
#include "formats/anim/AtomAnimation.h"

#include "formats/anim/UsdAnimation.h"
#include "model/scene/Scene.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace AtomAnimation {

namespace {

QString tr(char const* text, char const* note = nullptr, int n = -1)
{
    return QCoreApplication::translate("AtomAnimation", text, note, n);
}

// One curve of a layer: what it is called in the file, and where it goes in a pose.
struct Channel {
    char const* attribute;      // translate.translateX ...
    char const* leaf;           // translateX ...
    int part;                   // the keyframe part it sets
    int value;                  // its value for a curve of its own (Driver::Value), or -1
    double sign;                // the file's way round against Eclipse's
    bool position;
    std::function<double(Pose const&)> get;
    std::function<void(Pose&, double)> set;
};

QVector<Channel> const& channels()
{
    static QVector<Channel> const all = {
        {"translate.translateX", "translateX", Keyframe::PartX, Driver::X, 1.0, true, [](Pose const& p) { return p.x; }, [](Pose& p, double v) { p.x = v; }},
        {"translate.translateY", "translateY", Keyframe::PartY, Driver::Y, -1.0, true, [](Pose const& p) { return p.y; }, [](Pose& p, double v) { p.y = v; }},
        {"translate.translateZ", "translateZ", Keyframe::PartZ, Driver::Z, 1.0, true, [](Pose const& p) { return p.z; }, [](Pose& p, double v) { p.z = v; }},
        {"rotate.rotateX", "rotateX", Keyframe::PartRest, -1, 1.0, false, [](Pose const& p) { return p.rotX; }, [](Pose& p, double v) { p.rotX = v; }},
        {"rotate.rotateY", "rotateY", Keyframe::PartRest, -1, 1.0, false, [](Pose const& p) { return p.rotY; }, [](Pose& p, double v) { p.rotY = v; }},
        {"rotate.rotateZ", "rotateZ", Keyframe::PartAngle, Driver::Angle, -1.0, false, [](Pose const& p) { return p.rotation; }, [](Pose& p, double v) { p.rotation = v; }},
        {"scale.scaleX", "scaleX", Keyframe::PartScaleX, Driver::ScaleX, 1.0, false, [](Pose const& p) { return p.scaleX; }, [](Pose& p, double v) { p.scaleX = v; }},
        {"scale.scaleY", "scaleY", Keyframe::PartScaleY, Driver::ScaleY, 1.0, false, [](Pose const& p) { return p.scaleY; }, [](Pose& p, double v) { p.scaleY = v; }},
    };
    return all;
}

int channelOf(QString const& name)
{
    for (int c = 0; c < channels().size(); ++c)
        if (name == QLatin1String(channels()[c].leaf) || name == QLatin1String(channels()[c].attribute))
            return c;
    return -1;
}

// The file's frames a second, by the name it gives them.
double unitRate(QString const& unit, double fallback)
{
    static QHash<QString, double> const named = {{QStringLiteral("film"), 24.0}, {QStringLiteral("pal"), 25.0}, {QStringLiteral("ntsc"), 30.0},
                                                 {QStringLiteral("game"), 15.0}, {QStringLiteral("show"), 48.0}, {QStringLiteral("palf"), 50.0},
                                                 {QStringLiteral("ntscf"), 60.0}};
    if (named.contains(unit))
        return named.value(unit);
    if (unit.endsWith(QLatin1String("fps"))) {
        bool ok = false;
        double const rate = unit.chopped(3).toDouble(&ok);
        if (ok && rate > 0.0)
            return rate;
    }
    return fallback;
}

QString unitName(double fps)
{
    static QMap<double, QString> const named = {{24.0, QStringLiteral("film")}, {25.0, QStringLiteral("pal")}, {30.0, QStringLiteral("ntsc")},
                                                {15.0, QStringLiteral("game")}, {48.0, QStringLiteral("show")}, {50.0, QStringLiteral("palf")},
                                                {60.0, QStringLiteral("ntscf")}};
    for (auto it = named.begin(); it != named.end(); ++it)
        if (std::abs(it.key() - fps) < 1e-6)
            return it.value();
    return QStringLiteral("%1fps").arg(fps);
}

// A layer's name as a node of the file (one word).
QString nodeName(Layer const& layer)
{
    return layer.name.simplified().replace(QLatin1Char(' '), QLatin1Char('_'));
}

bool animatable(Layer const& layer)
{
    return layer.kind == Layer::DrawingLayer || layer.kind == Layer::Peg || layer.kind == Layer::Camera || layer.kind == Layer::Bone
           || layer.kind == Layer::Quadmap;
}

// The words of the file, with the line each is on; ; { and } words of their own.
struct Word {
    QString text;
    int line;
};

QVector<Word> wordsOf(QString const& text)
{
    QVector<Word> words;
    int line = 1;
    QString word;
    auto end = [&] {
        if (!word.isEmpty())
            words.push_back({word, line});
        word.clear();
    };
    for (int i = 0; i < text.size(); ++i) {
        QChar const c = text[i];
        if (c == QLatin1Char('/') && i + 1 < text.size() && text[i + 1] == QLatin1Char('/')) {
            end();
            while (i < text.size() && text[i] != QLatin1Char('\n'))
                ++i;
            --i;
            continue;
        }
        if (c == QLatin1Char('\n')) {
            end();
            ++line;
        } else if (c.isSpace()) {
            end();
        } else if (c == QLatin1Char(';') || c == QLatin1Char('{') || c == QLatin1Char('}')) {
            end();
            words.push_back({QString(c), line});
        } else {
            word += c;
        }
    }
    end();
    return words;
}

struct Key {
    double time = 0.0, value = 0.0;
    QString in, out;
};

struct Curve {
    QString node;
    int channel = -1;
    QString attribute;
    QVector<Key> keys;
};

}  // namespace

QString writeText(Scene const& scene, Options const& options, Result* result)
{
    double const field = options.fields ? scene.width / 12.0 : 1.0;
    double const scale = (options.scale > 0.0 ? options.scale : 1.0);
    int first = scene.frameCount(), last = 0;
    QString body;
    QTextStream out(&body);
    for (int l = 0; l < scene.layers.size(); ++l) {
        Layer const& layer = scene.layers[l];
        if (options.selectedOnly && !options.layers.contains(l))
            continue;
        if (layer.keys.isEmpty() || !animatable(layer))
            continue;
        out << "dagNode {\n\t" << nodeName(layer) << " 0 0;\n";
        int index = 0;
        for (Channel const& c : channels()) {
            QVector<Keyframe const*> keyed;
            bool moves = false;
            for (Keyframe const& k : layer.keys)
                if (k.sets(c.part)) {
                    keyed.push_back(&k);
                    moves = moves || std::abs(c.get(k.pose) - c.get(Pose())) > 1e-9;
                }
            // (Turns about X and Y that are never made are left out.)
            if (keyed.isEmpty() || (c.part == Keyframe::PartRest && !moves))
                continue;
            out << "\tanim " << c.attribute << ' ' << c.leaf << ' ' << nodeName(layer) << " 0 0 " << index++ << ";\n";
            out << "\tanimData {\n\t\tinput time;\n\t\toutput " << (c.position ? "linear" : c.part == Keyframe::PartAngle || c.part == Keyframe::PartRest ? "angular" : "unitless")
                << ";\n\t\tweighted 0;\n\t\tpreInfinity constant;\n\t\tpostInfinity constant;\n\t\tkeys {\n";
            for (Keyframe const* k : std::as_const(keyed)) {
                double value = c.get(k->pose) * c.sign;
                if (c.position)
                    value = value / field / scale;
                value += 0.0;   // (no "-0" in the file)
                ValueCurve const* const curve = c.value >= 0 ? k->curveOf(c.value) : nullptr;
                QString const type = !k->tween || (curve && curve->after == ValueCurve::Held) ? QStringLiteral("step")
                                     : curve && curve->after == ValueCurve::Straight             ? QStringLiteral("linear")
                                                                                                   : QStringLiteral("auto");
                out << "\t\t\t" << (k->frame + 1) << ' ' << QString::number(value, 'g', 10) << ' ' << type << ' ' << type << " 1 1 0;\n";
                first = std::min(first, k->frame);
                last = std::max(last, k->frame);
                if (result)
                    ++result->keys;
            }
            out << "\t\t}\n\t}\n";
        }
        out << "}\n";
        if (result)
            ++result->layers;
    }
    out.flush();
    QString text;
    QTextStream head(&text);
    head << "atomVersion 1.0;\n"
         << "exporter Eclipse;\n"
         << "timeUnit " << unitName(scene.fps) << ";\n"
         << "linearUnit " << (options.fields ? "field" : "unit") << ";\n"
         << "angularUnit deg;\n"
         << "startTime " << (first <= last ? first + 1 : 1) << ";\n"
         << "endTime " << (first <= last ? last + 1 : 1) << ";\n";
    head.flush();
    return text + body;
}

Result write(Scene const& scene, QString const& path, Options const& options)
{
    if (UsdAnimation::isUsd(path))
        return UsdAnimation::write(scene, path, options);
    Result result;
    if (path.trimmed().isEmpty()) {
        result.error = tr("No file has been selected for export.");
        return result;
    }
    if (QFileInfo(path).suffix().compare(QLatin1String("atom"), Qt::CaseInsensitive) != 0) {
        result.error = tr("Unsupported file-extension '%1' for animation export.").arg(QFileInfo(path).suffix());
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
    QString const text = writeText(scene, options, &result);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        result = Result();
        result.error = tr("Error while opening the .atom file for writing.");
        return result;
    }
    if (result.layers == 0)
        result.warnings << tr("No layer chosen has keyframes: the file holds no animation.");
    if (options.scale > 0.0 && std::abs(options.scale - 1.0) > 1e-9)
        result.notes << tr("All 3D Elements will receive an additional scale of %1 when being exported.").arg(options.scale);
    if (options.fields)
        result.notes << tr("Exported position X,Y,Z values will be converted to align with Eclipse Field units.");
    result.ok = true;
    return result;
}

Result read(Scene& scene, QString const& path, Options const& options)
{
    Result result;
    if (path.trimmed().isEmpty()) {
        result.error = tr("No file has been selected for import.");
        return result;
    }
    QString const suffix = QFileInfo(path).suffix().toLower();
    if (UsdAnimation::isUsd(path))
        return UsdAnimation::read(scene, path, options);
    if (suffix != QLatin1String("atom")) {
        result.error = tr("Unsupported file-extension '%1' for animation import.").arg(suffix);
        return result;
    }
    if (!QFileInfo::exists(path)) {
        result.error = tr("The selected file does not exist.");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = tr("Error while opening the .atom file for reading.");
        return result;
    }
    return readText(scene, QString::fromUtf8(file.readAll()), options);
}

Result readText(Scene& scene, QString const& text, Options const& options)
{
    Result result;
    QVector<Word> const words = wordsOf(text);
    int at = 0;
    auto syntax = [&](int line) {
        result = Result();
        result.error = tr("ATOM syntax error at line: %1").arg(line);
        return result;
    };
    auto lineAt = [&](int i) { return i < words.size() ? words[i].line : (words.isEmpty() ? 1 : words.last().line); };
    // A statement: the words up to its ;.
    auto statement = [&](QStringList* out) -> bool {
        out->clear();
        while (at < words.size() && words[at].text != QLatin1String(";")) {
            if (words[at].text == QLatin1String("{") || words[at].text == QLatin1String("}"))
                return false;
            *out << words[at++].text;
        }
        if (at >= words.size())
            return false;
        ++at;   // ;
        return true;
    };
    // A block not understood: passed by, whatever it holds.
    auto skipBlock = [&]() -> bool {
        int depth = 0;
        for (; at < words.size(); ++at) {
            if (words[at].text == QLatin1String("{"))
                ++depth;
            else if (words[at].text == QLatin1String("}") && --depth == 0) {
                ++at;
                return true;
            }
        }
        return false;
    };
    QString timeUnit = QStringLiteral("film"), angularUnit = QStringLiteral("deg");
    QVector<Curve> curves;
    auto readAnimData = [&](Curve& curve) -> bool {
        // animData { ... keys { time value in out ... ; } }
        ++at;   // {
        while (at < words.size() && words[at].text != QLatin1String("}")) {
            if (words[at].text == QLatin1String("keys") && at + 1 < words.size() && words[at + 1].text == QLatin1String("{")) {
                at += 2;
                QStringList key;
                while (at < words.size() && words[at].text != QLatin1String("}")) {
                    int const line = lineAt(at);
                    if (!statement(&key) || key.size() < 2)
                        return (void)syntax(line), false;
                    bool okTime = false, okValue = false;
                    Key k;
                    k.time = key[0].toDouble(&okTime);
                    k.value = key[1].toDouble(&okValue);
                    if (!okTime || !okValue)
                        return (void)syntax(line), false;
                    k.in = key.value(2);
                    k.out = key.value(3);
                    curve.keys.push_back(k);
                }
                if (at >= words.size())
                    return (void)syntax(lineAt(at)), false;
                ++at;   // } of keys
                continue;
            }
            QStringList skipped;
            int const line = lineAt(at);
            if (at < words.size() && words[at].text == QLatin1String("{")) {
                if (!skipBlock())
                    return (void)syntax(line), false;
                continue;
            }
            if (!statement(&skipped))
                return (void)syntax(line), false;
        }
        if (at >= words.size())
            return (void)syntax(lineAt(at)), false;
        ++at;   // } of animData
        return true;
    };
    while (at < words.size()) {
        QString const head = words[at].text;
        int const line = lineAt(at);
        if (head == QLatin1String("dagNode") || head == QLatin1String("node")) {
            ++at;
            if (at >= words.size() || words[at].text != QLatin1String("{"))
                return syntax(line);
            ++at;
            QStringList name;
            if (!statement(&name) || name.isEmpty())
                return syntax(line);
            QString const node = name[0];
            while (at < words.size() && words[at].text != QLatin1String("}")) {
                int const inner = lineAt(at);
                QStringList anim;
                if (words[at].text == QLatin1String("{")) {
                    if (!skipBlock())
                        return syntax(inner);
                    continue;
                }
                if (!statement(&anim))
                    return syntax(inner);
                if (anim.value(0) != QLatin1String("anim"))
                    continue;
                Curve curve;
                curve.node = node;
                curve.attribute = anim.value(2, anim.value(1));
                curve.channel = channelOf(anim.value(2));
                if (curve.channel < 0)
                    curve.channel = channelOf(anim.value(1));
                if (at < words.size() && words[at].text == QLatin1String("animData")) {
                    ++at;
                    if (at >= words.size() || words[at].text != QLatin1String("{") || !readAnimData(curve))
                        return result.error.isEmpty() ? syntax(inner) : result;
                }
                curves.push_back(curve);
            }
            if (at >= words.size())
                return syntax(line);
            ++at;   // }
            continue;
        }
        if (head == QLatin1String("{") || head == QLatin1String("}"))
            return syntax(line);
        QStringList header;
        if (!statement(&header)) {
            // A block of the header not understood.
            if (at < words.size() && words[at].text == QLatin1String("{") && skipBlock())
                continue;
            return syntax(line);
        }
        if (header.value(0) == QLatin1String("timeUnit"))
            timeUnit = header.value(1);
        else if (header.value(0) == QLatin1String("angularUnit"))
            angularUnit = header.value(1);
    }

    // The curves onto the layers of their names.
    double const rate = unitRate(timeUnit, scene.fps);
    double const field = options.fields ? scene.width / 12.0 : 1.0;
    double const scale = (options.scale > 0.0 ? options.scale : 1.0);
    bool const radians = angularUnit.startsWith(QLatin1String("rad"));
    QStringList missing, notAnimatable, notRead;
    QSet<int> touched;
    for (Curve const& curve : std::as_const(curves)) {
        int layerAt = -1;
        for (int l = 0; l < scene.layers.size() && layerAt < 0; ++l)
            if (scene.layers[l].name == curve.node || nodeName(scene.layers[l]) == curve.node)
                layerAt = l;
        if (layerAt < 0) {
            if (!missing.contains(curve.node))
                missing << curve.node;
            continue;
        }
        Layer& layer = scene.layers[layerAt];
        if (!animatable(layer)) {
            if (!notAnimatable.contains(curve.node))
                notAnimatable << curve.node;
            continue;
        }
        if (curve.channel < 0) {
            notRead << QStringLiteral("%1.%2").arg(curve.node, curve.attribute);
            continue;
        }
        Channel const& c = channels()[curve.channel];
        for (Key const& key : curve.keys) {
            int const frame = std::max(0, int(std::lround(key.time * scene.fps / rate)) - 1);
            double value = key.value * c.sign;
            if (c.position)
                value = value * field * scale;
            else if (radians && !c.position && c.part != Keyframe::PartScaleX && c.part != Keyframe::PartScaleY)
                value = value * 180.0 / std::numbers::pi;
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
            Keyframe& k = layer.keys[i];
            k.parts |= c.part;
            c.set(k.pose, value);
            if (c.value >= 0 && (key.out == QLatin1String("step") || key.out == QLatin1String("linear"))) {
                ValueCurve shaped;
                shaped.value = c.value;
                shaped.after = key.out == QLatin1String("step") ? ValueCurve::Held : ValueCurve::Straight;
                k.setCurve(shaped);
            }
            ++result.keys;
        }
        touched.insert(layerAt);
        if (layer.keys.size() && layer.keys.last().frame >= scene.frameCount())
            result.warnings << tr("%1: keyframes past the end of the scene.").arg(layer.name);
    }
    result.layers = int(touched.size());
    if (!missing.isEmpty())
        result.warnings << tr("The following nodes were not found: %1").arg(missing.join(QStringLiteral(", ")));
    for (QString const& node : std::as_const(notAnimatable))
        result.warnings << tr("Animation for node '%1' could not be imported, because this node can not be animated directly.\nTry to use an animated Peg instead.").arg(node);
    if (!notRead.isEmpty())
        result.warnings << tr("ATOM node attributes could not be imported: %1").arg(notRead.join(QStringLiteral(", ")));
    if (options.scale > 0.0 && std::abs(options.scale - 1.0) > 1e-9)
        result.notes << tr("All 3D Elements will receive an additional scale of %1 when being imported.").arg(options.scale);
    if (options.fields)
        result.notes << tr("Imported position X,Y,Z values will be converted to align with Eclipse Field units.");
    result.ok = true;
    return result;
}

}  // namespace AtomAnimation
