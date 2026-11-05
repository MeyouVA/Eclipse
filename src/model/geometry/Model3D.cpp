#include "model/geometry/Model3D.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <limits>

#include "model/scene/Scene.h"

namespace {

struct Kept {
    QDateTime changed;
    std::shared_ptr<Model3D::Mesh const> mesh;
};
// The pictures made lately, kept: a model held still is not drawn again each time it is shown.
struct Made {
    std::shared_ptr<Model3D::Mesh const> mesh;
    Model3D::Look look;
    QImage image;
    QTransform placement;
};
QMutex keptMutex;
QHash<QString, Kept> kept;
QMutex madeMutex;
QVector<Made> made;

QList<QByteArray> wordsOf(QByteArray const& line)
{
    return line.simplified().split(' ');
}

// The materials of an MTL file: their colours and the pictures they are painted with.
void readMaterials(QString const& file, QVector<Model3D::Material>& into)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QString const folder = QFileInfo(file).path();
    for (QByteArray const& raw : f.readAll().split('\n')) {
        QList<QByteArray> const w = wordsOf(raw);
        if (w.isEmpty() || w[0].isEmpty() || w[0].startsWith('#'))
            continue;
        if (w[0] == "newmtl" && w.size() >= 2) {
            Model3D::Material m;
            m.name = QString::fromUtf8(raw.simplified().mid(7));
            into.push_back(m);
        } else if (into.isEmpty()) {
            continue;
        } else if (w[0] == "Kd" && w.size() >= 4) {
            auto one = [](QByteArray const& v) { return std::clamp(int(std::lround(v.toDouble() * 255.0)), 0, 255); };
            into.back().colour = QColor(one(w[1]), one(w[2]), one(w[3]));
        } else if (w[0] == "map_Kd" && w.size() >= 2) {
            // (The file is the last word: options may come before it.)
            QString name = QString::fromUtf8(w.back());
            QImage texture(QDir(folder).absoluteFilePath(name));
            if (texture.isNull())
                texture = QImage(QDir(folder).absoluteFilePath(QFileInfo(name).fileName()));
            if (!texture.isNull())
                into.back().texture = texture.convertToFormat(QImage::Format_RGB32);
        }
    }
}

QJsonArray vector(QVector3D const& v)
{
    return QJsonArray{double(v.x()), double(v.y()), double(v.z())};
}

QVector3D vectorOf(QJsonValue const& value, QVector3D fallback)
{
    QJsonArray const a = value.toArray();
    return a.size() == 3 ? QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())) : fallback;
}

// A part's own move, turn and scale about its pivot, as a matrix.
QMatrix4x4 overrideMatrix(Model3D::NodeOverride const& o, QVector3D const& pivot)
{
    QMatrix4x4 m;
    QVector3D const about = pivot + o.pivot;
    m.translate(o.move + about);
    m.rotate(o.turn.z(), 0, 0, 1);
    m.rotate(o.turn.y(), 0, 1, 0);
    m.rotate(o.turn.x(), 1, 0, 0);
    m.scale(o.scale);
    m.translate(-about);
    return m;
}

}

QVector<int> Model3D::Mesh::children(int node) const
{
    QVector<int> found;
    for (int n = 0; n < nodes.size(); ++n)
        if (nodes[n].parent == node)
            found.push_back(n);
    return found;
}

bool Model3D::Mesh::under(int node, int ancestor) const
{
    for (int n = node, guard = 0; n >= 0 && n < nodes.size() && guard <= nodes.size(); n = nodes[n].parent, ++guard)
        if (n == ancestor)
            return true;
    return false;
}

int Model3D::Mesh::nodeNamed(QString const& name) const
{
    for (int n = 0; n < nodes.size(); ++n)
        if (nodes[n].name == name)
            return n;
    return -1;
}

QVector3D Model3D::Shading::direction() const
{
    // (As it always was, where it is left as it starts.)
    if (azimuth == Shading().azimuth && elevation == Shading().elevation)
        return QVector3D(-0.35f, -0.5f, 1.0f).normalized();
    double const az = qDegreesToRadians(azimuth), el = qDegreesToRadians(elevation);
    return QVector3D(float(std::cos(el) * std::sin(az)), float(-std::sin(el)), float(std::cos(el) * std::cos(az))).normalized();
}

