// Eclipse - the 3D model files read besides OBJ: STL (text or binary), PLY (text or binary), OFF, and glTF 2.0
// (.gltf with its buffers, or .glb).
#include "model/geometry/Model3D.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using Model3D::Mesh;
using Model3D::Node;
using Model3D::Triangle;

void fail(QString* error, QString const& why)
{
    if (error)
        *error = why;
}

// A fan of triangles over a polygon's corners (as faces with more than three corners are made).
void addFan(Mesh& mesh, QVector<int> const& corners, int node, QRgb colour = 0, int material = -1)
{
    for (int k = 1; k + 1 < corners.size(); ++k) {
        Triangle t;
        t.v[0] = corners[0];
        t.v[1] = corners[k];
        t.v[2] = corners[k + 1];
        t.node = node;
        t.colour = colour;
        t.material = material;
        bool good = true;
        for (int v : t.v)
            good = good && v >= 0 && v < mesh.vertices.size();
        if (good)
            mesh.triangles.push_back(t);
    }
}

QRgb averageColour(QVector<QRgb> const& colours, QVector<int> const& corners)
{
    if (colours.isEmpty())
        return 0;
    int r = 0, g = 0, b = 0, n = 0;
    for (int c : corners)
        if (c >= 0 && c < colours.size()) {
            r += qRed(colours[c]);
            g += qGreen(colours[c]);
            b += qBlue(colours[c]);
            ++n;
        }
    return n ? qRgb(r / n, g / n, b / n) : 0;
}

// ---- STL ----

std::shared_ptr<Mesh const> stlBinary(QByteArray const& data, QString* error)
{
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("STL");
    quint32 const count = qFromLittleEndian<quint32>(data.constData() + 80);
    char const* p = data.constData() + 84;
    for (quint32 i = 0; i < count; ++i, p += 50) {
        QVector<int> corners;
        for (int c = 0; c < 3; ++c) {
            float xyz[3];
            for (int k = 0; k < 3; ++k) {
                quint32 const bits = qFromLittleEndian<quint32>(p + 12 + c * 12 + k * 4);
                std::memcpy(&xyz[k], &bits, 4);
            }
            mesh->vertices.push_back(QVector3D(xyz[0], xyz[1], xyz[2]));
            corners.push_back(int(mesh->vertices.size()) - 1);
        }
        addFan(*mesh, corners, 0);
    }
    if (!Model3D::finish(*mesh, error))
        return nullptr;
    return mesh;
}

}

std::shared_ptr<Model3D::Mesh const> Model3D::parseStl(QByteArray const& data, QString* error)
{
    // Binary: an 80-byte header, a count, and 50 bytes a triangle.  (A binary file may begin with "solid" too: the size says.)
    if (data.size() >= 84) {
        quint32 const count = qFromLittleEndian<quint32>(data.constData() + 80);
        if (qint64(84) + qint64(count) * 50 == data.size())
            return stlBinary(data, error);
    }
    if (!data.trimmed().startsWith("solid")) {
        fail(error, QObject::tr("This is not an STL file."));
        return nullptr;
    }
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("STL");
    mesh->nodes.push_back(Node{QObject::tr("Model"), -1, {}, 0});
    int node = 0;
    QVector<int> corners;
    for (QByteArray const& raw : data.split('\n')) {
        QList<QByteArray> const w = raw.simplified().split(' ');
        if (w.isEmpty())
            continue;
        if (w[0] == "solid") {
            // Each solid named: a part of its own.
            QString const name = QString::fromUtf8(raw.simplified().mid(6)).trimmed();
            if (!name.isEmpty()) {
                mesh->nodes.push_back(Node{name, 0, {}, 0});
                node = int(mesh->nodes.size()) - 1;
            }
        } else if (w[0] == "outer") {
            corners.clear();
        } else if (w[0] == "vertex" && w.size() >= 4) {
            mesh->vertices.push_back(QVector3D(w[1].toFloat(), w[2].toFloat(), w[3].toFloat()));
            corners.push_back(int(mesh->vertices.size()) - 1);
        } else if (w[0] == "endloop") {
            addFan(*mesh, corners, node);
            corners.clear();
        } else if (w[0] == "endsolid") {
            node = 0;
        }
    }
    if (!finish(*mesh, error))
        return nullptr;
    return mesh;
}

