// Eclipse - a colour conversion made ready to run on colours and pictures.
#pragma once

#include "colour/spaces/ColourConfig.h"

#include <QColor>
#include <QImage>

namespace Cms {

class Transform {
public:
    Transform() = default;
    explicit Transform(Chain chain);
    // Nothing to do: every colour comes out as it went in.
    bool isIdentity() const { return !d; }
    // Tells conversions apart (for caches); 0 for the identity.
    quint64 id() const;
    Chain chain() const;
    void apply(float* rgb) const;
    QColor apply(QColor const& colour) const;
    // A colour of 8 bits a channel, not multiplied by its alpha.
    QRgb applyStraight(QRgb colour) const;
    // Converts a picture in place (8 or 16 bits a channel, or float; it keeps its format); its alpha is kept.
    void apply(QImage* image) const;
    QImage applied(QImage const& image) const;
    // The picture converted with floats kept (RGBA32FPx4, multiplied by alpha): for files that keep more than 8 bits.
    QImage toFloat(QImage const& image) const;

private:
    void applyDeep(QImage* image) const;
    struct Data;
    std::shared_ptr<Data const> d;
};

// From one colour space to another in the configuration in use; the identity (with the reason) when one cannot be found.
Transform between(QString const& from, QString const& to, QString* error = nullptr);
// From a colour space to a view of a display.
Transform toDisplay(QString const& from, QString const& display, QString const& view, QString* error = nullptr);
// A transform of steps read from a LUT file (kept once read, until the file changes).
Transform ofLut(QString const& path, QString* error = nullptr);

}  // namespace Cms