Model3D::NodeOverride const* Model3D::Look::overrideOf(QString const& name) const
{
    for (NodeOverride const& o : overrides)
        if (o.name == name)
            return &o;
    return nullptr;
}

Model3D::NodeOverride& Model3D::Look::overrideFor(QString const& name)
{
    for (NodeOverride& o : overrides)
        if (o.name == name)
            return o;
    NodeOverride o;
    o.name = name;
    overrides.push_back(o);
    return overrides.back();
}

void Model3D::Look::tidy()
{
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(), [](NodeOverride const& o) { return o.plain(); }), overrides.end());
}

QJsonObject Model3D::toJson(Look const& look)
{
    Look const plain;
    QJsonObject out;
    if (look.rotX != 0.0 || look.rotY != 0.0 || look.rotZ != 0.0)
        out["turn"] = QJsonArray{look.rotX, look.rotY, look.rotZ};
    if (look.stretch != plain.stretch)
        out["stretch"] = vector(look.stretch);
    if (look.focal != 0.0)
        out["focal"] = look.focal;
    if (look.truck != 0.0)
        out["truck"] = look.truck;
    Shading const& s = look.shading;
    if (!(s == plain.shading)) {
        QJsonObject shading;
        shading["azimuth"] = s.azimuth;
        shading["elevation"] = s.elevation;
        shading["light"] = s.light.name(QColor::HexRgb);
        shading["intensity"] = s.intensity;
        shading["ambient"] = s.ambient;
        shading["specular"] = s.specular;
        shading["shininess"] = s.shininess;
        shading["smooth"] = s.smooth;
        shading["toonBands"] = s.toonBands;
        shading["lineRadius"] = s.lineRadius;
        shading["lineColour"] = s.lineColour.name(QColor::HexRgb);
        shading["grey"] = s.grey;
        out["shading"] = shading;
    }
    QJsonArray parts;
    for (NodeOverride const& o : look.overrides) {
        if (o.plain())
            continue;
        QJsonObject part{{"name", o.name}};
        if (o.hidden)
            part["hidden"] = true;
        if (o.shapeHidden)
            part["shapeHidden"] = true;
        if (o.available)
            part["available"] = true;
        if (!o.move.isNull())
            part["move"] = vector(o.move);
        if (!o.turn.isNull())
            part["turn"] = vector(o.turn);
        if (o.scale != QVector3D(1, 1, 1))
            part["scale"] = vector(o.scale);
        if (!o.pivot.isNull())
            part["pivot"] = vector(o.pivot);
        parts.append(part);
    }
    if (!parts.isEmpty())
        out["parts"] = parts;
    return out;
}

Model3D::Look Model3D::lookFromJson(QJsonObject const& json)
{
    Look look;
    if (QJsonArray const turn = json["turn"].toArray(); turn.size() == 3) {
        look.rotX = turn[0].toDouble();
        look.rotY = turn[1].toDouble();
        look.rotZ = turn[2].toDouble();
    }
    look.stretch = vectorOf(json["stretch"], look.stretch);
    look.focal = std::clamp(json["focal"].toDouble(), 0.0, 1000.0);
    look.truck = std::clamp(json["truck"].toDouble(), -50.0, 50.0);
    if (json.contains("shading")) {
        QJsonObject const s = json["shading"].toObject();
        Shading& to = look.shading;
        Shading const plain;
        to.azimuth = s["azimuth"].toDouble(plain.azimuth);
        to.elevation = std::clamp(s["elevation"].toDouble(plain.elevation), -90.0, 90.0);
        to.light = QColor(s["light"].toString(QStringLiteral("#ffffff")));
        to.intensity = std::clamp(s["intensity"].toDouble(plain.intensity), 0.0, 10.0);
        to.ambient = std::clamp(s["ambient"].toDouble(plain.ambient), 0.0, 1.0);
        to.specular = std::clamp(s["specular"].toDouble(), 0.0, 1.0);
        to.shininess = std::clamp(s["shininess"].toDouble(plain.shininess), 1.0, 512.0);
        to.smooth = s["smooth"].toBool();
        to.toonBands = std::clamp(s["toonBands"].toInt(), 0, 16);
        to.lineRadius = std::clamp(s["lineRadius"].toDouble(), 0.0, 32.0);
        to.lineColour = QColor(s["lineColour"].toString(QStringLiteral("#000000")));
        to.grey = s["grey"].toBool();
    }
    for (QJsonValue const& value : json["parts"].toArray()) {
        QJsonObject const p = value.toObject();
        NodeOverride o;
        o.name = p["name"].toString();
        o.hidden = p["hidden"].toBool();
        o.shapeHidden = p["shapeHidden"].toBool();
        o.available = p["available"].toBool();
        o.move = vectorOf(p["move"], QVector3D());
        o.turn = vectorOf(p["turn"], QVector3D());
        o.scale = vectorOf(p["scale"], QVector3D(1, 1, 1));
        o.pivot = vectorOf(p["pivot"], QVector3D());
        if (!o.name.isEmpty())
            look.overrides.push_back(o);
    }
    return look;
}