// ---- PLY ----

namespace {

struct PlyProperty {
    QByteArray name;
    QByteArray type;            // the type of the value (of each value, for a list)
    QByteArray countType;       // a list: the type of its count; empty: one value
};

struct PlyElement {
    QByteArray name;
    qint64 count = 0;
    QVector<PlyProperty> properties;
};

int plySize(QByteArray const& type)
{
    if (type == "char" || type == "uchar" || type == "int8" || type == "uint8")
        return 1;
    if (type == "short" || type == "ushort" || type == "int16" || type == "uint16")
        return 2;
    if (type == "int" || type == "uint" || type == "int32" || type == "uint32" || type == "float" || type == "float32")
        return 4;
    if (type == "double" || type == "float64")
        return 8;
    return 0;
}

// Reads the values of a PLY file one after another, as text or binary.
struct PlyReader {
    QByteArray const& data;
    qsizetype at;
    int format;                 // 0 text, 1 binary little-endian, 2 binary big-endian
    QList<QByteArray> words;
    int word = 0;
    bool bad = false;

    double next(QByteArray const& type)
    {
        if (format == 0) {
            while (word >= words.size()) {
                if (at >= data.size()) {
                    bad = true;
                    return 0.0;
                }
                qsizetype end = data.indexOf('\n', at);
                if (end < 0)
                    end = data.size();
                words = data.mid(at, end - at).simplified().split(' ');
                if (words.size() == 1 && words[0].isEmpty())
                    words.clear();
                word = 0;
                at = end + 1;
            }
            return words[word++].toDouble();
        }
        int const size = plySize(type);
        if (size == 0 || at + size > data.size()) {
            bad = true;
            return 0.0;
        }
        uchar bytes[8];
        std::memcpy(bytes, data.constData() + at, size_t(size));
        at += size;
        if ((format == 2) != (QSysInfo::ByteOrder == QSysInfo::BigEndian))
            std::reverse(bytes, bytes + size);
        if (type == "char" || type == "int8")
            return double(qint8(bytes[0]));
        if (type == "uchar" || type == "uint8")
            return double(bytes[0]);
        if (type == "short" || type == "int16") {
            qint16 v;
            std::memcpy(&v, bytes, 2);
            return v;
        }
        if (type == "ushort" || type == "uint16") {
            quint16 v;
            std::memcpy(&v, bytes, 2);
            return v;
        }
        if (type == "int" || type == "int32") {
            qint32 v;
            std::memcpy(&v, bytes, 4);
            return v;
        }
        if (type == "uint" || type == "uint32") {
            quint32 v;
            std::memcpy(&v, bytes, 4);
            return v;
        }
        if (type == "float" || type == "float32") {
            float v;
            std::memcpy(&v, bytes, 4);
            return v;
        }
        double v;
        std::memcpy(&v, bytes, 8);
        return v;
    }
};

}

