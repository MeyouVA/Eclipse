// Eclipse - the PDF reader (Illustrator files since version 9 are PDF files too): the vector artwork of each page - its
// filled and stroked paths, in their colours (grey, RGB, or CMYK made RGB) and opacity, through forms - kept apart by the
// layers of the file (its optional content groups).  Text and pictures are counted, not read.
#pragma once

#include "EclipseExport.h"
#include "model/scene/Scene.h"

#include <QSizeF>
#include <QString>
#include <QVector>

namespace Pdf {

struct ArtLayer {
    QString name;           // the file's layer ("" for artwork in none)
    Drawing drawing;        // in points, from the page's top left, down the page
};

struct Page {
    QSizeF size;            // in points
    QVector<ArtLayer> layers;
};

struct Options {
    bool cmyk = true;       // CMYK colours made RGB (else artwork in CMYK is left out)
};

struct Report {
    int texts = 0;          // pieces of text left out
    int images = 0;         // pictures left out
    int cmykLeftOut = 0;    // paths in CMYK left out (Options::cmyk off)
    int streamsUnread = 0;  // content in a compression that is not read
};

ECLIPSE_FORMATS_API bool read(QString const& file, QVector<Page>& pages, QString* error = nullptr, Options const& options = {}, Report* report = nullptr);
ECLIPSE_FORMATS_API bool readData(QByteArray const& data, QVector<Page>& pages, QString* error = nullptr, Options const& options = {}, Report* report = nullptr);

}  // namespace Pdf
