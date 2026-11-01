#include "ui/io/SvgStrokes.h"
#include <QFile>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <cmath>

namespace {
QVector<Stroke> curves(QPainterPath const& path)
{
    QVector<Stroke> result;
    Stroke line;
    auto finish = [&] {
        if (line.points.size() > 1) {
            if (line.points.last().pos == line.points.first().pos) {
                line.closed = true;
                line.points.first().in = line.points.last().in;
                line.points.removeLast();
            }
            result.push_back(line);
        }
        line = Stroke(); line.curve = true;
    };
    line.curve = true;
    for (int i = 0; i < path.elementCount(); ++i) {
        auto const e = path.elementAt(i);
        QPointF const p(e.x, e.y);
        if (e.isMoveTo()) { finish(); line.points.push_back({p, 1.0f, true, true}); }
        else if (e.isLineTo()) line.points.push_back({p, 1.0f, true, true});
        else if (e.type == QPainterPath::CurveToElement && !line.points.isEmpty() && i + 2 < path.elementCount()) {
            auto const second = path.elementAt(++i), end = path.elementAt(++i);
            line.points.last().out = p - line.points.last().pos;
            StrokePoint point{QPointF(end.x, end.y), 1.0f, true, true};
            point.in = QPointF(second.x, second.y) - point.pos;
            line.points.push_back(point);
        }
    }
    finish();
    return result;
}

class Engine final : public QPaintEngine {
public:
    SvgStrokes::Result result;
    QString problem;
    Engine() : QPaintEngine(AllFeatures) {}
    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    Type type() const override { return User; }
    void updateState(QPaintEngineState const&) override {}
    void drawPixmap(QRectF const&, QPixmap const&, QRectF const&) override { problem = QStringLiteral("The SVG contains raster images or raster effects. Import it as a picture instead."); }
    void drawImage(QRectF const&, QImage const&, QRectF const&, Qt::ImageConversionFlags) override { problem = QStringLiteral("The SVG contains raster images or raster effects. Import it as a picture instead."); }
    void drawPolygon(QPointF const* points, int count, PolygonDrawMode mode) override {
        if (!count) return;
        QPainterPath path; path.moveTo(points[0]);
        for (int i = 1; i < count; ++i) path.lineTo(points[i]);
        if (mode != PolylineMode) path.closeSubpath();
        path.setFillRule(mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
        drawPath(path);
    }
    void drawTextItem(QPointF const& at, QTextItem const& text) override {
        QPainterPath path; path.addText(at, text.font(), text.text());
        paintFill(path, QBrush(painter()->pen().color()));
    }
    void paintFill(QPainterPath const& original, QBrush const& brush) {
        if (brush.style() == Qt::NoBrush) return;
        QPainter* const p = painter();
        QPainterPath path = p->worldTransform().map(original);
        if (p->hasClipping()) path = path.intersected(p->worldTransform().map(p->clipPath()));
        if (path.isEmpty()) return;
        Fill fill; fill.color = brush.color();
        fill.color.setAlphaF(fill.color.alphaF() * p->opacity());
        if (auto const* gradient = brush.gradient()) {
            Swatch swatch; swatch.name = QStringLiteral("SVG Gradient");
            swatch.kind = gradient->type() == QGradient::LinearGradient ? Swatch::Linear : Swatch::Radial;
            if (gradient->spread() != QGradient::PadSpread || gradient->type() == QGradient::ConicalGradient) { problem = QStringLiteral("This SVG gradient cannot be represented as editable artwork. Import it as a picture instead."); return; }
            QTransform transform = brush.transform();
            if (gradient->coordinateMode() == QGradient::ObjectBoundingMode || gradient->coordinateMode() == QGradient::ObjectMode) {
                QRectF const bounds = original.boundingRect();
                QTransform const box(bounds.width(), 0, 0, bounds.height(), bounds.x(), bounds.y());
                transform = gradient->coordinateMode() == QGradient::ObjectMode ? transform * box : box * transform;
            }
            transform *= p->worldTransform();
            if (gradient->type() == QGradient::LinearGradient) {
                auto const* linear = static_cast<QLinearGradient const*>(gradient);
                fill.gradFrom = transform.map(linear->start());
                QPointF const direction = linear->finalStop() - linear->start();
                double const squared = QPointF::dotProduct(direction, direction);
                bool invertible = false; QTransform const inverse = transform.inverted(&invertible);
                if (!invertible || squared < 1e-20) { problem = QStringLiteral("The SVG gradient has a degenerate transform."); return; }
                QPointF const normal((inverse.m11() * direction.x() + inverse.m12() * direction.y()) / squared, (inverse.m21() * direction.x() + inverse.m22() * direction.y()) / squared);
                fill.gradTo = fill.gradFrom + normal / QPointF::dotProduct(normal, normal);
            } else {
                auto const* radial = static_cast<QRadialGradient const*>(gradient);
                if (radial->focalPoint() != radial->center()) { problem = QStringLiteral("An off-centre radial gradient needs picture import."); return; }
                fill.gradFrom = transform.map(radial->center()); fill.gradTo = transform.map(radial->center() + QPointF(radial->radius(), 0));
                if (std::abs(QLineF(fill.gradFrom, transform.map(radial->center() + QPointF(0, radial->radius()))).length() - QLineF(fill.gradFrom, fill.gradTo).length()) > 1e-4) { problem = QStringLiteral("An elliptical radial gradient needs picture import."); return; }
            }
            for (auto const& stop : gradient->stops()) { QColor color = stop.second; color.setAlphaF(color.alphaF() * p->opacity()); swatch.stops.push_back({stop.first, color}); }
            if (!swatch.stops.isEmpty()) swatch.color = swatch.stops.first().color;
            fill.swatch = int(result.gradients.size()); result.gradients.push_back(swatch); fill.gradPlaced = true;
        } else if (brush.style() != Qt::SolidPattern) { problem = QStringLiteral("SVG patterns need picture import."); return; }
        // Resolve nonzero winding into disjoint boundaries before storing the drawing's even-odd contours.
        int moves = 0;
        for (int i = 0; i < path.elementCount(); ++i) if (path.elementAt(i).isMoveTo()) ++moves;
        if (path.fillRule() == Qt::WindingFill && moves > 1) path = path.simplified();
        fill.contours = curves(path);
        for (Stroke& contour : fill.contours) contour.closed = true;
        fill.rebuild(); fill.order = ++order;
        result.drawing.fills.push_back(fill);
    }
    void drawPath(QPainterPath const& path) override {
        QPainter* const p = painter();
        paintFill(path, p->brush());
        QPen const pen = p->pen();
        if (pen.style() == Qt::NoPen) return;
        QTransform const t = p->worldTransform();
        double const sx = std::hypot(t.m11(), t.m12()), sy = std::hypot(t.m21(), t.m22());
        bool const uniform = std::abs(sx - sy) < 1e-6 && std::abs(t.m11() * t.m21() + t.m12() * t.m22()) < 1e-6;
        if (!uniform || p->hasClipping() || pen.style() != Qt::SolidLine || pen.brush().style() != Qt::SolidPattern || pen.capStyle() == Qt::SquareCap) {
            QPainterPathStroker stroker; stroker.setWidth(pen.widthF()); stroker.setJoinStyle(pen.joinStyle()); stroker.setCapStyle(pen.capStyle()); stroker.setMiterLimit(pen.miterLimit());
            if (pen.style() != Qt::SolidLine) { stroker.setDashPattern(pen.dashPattern()); stroker.setDashOffset(pen.dashOffset()); }
            paintFill(stroker.createStroke(path), pen.brush()); return;
        }
        for (Stroke line : curves(t.map(path))) {
            line.color = pen.color(); line.color.setAlphaF(line.color.alphaF() * p->opacity());
            line.width = float(pen.widthF() * (pen.isCosmetic() ? 1.0 : sx));
            line.capStart = line.capEnd = pen.capStyle() == Qt::RoundCap ? Stroke::RoundCap : Stroke::FlatCap;
            line.join = pen.joinStyle() == Qt::RoundJoin ? Stroke::RoundJoin : pen.joinStyle() == Qt::BevelJoin ? Stroke::BevelJoin : Stroke::MitreJoin;
            line.order = ++order; result.drawing.strokes.push_back(line);
        }
    }
private:
    int order = 0;
};
class Device final : public QPaintDevice {
public:
    mutable Engine engine;
    QSize size;
    QPaintEngine* paintEngine() const override { return &engine; }
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmWidth: return size.width(); case PdmHeight: return size.height();
        case PdmWidthMM: return qRound(size.width() * 25.4 / 96); case PdmHeightMM: return qRound(size.height() * 25.4 / 96);
        case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 96;
        case PdmDepth: return 32; case PdmDevicePixelRatio: return 1; case PdmDevicePixelRatioScaled: return int(devicePixelRatioFScale());
        default: return 0;
        }
    }
};
}