std::shared_ptr<Model3D::Mesh const> Model3D::parsePly(QByteArray const& data, QString* error)
{
    if (!data.startsWith("ply")) {
        fail(error, QObject::tr("This is not a PLY file."));
        return nullptr;
    }
    qsizetype const headerEnd = data.indexOf("end_header");
    if (headerEnd < 0) {
        fail(error, QObject::tr("The PLY file has no end to its header."));
        return nullptr;
    }
    int format = -1;
    QVector<PlyElement> elements;
    for (QByteArray const& raw : data.left(headerEnd).split('\n')) {
        QList<QByteArray> const w = raw.simplified().split(' ');
        if (w.size() >= 2 && w[0] == "format")
            format = w[1] == "ascii" ? 0 : w[1] == "binary_little_endian" ? 1 : w[1] == "binary_big_endian" ? 2 : -1;
        else if (w.size() >= 3 && w[0] == "element")
            elements.push_back(PlyElement{w[1], w[2].toLongLong(), {}});
        else if (w.size() >= 3 && w[0] == "property" && !elements.isEmpty()) {
            if (w[1] == "list" && w.size() >= 5)
                elements.back().properties.push_back(PlyProperty{w[4], w[3], w[2]});
            else
                elements.back().properties.push_back(PlyProperty{w[2], w[1], {}});
        }
    }
    if (format < 0) {
        fail(error, QObject::tr("The PLY file is in a format that is not read."));
        return nullptr;
    }
    qsizetype body = data.indexOf('\n', headerEnd);
    body = body < 0 ? data.size() : body + 1;
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("PLY");
    QVector<QRgb> colours;
    PlyReader read{data, body, format, {}, 0, false};
    for (PlyElement const& element : elements) {
        bool const vertex = element.name == "vertex", face = element.name == "face";
        for (qint64 i = 0; i < element.count && !read.bad; ++i) {
            double x = 0, y = 0, z = 0;
            int r = -1, g = -1, b = -1;
            QVector<int> corners;
            for (PlyProperty const& p : element.properties) {
                if (!p.countType.isEmpty()) {
                    int const n = int(read.next(p.countType));
                    for (int k = 0; k < n && !read.bad; ++k) {
                        double const v = read.next(p.type);
                        if (face && (p.name == "vertex_indices" || p.name == "vertex_index"))
                            corners.push_back(int(v));
                    }
                    continue;
                }
                double const v = read.next(p.type);
                if (p.name == "x")
                    x = v;
                else if (p.name == "y")
                    y = v;
                else if (p.name == "z")
                    z = v;
                else if (p.name == "red" || p.name == "diffuse_red")
                    r = p.type.startsWith("float") || p.type == "double" ? int(v * 255) : int(v);
                else if (p.name == "green" || p.name == "diffuse_green")
                    g = p.type.startsWith("float") || p.type == "double" ? int(v * 255) : int(v);
                else if (p.name == "blue" || p.name == "diffuse_blue")
                    b = p.type.startsWith("float") || p.type == "double" ? int(v * 255) : int(v);
            }
            if (vertex) {
                mesh->vertices.push_back(QVector3D(float(x), float(y), float(z)));
                if (r >= 0 && g >= 0 && b >= 0) {
                    colours.resize(mesh->vertices.size(), qRgb(200, 200, 200));
                    colours.back() = qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
                }
            } else if (face) {
                addFan(*mesh, corners, 0, averageColour(colours, corners));
            }
        }
    }
    if (read.bad && mesh->triangles.isEmpty()) {
        fail(error, QObject::tr("The PLY file ends before all it says is in it."));
        return nullptr;
    }
    if (!finish(*mesh, error))
        return nullptr;
    return mesh;
}

// ---- OFF ----

std::shared_ptr<Model3D::Mesh const> Model3D::parseOff(QByteArray const& data, QString* error)
{
    // The numbers, with the comments taken out.
    QList<QByteArray> words;
    for (QByteArray line : data.split('\n')) {
        if (qsizetype const hash = line.indexOf('#'); hash >= 0)
            line.truncate(hash);
        for (QByteArray const& w : line.simplified().split(' '))
            if (!w.isEmpty())
                words.push_back(w);
    }
    if (words.isEmpty() || !words[0].endsWith("OFF")) {
        fail(error, QObject::tr("This is not an OFF file."));
        return nullptr;
    }
    bool const coloured = words[0].startsWith("C");
    int at = 1;
    auto number = [&]() { return at < words.size() ? words[at++].toDouble() : 0.0; };
    int const vertices = int(number()), faces = int(number());
    number();   // (the edges)
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("OFF");
    QVector<QRgb> colours;
    for (int i = 0; i < vertices && at < words.size(); ++i) {
        double const x = number(), y = number(), z = number();
        mesh->vertices.push_back(QVector3D(float(x), float(y), float(z)));
        if (coloured) {
            double const r = number(), g = number(), b = number(), a = number();
            (void)a;
            bool const unit = r <= 1.0 && g <= 1.0 && b <= 1.0;
            auto one = [unit](double v) { return std::clamp(int(std::lround(unit ? v * 255.0 : v)), 0, 255); };
            colours.push_back(qRgb(one(r), one(g), one(b)));
        }
    }
    for (int i = 0; i < faces && at < words.size(); ++i) {
        int const n = int(number());
        QVector<int> corners;
        for (int k = 0; k < n; ++k)
            corners.push_back(int(number()));
        // (A face may end with a colour of its own: three or four numbers more on its line, which are not read here.)
        addFan(*mesh, corners, 0, averageColour(colours, corners));
    }
    if (!finish(*mesh, error))
        return nullptr;
    return mesh;
}

