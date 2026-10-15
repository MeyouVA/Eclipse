// Eclipse - what the effects of a layer do to its picture, and the panel that edits them.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QImage>
#include <QWidget>

#include "model/scene/Scene.h"

class Document;
class QPlainTextEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Effects {

// Softens a picture (premultiplied ARGB) by about `radius` pixels.
void blur(QImage& image, double radius);
// Moves every colour towards `color` by `amount` (0..1), leaving the shape as it is.
void tint(QImage& image, QColor const& color, double amount);

// A soft copy of the shape of the picture in one colour, `radius` pixels soft, moved by
// `offset` pixels, put under the picture (a shadow) - or with no offset, around it (a glow).
void halo(QImage& image, QColor const& color, double radius, QPointF offset);

// Makes the picture `amount` (0..1) see-through.
void transparency(QImage& image, double amount);
// Smears the picture `length` pixels along a direction (degrees, 0: across, 90: up and down).
void directionalBlur(QImage& image, double length, double degrees);
// Turns the hue by `hue` degrees, and raises the saturation and the lightness by so many percent; `amount` of it (0..1).
void hueSaturation(QImage& image, double hue, double saturation, double lightness, double amount);
// So many percent brighter and more contrast; `amount` of it (0..1).
void brightnessContrast(QImage& image, double brightness, double contrast, double amount);
// A grain over the picture (where it is painted), `amount` strong (0..1); `seed` says which grain.
void grain(QImage& image, double amount, quint32 seed);
// Each point smeared along the line from the middle of what is painted, `amount` (0..1) of how far it is from it.
void radialBlur(QImage& image, double amount);
// Each colour multiplied by `scale` (its red, green and blue as shares), `amount` of the way (0..1).
void colourScale(QImage& image, QColor const& scale, double amount);
// The colours turned to their opposites, `amount` of the way (0..1).
void negate(QImage& image, double amount);
// Each point the middle colour (and opacity) of those within `radius` pixels: specks go, edges stay.
void median(QImage& image, double radius);
// The rows of the picture rocked side to side by waves `height` pixels high and `length` pixels apart, moved on by `frame`.
void ripple(QImage& image, double height, double length, int frame);
// The bright parts (lighter than about two thirds) blurred `radius` pixels and added back over the picture: a glow of light.
void bloom(QImage& image, double radius);
// Blocks `size` pixels across, each the mean colour of what lies in it.
void pixelate(QImage& image, double size);
// Each point taken from up to `strength` pixels away, by a smooth noise whose swirls are about `size` pixels across; it moves
// on with `frame`.
void turbulence(QImage& image, double strength, double size, int frame);
// The picture made a soft matte of `colour`: solid where it is painted and as far as `inner` pixels out from it, fading to
// nothing at `outer` pixels.
void matteGenerator(QImage& image, double inner, double outer, QColor const& colour);
// The colours made grey (as light as they were), `amount` of the way (0..1).
void blackWhite(QImage& image, double amount);
// What is painted grown by `radius` pixels all round (shrunk, with `shrink`): each point the strongest (weakest) of those round it.
void matteResize(QImage& image, double radius, bool shrink);
// What is painted coloured from `from` to `to` across it, along `degrees` (0: left to right) or, `radial`, from its middle
// out - `amount` of the way (0..1); how see-through it is stays as it was.
void gradient(QImage& image, QColor const& from, QColor const& to, double degrees, bool radial, double amount);

QString name(Effect const& effect, Scene const& scene);
// One channel of the picture (0 red, 1 green, 2 blue, 3 opacity) as grey: the colour channels where the picture is, the
// opacity over the whole of it.
void channelSelect(QImage& image, int channel);
// Only what lies inside a rectangle of the image (in its pixels) is kept.
void crop(QImage& image, QRectF const& keep);
// What is more opaque than `threshold` (0..1) made wholly opaque, the rest taken away.
void removeTransparency(QImage& image, double threshold);
// The picture made a white matte of itself, as opaque as it is.
void matteComposite(QImage& image);
// `top` laid over `under` as a blend mode says (a Layer::Blend): the two together, in `under`.
void blendOnto(QImage& under, QImage const& top, int mode);
// Blurred `radius` where the matte is white, less where it is darker, not at all where there is none.
void variableBlur(QImage& image, QImage const& matte, double radius);
// A 12-field chart of a frame (where it lies in the image, in its pixels) drawn over the image: its edge, the field lines
// either side of the middle, the cross and the diagonals.
void fieldChart(QImage& image, QRectF const& frame, QColor const& colour, double width);
// What each of the 256 levels turns into on a curve through `points` (in, out; 0..1, in order of `in`): smooth, and never
// turning back between two points (flat before the first and after the last).  No points: each level as it is.
QVector<int> curveLevels(QVector<QPointF> const& points);
// Each colour level changed as the curves say (the three together, then red, green and blue), `amount` of the way (0..1).
void colourCurves(QImage& image, QVector<QPointF> const (&curves)[4], double amount);
// The image moved by a transform (in its pixels).
void transformImage(QImage& image, QTransform const& transform);
// The image made at `scale` (0..1) of its resolution, then brought back to its size.
void scaleOutput(QImage& image, double scale);
// `top` and `under` joined in `under`: the two together, and the gaps between them up to `radius` pixels across filled in
// the colours of what is round them.
void glue(QImage& under, QImage const& top, double radius);
// A Script effect: `script` (JavaScript) run on the picture.  It sees `frame` (from 1), `amount`, and `image`: width and
// height (pixels), pixel(x, y) -> "#aarrggbb", setPixel(x, y, "#rrggbb" or "#aarrggbb"), and blur(radius), tint(colour,
// amount), transparency(amount), negate(amount), blackWhite(amount), hueSaturation(hue, saturation, lightness),
// brightnessContrast(brightness, contrast), pixelate(size), median(radius), bloom(radius), grain(amount),
// directionalBlur(length, degrees), colourScale(colour, amount) - distances in scene units (`unit` pixels each).
// What went wrong, or empty.
QString runScript(QImage& image, QString const& script, int frame, double amount, double unit);
// An External effect: the command line run on the picture ({in}, {out}, {frame} filled in); waits up to `seconds` for it.
// What went wrong, or empty.
QString runExternal(QImage& image, QString const& command, int frame, int seconds = 60);

}

