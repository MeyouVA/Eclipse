// Eclipse - vectorising with options: what a scanned or imported picture becomes, set out as named styles or as a line of
// options ("-2pass -threshl 0.1 -rmv_dirt 10 ..."): line art and colour art each with a threshold, holes and dirt removed,
// outlines fitted and smoothed, coloured construction lines left out or kept apart, margins and peg bars cleared, the
// sheet registered by its peg holes, and the result made into a drawing.
#pragma once

#include "model/scene/Scene.h"

#include <QImage>
#include <QStringList>
#include <QTransform>

namespace Vectorize {

struct Options {
    enum Mode { BlackAndWhite, CentreLines, Colour };
    int mode = BlackAndWhite;           // -pencil: CentreLines; -color_vectorize: Colour
    int colours = 16;                   // Colour: at most this many
    bool twoPass = false;               // -2pass: a line art and a colour art, each with its own settings
    // Thresholds (0..1): line art is darker than `threshLine` of white; colour art (two passes) the rest darker than
    // 1 - `threshColour` of white.
    double threshLine = 0.5;
    double threshColour = 0.5;
    double holesLine = 7.0, holesColour = 7.0;      // -rmv_holes: holes smaller than this (pixels) filled
    double dirtLine = 1.0, dirtColour = 1.0;        // -rmv_dirt: specks smaller than this (pixels) left out
    bool keepDirt = false;
    double fitLine = 1.0, fitColour = 1.0;          // -fit_error: how far (pixels) the outlines may stray to have fewer points
    int smoothLine = 1, smoothColour = 1;           // -smooth: passes over the outlines
    int firstSmoothLine = 0, firstSmoothColour = 0; // -first_smooth: passes over the pixels' outline first
    int jagLine = 0, jagColour = 0;                 // -jag_filter: jagged edges evened over this many pixels
    int expandLine = 0, expandColour = 0;           // -expand_bitmap: the art made this many pixels fatter
    double hairs = 0.0;                             // -rmv_hairs: hairs thinner than this many pixels taken off
    int hairPasses = 1;
    bool noRed = false, noGreen = false, noBlue = false;    // lines of that colour left out (construction lines)
    // -rgb: red, green and blue lines each made a shape of its own colour (this alpha), apart from the black line art.
    bool rgb = false;
    int rgbAlpha = 255;
    double threshRgb = 0.2;                         // how much more one channel is than the others to be coloured
    double threshSaturation = 0.5, threshValue = 0.5;
    int expandRgb = 0;
    double dirtRgb = 0.0;
    // -pressure_variation: the centre lines' pressure from their thickness (strategy 0: none; 1: thickness; 2: thickness, eased).
    int pressureStrategy = 0;
    double minPressure = 0.2, maxPressure = 1.0, maxVariation = 1.0;
    // Gaps in the line art up to this many pixels closed for the colour art (-gap; -noclosegap: none).
    double gap = 10.0;
    bool closeGaps = true;
    // A frame round the colour art (two passes), so that the paper round the drawing can be painted too; this many fields
    // wide (else the picture's size).
    bool frame = true;
    double frameFields = -1.0;
    bool flatten = false;               // -flatten: the shapes of one colour made one
    bool buildMatte = false;            // -buildmatte: a matte of the whole drawing underneath it (on the Underlay)
    bool matteColourArt = false;        // -buildmatte_colourart: a matte of the colour art
    // Margins cleared (inches: -margins, -top_margin...; pixels: -pixel_margins...).
    double marginTop = 0.0, marginBottom = 0.0, marginLeft = 0.0, marginRight = 0.0;
    int pixelTop = 0, pixelBottom = 0, pixelLeft = 0, pixelRight = 0;
    double dpi = -1.0;                  // of the picture (-1: 72, or what the picture says)
    int downscale = 1;                  // -downscale_input
    // Optical registration by the peg holes (-register <dpi side strictness>).
    bool registerPegs = false;
    int pegSide = 0;                    // 0 top, 1 bottom, 2 left, 3 right
    double looseness = 2.0;             // -registration_looseness_factor
    double threshRegister = 0.5;        // -threshr: how dark a hole shows
    bool centreHolesOnly = false;       // -register_center_peg_holes
    double pegPitch = 8.0;              // inches between peg bars
    double pegMargin = 1.0;             // -rmargin: inches from the edge to look in
    double pegFromCentre = 5.25;        // -peg_distance_from_center: inches from the middle to a side hole
    int outPegSide = -1;                // -out_peg_position: where the pegs are to be (-1: the same side)
    bool outputPegMatrix = false;
    bool registerDebug = false;         // -rdebug
    bool removePegBars = false;         // the peg bar strip cleared
    double pegBarSize = 1.0;            // inches
    double fieldSize = -1.0;            // -field_size: the picture made this many fields wide (-1: its own size)
    double calibrateX = 1.0, calibrateY = 1.0;  // -scanner_calibrate
    QString secondFile;                 // -file2: the colour art read from another picture
    bool operator==(Options const&) const = default;
};

// The styles to choose from (Default Style, Grayscale, ...) and what each is.
QStringList styleNames();
Options style(QString const& name);
QString styleLine(QString const& name);
// A line of options read into `options` (over what it has); false, with `error`, at an option it does not know or a value
// that is not a number.  `ignored`: options read that have nothing to do in Eclipse (textured strokes, bubbles).
bool parse(QString const& line, Options& options, QString* error = nullptr, QStringList* ignored = nullptr);
// The options as a line (those not as a new Options has them).
QString toLine(Options const& options);
// What each option is, for -help.
QString help();

// Where the peg holes put the picture: the move that brings its round middle hole to where it is to be and its holes level.
// False (with `why`) when the holes are not found.
bool registration(QImage const& image, Options const& options, QTransform& move, QString* why = nullptr, QString* debug = nullptr);

struct Result {
    Drawing drawing;            // in the picture's pixels (after registration, scaled by field size / calibration)
    QTransform placed;          // from the picture's pixels to the drawing's
    QStringList notes;          // registration matrix, debug lines, what was left out
    bool ok = true;
};
// The picture vectorised as the options say: line art on the Line Art, colour art on the Colour Art.  `colour` and
// `swatch`: what the line art is painted with.
Result run(QImage const& image, Options const& options, QColor const& colour = Qt::black, int swatch = -1);

}  // namespace Vectorize