// ---- glTF 2.0 ----

namespace {

struct Gltf {
    QJsonObject root;
    QVector<QByteArray> buffers;
    QString folder;

    QByteArray uriData(QString const& uri) const
    {
        if (uri.startsWith(QLatin1String("data:"))) {
            qsizetype const comma = uri.indexOf(QLatin1Char(','));
            return comma < 0 ? QByteArray() : QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
        }
        QFile f(QDir(folder).absoluteFilePath(QUrl::fromPercentEncoding(uri.toUtf8())));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QByteArray view(int index) const
    {
        QJsonObject const v = root["bufferViews"].toArray()[index].toObject();
        int const buffer = v["buffer"].toInt();
        if (buffer < 0 || buffer >= buffers.size())
            return {};
        return buffers[buffer].mid(v["byteOffset"].toInteger(), v["byteLength"].toInteger());
    }

    // The values of an accessor: so many components each (floats, normalised where the file says so).
    QVector<float> accessor(int index, int* components) const
    {
        QJsonObject const a = root["accessors"].toArray()[index].toObject();
        static QHash<QString, int> const sizes = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}, {"MAT4", 16}};
        int const n = sizes.value(a["type"].toString(), 1);
        *components = n;
        qint64 const count = a["count"].toInteger();
        int const kind = a["componentType"].toInt();
        int const size = kind == 5126 || kind == 5125 ? 4 : kind == 5122 || kind == 5123 ? 2 : 1;
        bool const normalised = a["normalized"].toBool();
        QVector<float> out;
        if (!a.contains("bufferView")) {
            out.fill(0.0f, count * n);
            return out;
        }
        QJsonObject const v = root["bufferViews"].toArray()[a["bufferView"].toInt()].toObject();
        QByteArray const bytes = view(a["bufferView"].toInt());
        qint64 const stride = v["byteStride"].toInteger(n * size);
        qint64 const offset = a["byteOffset"].toInteger();
        out.reserve(count * n);
        for (qint64 i = 0; i < count; ++i)
            for (int c = 0; c < n; ++c) {
                qint64 const at = offset + i * stride + c * size;
                if (at + size > bytes.size())
                    return out;
                char const* const p = bytes.constData() + at;
                float value = 0.0f;
                switch (kind) {
                case 5126: {
                    quint32 const bits = qFromLittleEndian<quint32>(p);
                    std::memcpy(&value, &bits, 4);
                    break;
                }
                case 5125: value = float(qFromLittleEndian<quint32>(p)); break;
                case 5123: value = float(qFromLittleEndian<quint16>(p)); if (normalised) value /= 65535.0f; break;
                case 5122: value = float(qFromLittleEndian<qint16>(p)); if (normalised) value = std::max(value / 32767.0f, -1.0f); break;
                case 5121: value = float(quint8(*p)); if (normalised) value /= 255.0f; break;
                case 5120: value = float(qint8(*p)); if (normalised) value = std::max(value / 127.0f, -1.0f); break;
                default: break;
                }
                out.push_back(value);
            }
        return out;
    }