bool Model3D::finish(Mesh& mesh, QString* error)
{
    if (mesh.triangles.isEmpty()) {
        if (error)
            *error = QObject::tr("There are no faces in the model.");
        return false;
    }
    if (mesh.nodes.isEmpty())
        mesh.nodes.push_back(Node{QObject::tr("Model"), -1, {}, 0});
    float const big = std::numeric_limits<float>::max();
    QVector<QVector3D> lows(mesh.nodes.size(), QVector3D(big, big, big)), highs(mesh.nodes.size(), QVector3D(-big, -big, -big));
    for (Node& n : mesh.nodes)
        n.triangles = 0;
    for (Triangle& t : mesh.triangles) {
        if (t.node < 0 || t.node >= mesh.nodes.size())
            t.node = 0;
        ++mesh.nodes[t.node].triangles;
        for (int v : t.v) {
            QVector3D const& p = mesh.vertices[v];
            // (Into the box of its part and of each part it hangs under.)
            for (int n = t.node, guard = 0; n >= 0 && guard <= mesh.nodes.size(); n = mesh.nodes[n].parent, ++guard) {
                lows[n] = QVector3D(std::min(lows[n].x(), p.x()), std::min(lows[n].y(), p.y()), std::min(lows[n].z(), p.z()));
                highs[n] = QVector3D(std::max(highs[n].x(), p.x()), std::max(highs[n].y(), p.y()), std::max(highs[n].z(), p.z()));
            }
        }
    }
    QVector3D lo = lows[0], hi = highs[0];
    for (int n = 0; n < mesh.nodes.size(); ++n) {
        if (lows[n].x() <= highs[n].x())
            mesh.nodes[n].pivot = (lows[n] + highs[n]) / 2.0f;
        if (mesh.nodes[n].parent < 0 && lows[n].x() <= highs[n].x()) {
            lo = QVector3D(std::min(lo.x(), lows[n].x()), std::min(lo.y(), lows[n].y()), std::min(lo.z(), lows[n].z()));
            hi = QVector3D(std::max(hi.x(), highs[n].x()), std::max(hi.y(), highs[n].y()), std::max(hi.z(), highs[n].z()));
        }
    }
    mesh.centre = (lo + hi) / 2.0f;
    QVector3D const size = hi - lo;
    mesh.extent = std::max({double(size.x()), double(size.y()), double(size.z()), 1e-6});
    return true;
}

