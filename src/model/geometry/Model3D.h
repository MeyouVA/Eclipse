// Eclipse - 3D models brought into a scene (Harmony's 3D model elements): a model file read (Wavefront OBJ, with the
// colours and textures of its MTL materials), and pictures made of it, turned in space, for a layer to show.
//
// A drawing of a layer can be a model (Drawing::model, the file): what it shows is a picture of the model, made for the
// angles the layer is turned to in space on that frame (its pose's rotX and rotY) - lit from the front, the nearer parts
// hiding the further.  The model is centred on the drawing's middle and sized to kSize scene units at its biggest.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
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
};

struct Mesh {
    QVector<QVector3D> vertices;
    QVector<QVector2D> uvs;
    QVector<Triangle> triangles;
    QVector<Material> materials;
    // Its middle and how big it is, at its biggest (across, up or deep).
    QVector3D centre;
    double extent = 1.0;
};

// The model in a file (read once, then kept while the file stays as it is); null if it could not be read (`error` says why).
std::shared_ptr<Mesh const> load(QString const& file, QString* error = nullptr);
// A mesh read from the text of an OBJ file (materials from `folder`, where the file was).
std::shared_ptr<Mesh const> parseObj(QByteArray const& text, QString const& folder, QString* error = nullptr);

// A picture of the model turned `rotX` degrees about the line across it (its top leaning away) and `rotY` about the line up
// it (its right side going away), `pixels` across and down; `placement` takes its pixels to the drawing's coordinates.
QImage render(Mesh const& mesh, double rotX, double rotY, int pixels, QTransform* placement);

// What a drawing that is the model in `file` shows, turned so: one picture (none if the model could not be read).
bool picture(QString const& file, double rotX, double rotY, Picture& out);

}
