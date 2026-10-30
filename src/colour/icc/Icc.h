// Eclipse - ICC profiles: reading the matrix-and-curves kind as a colour space, and writing one for a colour space.
#pragma once

#include "colour/spaces/ColourConfig.h"

#include <QByteArray>

namespace Cms::Icc {

struct Profile {
    QString description;
    QString copyright;
    bool grey = false;
    int version = 0;            // the major version: 2 or 4
    Matrix3 toXyzD50 = identity3(); // linear RGB to the profile connection space (D50)
    Matrix3 adaptation = identity3(); // its chromatic adaptation (chad), the white of the device to D50
    double whiteX = kD50x, whiteY = kD50y;  // the device's white
    Chain decode;               // the curves: coded values to linear light, per channel
    Primaries primaries;        // the device's primaries and white, worked out from the matrix
};

bool read(QByteArray const& data, Profile* out, QString* error);
// A colour space of the profile, against the configuration's reference (through its CIE XYZ D65 interchange).
bool spaceOf(Profile const& profile, Config const& config, Space* out, QString* error);
bool spaceOfFile(QString const& path, Config const& config, Space* out, QString* error);
// A version 4 profile of primaries and a curve; empty when the curve cannot be written as one.
QByteArray write(Primaries const& primaries, Curve curve, QString const& description);
// The profile of a colour space of the configuration in use (its primaries and curve known), or empty.
QByteArray profileOf(QString const& spaceName);

}  // namespace Cms::Icc