std::shared_ptr<Model3D::Mesh const> Model3D::parseObj(QByteArray const& text, QString const& folder, QString* error)
{
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("OBJ");
    mesh->nodes.push_back(Node{QObject::tr("Model"), -1, {}, 0});
    int material = -1;
    int object = 0;     // the object (o) the faces go in
    int node = 0;       // and the group (g) in it
    auto partNamed = [&](QString const& name, int parent) {
        for (int n = 1; n < mesh->nodes.size(); ++n)
            if (mesh->nodes[n].name == name && mesh->nodes[n].parent == parent)
                return n;
        mesh->nodes.push_back(Node{name, parent, {}, 0});
        return int(mesh->nodes.size()) - 1;
    };
    for (QByteArray const& raw : text.split('\n')) {
        QList<QByteArray> const w = wordsOf(raw);
        if (w.isEmpty() || w[0].isEmpty() || w[0].startsWith('#'))
            continue;
        if (w[0] == "v" && w.size() >= 4) {
            mesh->vertices.push_back(QVector3D(w[1].toFloat(), w[2].toFloat(), w[3].toFloat()));
        } else if (w[0] == "vt" && w.size() >= 3) {
            mesh->uvs.push_back(QVector2D(w[1].toFloat(), w[2].toFloat()));
        } else if (w[0] == "mtllib" && w.size() >= 2 && !folder.isEmpty()) {
            readMaterials(QDir(folder).absoluteFilePath(QString::fromUtf8(raw.simplified().mid(7))), mesh->materials);
        } else if (w[0] == "usemtl" && w.size() >= 2) {
            QString const name = QString::fromUtf8(raw.simplified().mid(7));
            material = -1;
            for (int m = 0; m < mesh->materials.size(); ++m)
                if (mesh->materials[m].name == name)
                    material = m;
        } else if (w[0] == "o") {
            // An object: a part of the model.
            QString const name = QString::fromUtf8(raw.simplified().mid(2)).trimmed();
            object = node = name.isEmpty() ? 0 : partNamed(name, 0);
        } else if (w[0] == "g") {
            // A group: a part of the object it is in.
            QString const name = QString::fromUtf8(raw.simplified().mid(2)).trimmed();
            node = name.isEmpty() || name == QLatin1String("default") ? object : partNamed(name, object);
        } else if (w[0] == "f" && w.size() >= 4) {
            QVector<int> vs, ts;
            for (int k = 1; k < w.size(); ++k) {
                QList<QByteArray> const parts = w[k].split('/');
                int v = parts[0].toInt();
                v = v < 0 ? int(mesh->vertices.size()) + v : v - 1;
                int t = parts.size() > 1 && !parts[1].isEmpty() ? parts[1].toInt() : 0;
                t = t < 0 ? int(mesh->uvs.size()) + t : t - 1;
                vs.push_back(v);
                ts.push_back(t);
            }
            for (int k = 1; k + 1 < vs.size(); ++k) {
                Triangle tri;
                int const corner[3] = {0, k, k + 1};
                bool good = true;
                for (int c = 0; c < 3; ++c) {
                    tri.v[c] = vs[corner[c]];
                    tri.t[c] = ts[corner[c]] >= 0 && ts[corner[c]] < mesh->uvs.size() ? ts[corner[c]] : -1;
                    good = good && tri.v[c] >= 0 && tri.v[c] < mesh->vertices.size();
                }
                tri.material = material;
                tri.node = node;
                if (good)
                    mesh->triangles.push_back(tri);
            }
        }
    }
    if (!finish(*mesh, error))
        return nullptr;
    return mesh;
}

QString Model3D::fileFilter()
{
    return QStringLiteral("*.obj *.stl *.ply *.off *.gltf *.glb");
}

bool Model3D::isModelFile(QString const& file)
{
    static QStringList const kinds = {QStringLiteral("obj"), QStringLiteral("stl"), QStringLiteral("ply"), QStringLiteral("off"), QStringLiteral("gltf"),
                                      QStringLiteral("glb")};
    return kinds.contains(QFileInfo(file).suffix().toLower());
}

std::shared_ptr<Model3D::Mesh const> Model3D::load(QString const& file, QString* error)
{
    QFileInfo const info(file);
    {
        QMutexLocker lock(&keptMutex);
        auto const it = kept.constFind(file);
        if (it != kept.constEnd() && it->changed == info.lastModified())
            return it->mesh;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("The model file could not be opened.");
        return nullptr;
    }
    QByteArray const data = f.readAll();
    QString const kind = info.suffix().toLower();
    std::shared_ptr<Mesh const> mesh;
    if (kind == QLatin1String("stl"))
        mesh = parseStl(data, error);
    else if (kind == QLatin1String("ply"))
        mesh = parsePly(data, error);
    else if (kind == QLatin1String("off"))
        mesh = parseOff(data, error);
    else if (kind == QLatin1String("gltf") || kind == QLatin1String("glb"))
        mesh = parseGltf(data, info.path(), error);
    else
        mesh = parseObj(data, info.path(), error);
    if (mesh) {
        QMutexLocker lock(&keptMutex);
        kept.insert(file, {info.lastModified(), mesh});
    }
    return mesh;
}

QImage Model3D::render(Mesh const& mesh, double rotX, double rotY, int pixels, QTransform* placement)
{
    Look look;
    look.rotX = rotX;
    look.rotY = rotY;
    return render(mesh, look, pixels, placement);
}