    QMatrix4x4 local(QJsonObject const& node) const
    {
        QMatrix4x4 m;
        if (QJsonArray const matrix = node["matrix"].toArray(); matrix.size() == 16) {
            float values[16];
            for (int i = 0; i < 16; ++i)
                values[i] = float(matrix[i].toDouble());
            // (Column by column in the file.)
            return QMatrix4x4(values).transposed();
        }
        if (QJsonArray const t = node["translation"].toArray(); t.size() == 3)
            m.translate(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
        if (QJsonArray const r = node["rotation"].toArray(); r.size() == 4)
            m.rotate(QQuaternion(float(r[3].toDouble()), float(r[0].toDouble()), float(r[1].toDouble()), float(r[2].toDouble())));
        if (QJsonArray const s = node["scale"].toArray(); s.size() == 3)
            m.scale(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
        return m;
    }

    QImage image(int index) const
    {
        QJsonObject const im = root["images"].toArray()[index].toObject();
        QByteArray const bytes = im.contains("bufferView") ? view(im["bufferView"].toInt()) : uriData(im["uri"].toString());
        QImage picture;
        picture.loadFromData(bytes);
        return picture.isNull() ? picture : picture.convertToFormat(QImage::Format_RGB32);
    }
};

void addGltfNode(Gltf const& g, Mesh& mesh, int index, int parent, QMatrix4x4 const& above, int depth, QVector<int>& materialOf)
{
    QJsonArray const nodes = g.root["nodes"].toArray();
    if (index < 0 || index >= nodes.size() || depth > 64)
        return;
    QJsonObject const node = nodes[index].toObject();
    QMatrix4x4 const world = above * g.local(node);
    QString name = node["name"].toString();
    if (name.isEmpty())
        name = QObject::tr("Node %1").arg(index);
    mesh.nodes.push_back(Node{name, parent, {}, 0});
    int const part = int(mesh.nodes.size()) - 1;
    if (node.contains("mesh")) {
        QJsonObject const m = g.root["meshes"].toArray()[node["mesh"].toInt()].toObject();
        for (QJsonValue const& pv : m["primitives"].toArray()) {
            QJsonObject const primitive = pv.toObject();
            int const mode = primitive["mode"].toInt(4);
            QJsonObject const attributes = primitive["attributes"].toObject();
            if (!attributes.contains("POSITION") || (mode != 4 && mode != 5 && mode != 6))
                continue;
            int n = 0;
            QVector<float> const positions = g.accessor(attributes["POSITION"].toInt(), &n);
            if (n != 3)
                continue;
            int const first = int(mesh.vertices.size()), firstUv = int(mesh.uvs.size());
            for (qsizetype i = 0; i + 2 < positions.size(); i += 3)
                mesh.vertices.push_back(world.map(QVector3D(positions[i], positions[i + 1], positions[i + 2])));
            int const count = int(mesh.vertices.size()) - first;
            bool uvs = false;
            if (attributes.contains("TEXCOORD_0")) {
                int un = 0;
                QVector<float> const coords = g.accessor(attributes["TEXCOORD_0"].toInt(), &un);
                if (un == 2 && coords.size() >= qsizetype(count) * 2) {
                    // (glTF counts down from the top of the picture; OBJ, as read here, up from the bottom.)
                    for (int i = 0; i < count; ++i)
                        mesh.uvs.push_back(QVector2D(coords[i * 2], 1.0f - coords[i * 2 + 1]));
                    uvs = true;
                }
            }
            QVector<QRgb> colours;
            if (attributes.contains("COLOR_0")) {
                int cn = 0;
                QVector<float> const c = g.accessor(attributes["COLOR_0"].toInt(), &cn);
                if (cn >= 3)
                    for (int i = 0; i < count && qsizetype(i) * cn + 2 < c.size(); ++i)
                        colours.push_back(qRgb(std::clamp(int(c[i * cn] * 255), 0, 255), std::clamp(int(c[i * cn + 1] * 255), 0, 255),
                                               std::clamp(int(c[i * cn + 2] * 255), 0, 255)));
            }
            QVector<int> order;
            if (primitive.contains("indices")) {
                int in = 0;
                for (float v : g.accessor(primitive["indices"].toInt(), &in))
                    order.push_back(int(v));
            } else {
                for (int i = 0; i < count; ++i)
                    order.push_back(i);
            }
            int const material = primitive.contains("material") ? materialOf.value(primitive["material"].toInt(), -1) : -1;
            auto add = [&](int a, int b, int c) {
                if (a < 0 || b < 0 || c < 0 || a >= count || b >= count || c >= count)
                    return;
                Triangle t;
                t.v[0] = first + a;
                t.v[1] = first + b;
                t.v[2] = first + c;
                if (uvs)
                    for (int k = 0; k < 3; ++k)
                        t.t[k] = firstUv + (k == 0 ? a : k == 1 ? b : c);
                t.material = material;
                t.node = part;
                if (!colours.isEmpty())
                    t.colour = averageColour(colours, {a, b, c});
                mesh.triangles.push_back(t);
            };
            if (mode == 4)
                for (qsizetype i = 0; i + 2 < order.size(); i += 3)
                    add(order[i], order[i + 1], order[i + 2]);
            else if (mode == 5)
                for (qsizetype i = 0; i + 2 < order.size(); ++i)
                    i % 2 ? add(order[i + 1], order[i], order[i + 2]) : add(order[i], order[i + 1], order[i + 2]);
            else
                for (qsizetype i = 1; i + 1 < order.size(); ++i)
                    add(order[0], order[i], order[i + 1]);
        }
    }
    for (QJsonValue const& child : node["children"].toArray())
        addGltfNode(g, mesh, child.toInt(), part, world, depth + 1, materialOf);
}

}

std::shared_ptr<Model3D::Mesh const> Model3D::parseGltf(QByteArray const& data, QString const& folder, QString* error)
{
    Gltf g;
    g.folder = folder;
    QByteArray json = data;
    QByteArray binary;
    if (data.startsWith("glTF")) {
        // GLB: a header, then the JSON chunk and (perhaps) a binary chunk.
        if (data.size() < 20 || qFromLittleEndian<quint32>(data.constData() + 4) != 2) {
            fail(error, QObject::tr("Only glTF 2.0 files are read."));
            return nullptr;
        }
        qsizetype at = 12;
        json.clear();
        while (at + 8 <= data.size()) {
            quint32 const length = qFromLittleEndian<quint32>(data.constData() + at);
            quint32 const kind = qFromLittleEndian<quint32>(data.constData() + at + 4);
            QByteArray const chunk = data.mid(at + 8, length);
            if (kind == 0x4E4F534A)
                json = chunk;
            else if (kind == 0x004E4942)
                binary = chunk;
            at += 8 + qsizetype(length);
        }
    }
    QJsonParseError parsed;
    g.root = QJsonDocument::fromJson(json, &parsed).object();
    if (parsed.error != QJsonParseError::NoError || g.root.isEmpty()) {
        fail(error, QObject::tr("The glTF file could not be read: %1").arg(parsed.errorString()));
        return nullptr;
    }
    if (!g.root["asset"].toObject()["version"].toString().startsWith(QLatin1Char('2'))) {
        fail(error, QObject::tr("Only glTF 2.0 files are read."));
        return nullptr;
    }
    QJsonArray const buffers = g.root["buffers"].toArray();
    for (int i = 0; i < buffers.size(); ++i) {
        QJsonObject const b = buffers[i].toObject();
        g.buffers.push_back(b.contains("uri") ? g.uriData(b["uri"].toString()) : binary);
    }
    auto mesh = std::make_shared<Mesh>();
    mesh->format = QStringLiteral("glTF");
    // The materials: their base colours, and the pictures they are painted with.
    QVector<int> materialOf;
    QJsonArray const materials = g.root["materials"].toArray();
    for (int i = 0; i < materials.size(); ++i) {
        QJsonObject const m = materials[i].toObject();
        QJsonObject const pbr = m["pbrMetallicRoughness"].toObject();
        Material material;
        material.name = m["name"].toString(QObject::tr("Material %1").arg(i));
        if (QJsonArray const c = pbr["baseColorFactor"].toArray(); c.size() >= 3)
            material.colour = QColor::fromRgbF(float(std::clamp(c[0].toDouble(), 0.0, 1.0)), float(std::clamp(c[1].toDouble(), 0.0, 1.0)),
                                               float(std::clamp(c[2].toDouble(), 0.0, 1.0)));
        if (pbr.contains("baseColorTexture")) {
            int const texture = pbr["baseColorTexture"].toObject()["index"].toInt();
            QJsonObject const t = g.root["textures"].toArray()[texture].toObject();
            if (t.contains("source"))
                material.texture = g.image(t["source"].toInt());
        }
        mesh->materials.push_back(material);
        materialOf.push_back(i);
    }
    mesh->nodes.push_back(Node{QObject::tr("Model"), -1, {}, 0});
    QJsonArray const scenes = g.root["scenes"].toArray();
    QJsonArray roots;
    if (!scenes.isEmpty())
        roots = scenes[std::clamp(g.root["scene"].toInt(0), 0, int(scenes.size()) - 1)].toObject()["nodes"].toArray();
    else
        for (int i = 0; i < g.root["nodes"].toArray().size(); ++i)
            roots.append(i);
    for (QJsonValue const& r : roots)
        addGltfNode(g, *mesh, r.toInt(), 0, QMatrix4x4(), 0, materialOf);
    if (!finish(*mesh, error))
        return nullptr;
    return mesh;
}
