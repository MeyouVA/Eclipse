#include "model/geometry/Model3D.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
    double rotX = 0.0, rotY = 0.0;
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

}

std::shared_ptr<Model3D::Mesh const> Model3D::parseObj(QByteArray const& text, QString const& folder, QString* error)
{
    auto mesh = std::make_shared<Mesh>();
    int material = -1;
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
                if (good)
                    mesh->triangles.push_back(tri);
            }
        }
    }
    if (mesh->triangles.isEmpty()) {
        if (error)
            *error = QObject::tr("There are no faces in the model.");
        return nullptr;
    }
    QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D hi = -lo;
    for (Triangle const& t : mesh->triangles)
        for (int v : t.v) {
            QVector3D const& p = mesh->vertices[v];
            lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
            hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
        }
    mesh->centre = (lo + hi) / 2.0f;
    QVector3D const size = hi - lo;
    mesh->extent = std::max({double(size.x()), double(size.y()), double(size.z()), 1e-6});
    return mesh;
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
    std::shared_ptr<Mesh const> mesh = parseObj(f.readAll(), info.path(), error);
    if (mesh) {
        QMutexLocker lock(&keptMutex);
        kept.insert(file, {info.lastModified(), mesh});
    }
    return mesh;
}

QImage Model3D::render(Mesh const& mesh, double rotX, double rotY, int pixels, QTransform* placement)
{
    pixels = std::clamp(pixels, 8, 4096);
    // The box the picture is: as wide as the model can be, however it is turned.
    double const span = 1.75 * mesh.extent;
    double const scale = pixels / span;
    double const ax = qDegreesToRadians(rotX), ay = qDegreesToRadians(rotY);
    double const cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    QVector<QVector3D> at(mesh.vertices.size());
    for (int i = 0; i < mesh.vertices.size(); ++i) {
        QVector3D const p = mesh.vertices[i] - mesh.centre;
        double x = p.x(), y = -p.y(), z = p.z();
        double const x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
        double const y2 = y * cx - z1 * sx, z2 = y * sx + z1 * cx;
        at[i] = QVector3D(float(x1 * scale + pixels / 2.0), float(y2 * scale + pixels / 2.0), float(z2 * scale));
    }
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QVector<float> depth(qsizetype(pixels) * pixels, -std::numeric_limits<float>::infinity());
    QVector3D const light = QVector3D(-0.35f, -0.5f, 1.0f).normalized();
    for (Triangle const& t : mesh.triangles) {
        QVector3D const& a = at[t.v[0]];
        QVector3D const& b = at[t.v[1]];
        QVector3D const& c = at[t.v[2]];
        double const area = double(b.x() - a.x()) * (c.y() - a.y()) - double(b.y() - a.y()) * (c.x() - a.x());
        if (std::abs(area) < 1e-9)
            continue;
        // Lit from the front, above and to the left, on either face.
        QVector3D const normal = QVector3D::crossProduct(b - a, c - a).normalized();
        double const lit = 0.32 + 0.68 * std::abs(double(QVector3D::dotProduct(normal, light)));
        Material const* const m = t.material >= 0 && t.material < mesh.materials.size() ? &mesh.materials[t.material] : nullptr;
        QColor const flat = m ? m->colour : QColor(200, 200, 200);
        bool const textured = m && !m->texture.isNull() && t.t[0] >= 0 && t.t[1] >= 0 && t.t[2] >= 0;
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
                auto shade = [lit](int v) { return std::clamp(int(std::lround(v * lit)), 0, 255); };
                line[x] = qRgb(shade(colour.red()), shade(colour.green()), shade(colour.blue()));
            }
        }
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
    std::shared_ptr<Mesh const> const mesh = load(file);
    if (!mesh)
        return false;
    {
        QMutexLocker lock(&madeMutex);
        for (Made const& m : made)
            if (m.mesh == mesh && m.rotX == rotX && m.rotY == rotY) {
                out = Picture();
                out.image = m.image;
                out.size = m.image.size();
                out.placement = m.placement;
                return true;
            }
    }
    Made m;
    m.mesh = mesh;
    m.rotX = rotX;
    m.rotY = rotY;
    m.image = render(*mesh, rotX, rotY, 1024, &m.placement);
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
