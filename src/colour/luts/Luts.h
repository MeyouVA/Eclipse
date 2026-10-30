// Eclipse - look-up tables: reading the usual LUT file formats as steps of a colour conversion, and writing a .cube.
#pragma once

#include "colour/maths/ColourOps.h"

#include <QByteArray>
#include <QStringList>

namespace Cms::Luts {

// The suffixes of the formats read, and a file dialog filter for them.
QStringList suffixes();
QString filter();

// Reads a LUT file (its format by its suffix, then by what it holds); `cccId` picks a correction from a .ccc or .cdl.
bool read(QString const& path, Chain* out, QString* error, QString const& cccId = {});
// The same from text already read; `suffix` says the format.
bool readText(QByteArray const& data, QString const& suffix, Chain* out, QString* error, QString const& cccId = {}, QString const& folder = {});

// A 3D LUT of `size` points a side sampling the chain over 0..1, as a .cube file.
QByteArray writeCube(Chain const& chain, int size, QString const& title);

// Makes a 1D LUT step from tables (red, green, blue one after another) over a range; and a 3D one (red fastest).
Op lut1d(QVector<float> tables, int size, double low = 0.0, double high = 1.0);
Op lut3d(QVector<float> cube, int size, double low = 0.0, double high = 1.0);
// A curve for each channel through points (x rising), as an even 1D LUT of `samples` over each one's range.
Op curveThrough(std::array<QVector<double>, 3> const& x, std::array<QVector<double>, 3> const& y, int samples = 4096);
// The XML formats: Common LUT Format (.clf, .ctf) and colour decisions (.cdl, .cc, .ccc).
bool readClf(QByteArray const& data, Chain* out, QString* error, QString const& folder);
bool readCdl(QByteArray const& data, Chain* out, QString* error, QString const& cccId);

}  // namespace Cms::Luts
