// Eclipse - 3D models brought into a scene (3D model elements): a model file read (Wavefront OBJ with its MTL
// materials, STL, PLY, OFF, or glTF 2.0 / GLB), and pictures made of it, turned in space, for a layer to show.
//
// A drawing of a layer can be a model (Drawing::model, the file): what it shows is a picture of the model, made for the
// angles the layer is turned to in space on that frame (its pose's rotX and rotY) and the drawing's own look of it
// (Drawing::modelLook: its own turn, the camera, the light, a toon shading, the parts hidden or moved).  The model is
// centred on the drawing's middle and sized to kSize scene units at its biggest.
//
// A model is made of parts (subnodes): the objects and groups of an OBJ file, the nodes of a glTF file.  Part 0 is
// the model itself; the others hang under it, or under one another.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QTransform>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include <memory>

struct Picture;

namespace Model3D {

// How big a model is in the scene, at its biggest (scene units).
inline constexpr double kSize = 400.0;

struct Material {
    QString name;
    QColor colour = QColor(200, 200, 200);
    QImage texture;     // none: the colour alone
};

struct Triangle {
    int v[3] = {0, 0, 0};
    int t[3] = {-1, -1, -1};    // the texture coordinates of its corners (-1: none)
    int material = -1;
    int node = 0;               // the part it belongs to
    QRgb colour = 0;            // a colour of its own (from the file's vertex colours); 0: none
};

// A part of the model (a subnode).
struct Node {
    QString name;
    int parent = -1;            // -1: the model itself (part 0)
    QVector3D pivot;            // the middle of it and all under it, where it turns and scales about
    int triangles = 0;          // how many triangles are its own
};

struct Mesh {
    QVector<QVector3D> vertices;
    QVector<QVector2D> uvs;
    QVector<Triangle> triangles;
    QVector<Material> materials;
    QVector<Node> nodes;
    QString format;             // what the file was ("OBJ", "STL", "PLY", "OFF", "glTF")
    // Its middle and how big it is, at its biggest (across, up or deep).
    QVector3D centre;
    double extent = 1.0;
    // The parts directly under a part, in order.
    QVector<int> children(int node) const;
    // Whether a part is `ancestor` or hangs (somewhere) under it.
    bool under(int node, int ancestor) const;
    int nodeNamed(QString const& name) const;
};

// A part of the model hidden or moved (Subnode Override), by its name.
struct NodeOverride {
    QString name;
    bool hidden = false;        // Disable Subnode: it and all under it not shown
    bool shapeHidden = false;   // Hide Subnode Shape: its own triangles not shown (what hangs under it still is)
    bool available = false;     // Make Properties Available: it has transformation values of its own
    QVector3D move;             // in the file's units
    QVector3D turn;             // degrees about X, Y and Z
    QVector3D scale = QVector3D(1, 1, 1);
    QVector3D pivot;            // the pivot moved by so much from where the part's middle is
    bool operator==(NodeOverride const& o) const = default;
    bool plain() const { return !hidden && !shapeHidden && !available && move.isNull() && turn.isNull() && scale == QVector3D(1, 1, 1) && pivot.isNull(); }
};

// How the model is lit and shaded.
struct Shading {
    // Where the light comes from: round from straight ahead (degrees, less: from the left) and up (degrees).
    double azimuth = -19.290;
    double elevation = 25.270;
    QColor light = Qt::white;
    double intensity = 1.0;     // Adjust Light Intensity
    double ambient = 0.32;      // how lit the parts the light does not reach still are
    double specular = 0.0;      // Specular Strength: how bright the shine is (0: none)
    double shininess = 24.0;    // how small and sharp the shine is
    bool smooth = false;        // Generate Normals: shaded smoothly over the corners, rather than face by face
    int toonBands = 0;          // Toon Shader: the light in so many flat steps (0: smooth)
    double lineRadius = 0.0;    // LINE_RADIUS: an outline round the model and where it folds over itself (pixels; 0: none)
    QColor lineColour = Qt::black;  // LINE_COLOR
    bool grey = false;          // GRAYSCALE
    bool operator==(Shading const& o) const = default;
    QVector3D direction() const;
};

// The drawing's own look of its model.
struct Look {
    double rotX = 0.0, rotY = 0.0, rotZ = 0.0;      // its own turn, before the layer's
    QVector3D stretch = QVector3D(1, 1, 1);         // scaled along its own axes (Scaling, with the axes unlocked)
    double focal = 0.0;         // Focal Length (mm): seen in perspective; 0: no perspective
    double truck = 0.0;         // Camera Truck In/Out: the camera so many model sizes nearer (less: further)
    Shading shading;
    QVector<NodeOverride> overrides;
    bool operator==(Look const& o) const = default;
    bool plain() const { return *this == Look(); }
    NodeOverride const* overrideOf(QString const& name) const;
    NodeOverride& overrideFor(QString const& name);
    // The overrides that change nothing taken out.
    void tidy();
};

QJsonObject toJson(Look const& look);
Look lookFromJson(QJsonObject const& json);

// The model in a file (read once, then kept while the file stays as it is); null if it could not be read (`error` says why).
std::shared_ptr<Mesh const> load(QString const& file, QString* error = nullptr);
// The kinds of file read, as a file dialog filter ("*.obj *.stl ...").
QString fileFilter();
bool isModelFile(QString const& file);
// A mesh read from the text of an OBJ file (materials from `folder`, where the file was).
std::shared_ptr<Mesh const> parseObj(QByteArray const& text, QString const& folder, QString* error = nullptr);
std::shared_ptr<Mesh const> parseStl(QByteArray const& data, QString* error = nullptr);
std::shared_ptr<Mesh const> parsePly(QByteArray const& data, QString* error = nullptr);
std::shared_ptr<Mesh const> parseOff(QByteArray const& data, QString* error = nullptr);
// glTF 2.0: the JSON (.gltf, its buffers and pictures beside it in `folder` or inside it) or the binary (.glb).
std::shared_ptr<Mesh const> parseGltf(QByteArray const& data, QString const& folder, QString* error = nullptr);
// The middles, sizes and part pivots worked out once the triangles are in (false: there are none).
bool finish(Mesh& mesh, QString* error);

QImage render(Mesh const& mesh, double rotX, double rotY, int pixels, QTransform* placement);
QImage render(Mesh const& mesh, Look const& look, int pixels, QTransform* placement);

// What a drawing that is the model in `file` shows, turned so: one picture (none if the model could not be read).
bool picture(QString const& file, double rotX, double rotY, Picture& out);
bool picture(QString const& file, Look const& look, Picture& out);
// The models read and the pictures made of them let go: the files are read again when next shown.
void forget();

}
