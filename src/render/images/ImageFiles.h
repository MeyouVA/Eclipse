// Eclipse - writing rendered frames as picture files, in the formats Qt has and the ones Eclipse writes itself.
#pragma once

#include "EclipseExport.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <QStringList>

namespace ImageFiles {

struct Format {
    QString suffix;         // also the format's name in a scene file
    QString name;           // as the format menus show it
    bool alpha = true;      // keeps transparency
    bool layers = false;    // can keep each layer of the scene apart
    bool readable = true;
    bool writable = true;
    bool animated = false;
    QStringList aliases;
    int alphaBits = 8;
    int channelBits = 8;
    bool floating = false;
};

// The shared picture format registry, with PNG first.
QVector<Format> const& formats();
// The format with this suffix (lower case, "tiff" taken as "tif"), or nullptr.
Format const* find(QString const& suffix);
QString fileFilter(bool writing = false);
QImage load(QString const& path, QString* error = nullptr, int frame = 0);
bool canRead(QString const& path);
QByteArray encodeEmbedded(QImage const& image);
QImage decodeEmbedded(QByteArray const& bytes);
QString embeddedSuffix(QByteArray const& bytes);
struct Frame {
    QImage image;
    int delayMs = 0;
};
QVector<Frame> readSequence(QString const& path, QString* error = nullptr);
bool writeSequence(QVector<Frame> const& frames, QString const& path, QString const& format, QString* error = nullptr, int loops = 0);
bool saveWithCodec(QImage const& image, QString const& path, QString const& format, QString* error, int quality);

// Writes `image` to `path` in `format` (a suffix); false with the reason when it cannot; `colourSpace` is said in the file where the format can (ICC, EXR chromaticities).
bool save(QImage const& image, QString const& path, QString const& format, QString* error = nullptr, int jpegQuality = 92, QString const& colourSpace = {});

// One layer of a layered file, the size of the picture.
struct Layer {
    QString name;
    QImage image;
    bool visible = true;
};
// A Photoshop file holding `layers` (bottom first) and `composite` as the flat picture.
bool saveLayered(QVector<Layer> const& layers, QImage const& composite, QString const& path, QString* error = nullptr, QString const& colourSpace = {});

}  // namespace ImageFiles