bool SvgStrokes::read(QString const& path, Result& result, QString* error)
{
    auto fail = [&](QString const& why) { if (error) *error = why; return false; };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QByteArray const bytes = file.readAll();
    QXmlStreamReader xml(bytes);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        auto const name = xml.name();
        if (name == QLatin1String("filter") || name == QLatin1String("mask") || name == QLatin1String("foreignObject") || name == QLatin1String("animate") || name == QLatin1String("animateTransform") || name == QLatin1String("set"))
            return fail(QStringLiteral("This SVG contains effects or animation that need picture import."));
    }
    if (xml.hasError()) return fail(xml.errorString());
    QSvgRenderer renderer(bytes); renderer.setAnimationEnabled(false);
    if (!renderer.isValid()) return fail(QStringLiteral("Invalid SVG document."));
    Device device; device.size = renderer.defaultSize();
    if (device.size.isEmpty()) device.size = renderer.viewBoxF().size().toSize();
    if (device.size.isEmpty()) return fail(QStringLiteral("The SVG has no usable size."));
    QPainter painter(&device); renderer.render(&painter, QRectF(QPointF(), device.size)); painter.end();
    if (!device.engine.problem.isEmpty()) return fail(device.engine.problem);
    result = device.engine.result; result.size = device.size;
    return true;
}