QImage Model3D::render(Mesh const& mesh, Look const& look, int pixels, QTransform* placement)
{
    pixels = std::clamp(pixels, 8, 4096);
    Shading const& shading = look.shading;
    // The box the picture is: as wide as the model can be, however it is turned.
    double const span = 1.75 * mesh.extent;
    double const scale = pixels / span;
    double const ax = qDegreesToRadians(look.rotX), ay = qDegreesToRadians(look.rotY), az = qDegreesToRadians(look.rotZ);
    double const cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az), sz = std::sin(az);
    // Seen in perspective: from a camera so far off (in the model's units), a nearer point further from the middle.
    double distance = 0.0;
    if (look.focal > 0.0)
        distance = std::max(0.6 * mesh.extent, mesh.extent * (look.focal / 18.0 - look.truck));
    // The parts moved or hidden: a matrix for each part (its own and those it hangs under), and which are left out.
    int const nodes = int(mesh.nodes.size());
    QVector<QMatrix4x4> placed(nodes);
    QVector<char> shown(nodes, 1), ownShown(nodes, 1);
    bool const overridden = !look.overrides.isEmpty();
    if (overridden) {
        QVector<NodeOverride const*> of(nodes, nullptr);
        for (int n = 0; n < nodes; ++n)
            of[n] = look.overrideOf(mesh.nodes[n].name);
        for (int n = 0; n < nodes; ++n) {
            for (int a = n, guard = 0; a >= 0 && a < nodes && guard <= nodes; a = mesh.nodes[a].parent, ++guard) {
                if (!of[a])
                    continue;
                if (of[a]->hidden)
                    shown[n] = 0;
                placed[n] = overrideMatrix(*of[a], mesh.nodes[a].pivot) * placed[n];
            }
            ownShown[n] = of[n] && of[n]->shapeHidden ? 0 : 1;
        }
    }
    auto toView = [&](QVector3D p) {
        p = QVector3D(p.x() * look.stretch.x(), p.y() * look.stretch.y(), p.z() * look.stretch.z());
        double x = p.x(), y = -p.y(), z = p.z();
        double const x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
        double const y2 = y * cx - z1 * sx, z2 = y * sx + z1 * cx;
        double const x3 = x1 * cz - y2 * sz, y3 = x1 * sz + y2 * cz;
        double grow = 1.0;
        if (distance > 0.0)
            grow = distance / std::max(distance - z2, distance * 0.05);
        return QVector3D(float(x3 * grow * scale + pixels / 2.0), float(y3 * grow * scale + pixels / 2.0), float(z2 * scale));
    };
    QVector<QVector3D> at(mesh.vertices.size());
    auto viewOf = [&](QVector3D const& p) { return toView(p - mesh.centre); };
    if (!overridden)
        for (int i = 0; i < mesh.vertices.size(); ++i)
            at[i] = viewOf(mesh.vertices[i]);
    // The corners of each triangle where they are seen.
    QVector<QVector3D> corners(qsizetype(mesh.triangles.size()) * 3);
    for (int i = 0; i < mesh.triangles.size(); ++i) {
        Triangle const& t = mesh.triangles[i];
        for (int c = 0; c < 3; ++c)
            corners[qsizetype(i) * 3 + c] = overridden ? viewOf(placed[t.node].map(mesh.vertices[t.v[c]])) : at[t.v[c]];
    }
    // Generate Normals: each corner's normal the mean of its faces' (shaded smoothly over the corners).
    QVector<QVector3D> normals;
    if (shading.smooth) {
        normals.fill(QVector3D(), mesh.vertices.size());
        for (int i = 0; i < mesh.triangles.size(); ++i) {
            QVector3D const* const c = corners.constData() + qsizetype(i) * 3;
            QVector3D const n = QVector3D::crossProduct(c[1] - c[0], c[2] - c[0]);
            for (int k = 0; k < 3; ++k)
                normals[mesh.triangles[i].v[k]] += n;
        }
        for (QVector3D& n : normals)
            n.normalize();
    }
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QVector<float> depth(qsizetype(pixels) * pixels, -std::numeric_limits<float>::infinity());
    QVector3D const light = shading.direction();
    QVector3D const half = (light + QVector3D(0, 0, 1)).normalized();
    double const lightR = shading.light.redF(), lightG = shading.light.greenF(), lightB = shading.light.blueF();
    bool const plainLight = shading.light == QColor(Qt::white) && shading.intensity == 1.0;
    auto lighting = [&](QVector3D normal, double& diffuse, double& shine) {
        double d = std::abs(double(QVector3D::dotProduct(normal, light))) * shading.intensity;
        if (shading.toonBands >= 2)
            d = std::min(1.0, std::floor(std::min(d, 0.9999) * shading.toonBands) / (shading.toonBands - 1));
        diffuse = shading.ambient + (1.0 - shading.ambient) * d;
        shine = 0.0;
        if (shading.specular > 0.0) {
            // (Two-sided: the face turned to the eye.)
            if (normal.z() < 0)
                normal = -normal;
            shine = shading.specular * std::pow(std::max(0.0, double(QVector3D::dotProduct(normal, half))), shading.shininess);
            if (shading.toonBands >= 2)
                shine = shine > 0.5 * shading.specular ? shading.specular : 0.0;
        }
    };
    for (int i = 0; i < mesh.triangles.size(); ++i) {
        Triangle const& t = mesh.triangles[i];
        if (!shown[t.node] || !ownShown[t.node])
            continue;
        QVector3D const& a = corners[qsizetype(i) * 3];
        QVector3D const& b = corners[qsizetype(i) * 3 + 1];
        QVector3D const& c = corners[qsizetype(i) * 3 + 2];
        double const area = double(b.x() - a.x()) * (c.y() - a.y()) - double(b.y() - a.y()) * (c.x() - a.x());
        if (std::abs(area) < 1e-9)
            continue;
        // Lit from where the light is, on either face.
        QVector3D const normal = QVector3D::crossProduct(b - a, c - a).normalized();
        double flatLit = 0.0, flatShine = 0.0;
        lighting(normal, flatLit, flatShine);
        Material const* const m = t.material >= 0 && t.material < mesh.materials.size() ? &mesh.materials[t.material] : nullptr;
        QColor const flat = t.colour ? QColor::fromRgb(t.colour) : m ? m->colour : QColor(200, 200, 200);
        bool const textured = m && !m->texture.isNull() && t.t[0] >= 0 && t.t[1] >= 0 && t.t[2] >= 0;
        QVector3D const* const cornerNormal = shading.smooth ? normals.constData() : nullptr;
        int const x0 = std::max(0, int(std::floor(std::min({a.x(), b.x(), c.x()})))), x1 = std::min(pixels - 1, int(std::ceil(std::max({a.x(), b.x(), c.x()}))));
        int const y0 = std::max(0, int(std::floor(std::min({a.y(), b.y(), c.y()})))), y1 = std::min(pixels - 1, int(std::ceil(std::max({a.y(), b.y(), c.y()}))));
        for (int y = y0; y <= y1; ++y) {
            QRgb* const line = reinterpret_cast<QRgb*>(image.scanLine(y));
            float* const near = depth.data() + qsizetype(y) * pixels;
            double const py = y + 0.5;
            for (int x = x0; x <= x1; ++x) {
                double const px = x + 0.5;
                double const wa = ((b.x() - px) * (c.y() - py) - (b.y() - py) * (c.x() - px)) / area;
                double const wb = ((c.x() - px) * (a.y() - py) - (c.y() - py) * (a.x() - px)) / area;
                double const wc = 1.0 - wa - wb;
                if (wa < -1e-6 || wb < -1e-6 || wc < -1e-6)
                    continue;
                float const z = float(wa * a.z() + wb * b.z() + wc * c.z());
                if (z <= near[x])
                    continue;
                near[x] = z;
                QColor colour = flat;
                if (textured) {
                    QVector2D const uv = float(wa) * mesh.uvs[t.t[0]] + float(wb) * mesh.uvs[t.t[1]] + float(wc) * mesh.uvs[t.t[2]];
                    QImage const& tex = m->texture;
                    double u = uv.x() - std::floor(uv.x()), v = uv.y() - std::floor(uv.y());
                    int const tx = std::clamp(int(u * tex.width()), 0, tex.width() - 1);
                    int const ty = std::clamp(int((1.0 - v) * tex.height()), 0, tex.height() - 1);
                    colour = QColor::fromRgb(tex.pixel(tx, ty));
                }
                double lit = flatLit, shine = flatShine;
                if (cornerNormal) {
                    QVector3D const n = (float(wa) * cornerNormal[t.v[0]] + float(wb) * cornerNormal[t.v[1]] + float(wc) * cornerNormal[t.v[2]]).normalized();
                    lighting(n, lit, shine);
                }
                int r, g, bl;
                if (plainLight && shine == 0.0) {
                    auto shade = [lit](int v) { return std::clamp(int(std::lround(v * lit)), 0, 255); };
                    r = shade(colour.red());
                    g = shade(colour.green());
                    bl = shade(colour.blue());
                } else {
                    // The light's colour on what it lights; the shine in the light's colour, over it.
                    auto shade = [&](int v, double tone) {
                        double const ambientPart = shading.ambient, litPart = lit - ambientPart;
                        return std::clamp(int(std::lround(v * (ambientPart + litPart * tone) + 255.0 * shine * tone)), 0, 255);
                    };
                    r = shade(colour.red(), lightR);
                    g = shade(colour.green(), lightG);
                    bl = shade(colour.blue(), lightB);
                }
                if (shading.grey)
                    r = g = bl = std::clamp(int(std::lround(0.299 * r + 0.587 * g + 0.114 * bl)), 0, 255);
                line[x] = qRgb(r, g, bl);
            }
        }
    }
    if (shading.lineRadius > 0.0) {
        // The outline: round what is drawn, and where a nearer part lies over a further one (a step in depth).
        double const step = 0.04 * mesh.extent * scale;
        QVector<char> edge(qsizetype(pixels) * pixels, 0);
        auto covered = [&](int x, int y) { return depth[qsizetype(y) * pixels + x] > -std::numeric_limits<float>::infinity(); };
        for (int y = 0; y < pixels; ++y)
            for (int x = 0; x < pixels; ++x) {
                if (!covered(x, y))
                    continue;
                float const z = depth[qsizetype(y) * pixels + x];
                static int const dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    int const nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= pixels || ny >= pixels || !covered(nx, ny) || z - depth[qsizetype(ny) * pixels + nx] > step) {
                        edge[qsizetype(y) * pixels + x] = 1;
                        break;
                    }
                }
            }
        int const reach = int(std::ceil(shading.lineRadius));
        double const r2 = shading.lineRadius * shading.lineRadius;
        QRgb const ink = qRgb(shading.lineColour.red(), shading.lineColour.green(), shading.lineColour.blue());
        QImage lined = image;
        for (int y = 0; y < pixels; ++y)
            for (int x = 0; x < pixels; ++x) {
                if (!edge[qsizetype(y) * pixels + x])
                    continue;
                for (int oy = -reach + 1; oy < reach; ++oy)
                    for (int ox = -reach + 1; ox < reach; ++ox) {
                        int const px = x + ox, py = y + oy;
                        if (px < 0 || py < 0 || px >= pixels || py >= pixels || double(ox) * ox + double(oy) * oy > r2)
                            continue;
                        // (Outside the model the line goes no further than half its width.)
                        if (!covered(px, py) && double(ox) * ox + double(oy) * oy > r2 / 4.0)
                            continue;
                        reinterpret_cast<QRgb*>(lined.scanLine(py))[px] = ink;
                    }
            }
        image = lined;
    }
    if (placement) {
        // A pixel is so many scene units: the model kSize at its biggest.
        double const unit = span / pixels * (kSize / mesh.extent);
        *placement = QTransform(unit, 0, 0, unit, -pixels / 2.0 * unit, -pixels / 2.0 * unit);
    }
    return image;
}

bool Model3D::picture(QString const& file, double rotX, double rotY, Picture& out)
{
    Look look;
    look.rotX = rotX;
    look.rotY = rotY;
    return picture(file, look, out);
}

bool Model3D::picture(QString const& file, Look const& look, Picture& out)
{
    std::shared_ptr<Mesh const> const mesh = load(file);
    if (!mesh)
        return false;
    {
        QMutexLocker lock(&madeMutex);
        for (Made const& m : made)
            if (m.mesh == mesh && m.look == look) {
                out = Picture();
                out.image = m.image;
                out.size = m.image.size();
                out.placement = m.placement;
                return true;
            }
    }
    Made m;
    m.mesh = mesh;
    m.look = look;
    m.image = render(*mesh, look, 1024, &m.placement);
    {
        QMutexLocker lock(&madeMutex);
        made.push_back(m);
        if (made.size() > 48)
            made.removeFirst();
    }
    out = Picture();
    out.image = m.image;
    out.size = m.image.size();
    out.placement = m.placement;
    return true;
}

void Model3D::forget()
{
    {
        QMutexLocker lock(&keptMutex);
        kept.clear();
    }
    QMutexLocker lock(&madeMutex);
    made.clear();
}