// A curve to drag the points of: what each level (0..1, across) turns into (up).  A click away from the points adds one; a
// double click on one (not either end) takes it away.
class ECLIPSE_UI_API ColourCurveEdit : public QWidget {
    Q_OBJECT

public:
    explicit ColourCurveEdit(QWidget* parent = nullptr);
    void setPoints(QVector<QPointF> const& points);
    // The points, both ends included.
    QVector<QPointF> points() const { return m_points; }
    QSize sizeHint() const override { return QSize(160, 160); }

signals:
    // The curve has been changed (once, when the mouse lets go).
    void edited(QVector<QPointF> const& points);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QRectF box() const;
    QPointF toWidget(QPointF const& level) const;
    QPointF toLevel(QPointF const& at) const;
    int pointAt(QPointF const& at) const;

    QVector<QPointF> m_points;
    int m_dragged = -1;
};

// The effects of the current layer, in the order they are applied: tick to switch one on
// or off, pick one to change it.
class ECLIPSE_UI_API EffectsPanel : public QWidget {
    Q_OBJECT

public:
    explicit EffectsPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void setCurrentCell(int layer, int frame);

private:
    void rebuild();
    void showEditors();
    void add(Effect::Type type);
    void removeCurrent();
    // The effect picked (none picked: all of them) copied; the effects copied added to the layer, after its own.
public:
    void copyEffects();
    void pasteEffects();
    static bool hasCopiedEffects() { return !s_copied.isEmpty(); }

private:
    static QVector<Effect> s_copied;
    // Replaces effect `row` by what `change` makes of it.
    template <class Change> void edit(Change change);

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    bool m_rebuilding = false;

    QListWidget* m_list;
    QLabel* m_amountLabel;
    QDoubleSpinBox* m_amount;
    QPushButton* m_color;
    QPushButton* m_endColor = nullptr;     // a gradient's second colour
    QLabel* m_matteLabel;
    QComboBox* m_matte;
    QCheckBox* m_invert;
    QWidget* m_particles = nullptr;
    // Hue-Saturation, Brightness-Contrast and Directional Blur: hue, saturation, lightness, brightness, contrast, direction.
    QWidget* m_adjust = nullptr;
    class QFormLayout* m_adjustForm = nullptr;
    QDoubleSpinBox* m_adjustBox[6] = {};
    // life, speed, spread, direction, gravity, seed, size at the end, wind, spin, region width and height, floor, bounce
    QDoubleSpinBox* m_particleBox[14] = {};
    QPushButton* m_particleColour = nullptr;
    class QComboBox* m_particleShape = nullptr;
    class QCheckBox* m_particleRecolour = nullptr;
    class QCheckBox* m_particleFollow = nullptr;
    QCheckBox* m_animated;
    QLabel* m_offsetLabel;
    QDoubleSpinBox* m_offsetX;
    QDoubleSpinBox* m_offsetY;
    // Channel Selector's channel; Crop's area (left, top, width, height).
    QWidget* m_more = nullptr;
    class QFormLayout* m_moreForm = nullptr;
    QComboBox* m_channel = nullptr;
    QDoubleSpinBox* m_cropBox[4] = {};
    // Colour Curves: which curve (all three colours, red, green, blue) and the curve.
    QComboBox* m_curveWhich = nullptr;
    ColourCurveEdit* m_curve = nullptr;
    // Script and External: the script (or the command line), put to use by Apply.
    QPlainTextEdit* m_text = nullptr;
    QPushButton* m_textApply = nullptr;
    QLabel* m_textSays = nullptr;
};
