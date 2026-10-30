// Eclipse - the Camera view: bitmap art - brush tips stamped, .abr brushes read, vector art turned into pixels, resolution.
#include "canvas/Canvas.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QSet>
#include <QInputDialog>
#include <QFile>

#include <QImage>
#include <QPainter>

#include <cmath>
#include <numeric>

#include "model/document/Commands.h"
#include "model/geometry/Vectorize.h"

namespace {
bool animatedSheet(Layer const& layer, int drawing, int sheet)
{
    for (Keyframe const& key : layer.keys)
        for (PointMove const& move : key.pose.points)
            if (move.drawing == drawing && move.sheet == sheet)
                return true;
    return false;
}
}

void Canvas::stampTip(QPainter& onto, Stroke const& stroke, double share, QBrush const& brush) const
{
    TipShape const& t = m_tipShape;
    Dabs mine;
    mine.kind = m_bitmapTip == BitmapImage && m_tipImage.isNull() ? BitmapRound : m_bitmapTip;
    mine.roundness = t.roundness;
    mine.angle = t.angle;
    mine.count = t.count;
    mine.spacing = t.spacing;
    mine.flipX = t.flipX;
    mine.flipY = t.flipY;
    mine.follow = t.followDirection;
    mine.randomAngle = t.randomAngle;
    mine.randomSpacing = t.randomSpacing;
    mine.randomCount = t.randomCount;
    mine.randomSize = t.randomSize;
    mine.randomOffset = t.randomOffset;
    mine.minOpacity = t.minOpacity;
    mine.size = stroke.width * share;
    stampDabs(onto, stroke.path(), mine, brush, m_tipImage);
}

void Canvas::stampDabs(QPainter& onto, QVector<StrokePoint> const& run, Dabs const& tip, QBrush const& brush, QImage const& picture) const
{
    if (run.isEmpty())
        return;
    onto.save();
    onto.setPen(Qt::NoPen);
    onto.setBrush(brush);
    // The same random choices each time the stroke is painted (a soft edge paints it more than once).
    quint32 seed = 0x9e3779b9u;
    auto random = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return double(seed >> 8) / double(1u << 24);
    };
    // A tip of your own, in the brush's colour: its light parts paint.
    QImage coloured;
    bool const own = tip.kind == BitmapImage && !picture.isNull();
    if (own) {
        QColor const colour = brush.color();
        QImage const grey = picture.convertToFormat(QImage::Format_ARGB32);
        coloured = QImage(grey.size(), QImage::Format_ARGB32);
        for (int y = 0; y < grey.height(); ++y)
            for (int x = 0; x < grey.width(); ++x) {
                QRgb const p = grey.pixel(x, y);
                coloured.setPixel(x, y, qRgba(colour.red(), colour.green(), colour.blue(), qGray(p) * qAlpha(p) / 255 * colour.alpha() / 255));
            }
    }
    int const kind = own ? BitmapImage : (tip.kind == BitmapImage ? BitmapRound : tip.kind);
    auto stamp = [&](QPointF at, double pressure, double heading) {
        double size = std::max(0.5, tip.size * std::clamp(pressure, 0.05, 1.0));
        if (tip.randomSize > 0)
            size *= 1.0 - random() * tip.randomSize / 100.0;
        int const dabs = std::max(1, tip.count + (tip.randomCount > 0 ? int(random() * (tip.randomCount + 1)) : 0));
        double const solid = (tip.minOpacity + (100 - tip.minOpacity) * std::clamp(pressure, 0.0, 1.0)) / 100.0;
        for (int d = 0; d < dabs; ++d) {
            QPointF place = at;
            if (tip.randomOffset > 0)
                place += QPointF(random() * 2.0 - 1.0, random() * 2.0 - 1.0) * (size * tip.randomOffset / 100.0);
            double const angle = tip.angle + (tip.follow ? heading : 0.0) + (tip.randomAngle > 0 ? (random() * 2.0 - 1.0) * tip.randomAngle : 0.0);
            double const tall = size * std::clamp(tip.roundness, 1, 100) / 100.0;
            onto.save();
            onto.translate(place);
            onto.rotate(-angle);
            onto.scale(tip.flipX ? -1.0 : 1.0, tip.flipY ? -1.0 : 1.0);
            onto.setOpacity(onto.opacity() * solid);
            switch (kind) {
            case BitmapSquare:
                onto.drawRect(QRectF(-size / 2, -tall / 2, size, tall));
                break;
            case BitmapChisel:
                // A flat tip held at 45 degrees: thick one way, thin the other, as a calligraphy pen.
                onto.rotate(-45.0);
                onto.drawEllipse(QRectF(-size / 2, -tall / 8, size, tall / 4));
                break;
            case BitmapSpray:
                for (int k = 0; k < 10; ++k) {
                    double const a = random() * 2.0 * 3.14159265358979, reach = std::sqrt(random());
                    double const grain = std::max(0.35, size * 0.04);
                    onto.drawEllipse(QPointF(std::cos(a) * reach * size / 2, std::sin(a) * reach * tall / 2), grain, grain);
                }
                break;
            case BitmapImage: {
                double const high = size * coloured.height() / std::max(1, coloured.width()) * std::clamp(tip.roundness, 1, 100) / 100.0;
                onto.drawImage(QRectF(-size / 2, -high / 2, size, high), coloured);
                break;
            }
            default:
                onto.drawEllipse(QRectF(-size / 2, -tall / 2, size, tall));
                break;
            }
            onto.restore();
        }
    };
    // (Which way the stroke goes, in degrees as the tip's angle counts them: anticlockwise, y up.)
    auto headingOf = [](QPointF a, QPointF b) { return -std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / 3.14159265358979; };
    double const usual = kind == BitmapSpray ? 0.3 : kind == BitmapImage ? 0.25 : 0.12;
    double const base = std::max(0.4, tip.size * (tip.spacing > 0 ? tip.spacing / 100.0 : usual));
    auto nextStep = [&] { return tip.randomSpacing > 0 ? std::max(0.2, base * (1.0 + (random() * 2.0 - 1.0) * tip.randomSpacing / 100.0)) : base; };
    stamp(run[0].pos, run[0].pressure, run.size() > 1 ? headingOf(run[0].pos, run[1].pos) : 0.0);
    double step = nextStep();
    double carried = 0.0;
    for (int i = 0; i + 1 < run.size(); ++i) {
        QPointF const a = run[i].pos, b = run[i + 1].pos;
        double const length = QLineF(a, b).length();
        double along = step - carried;
        while (along <= length) {
            double const t = length > 0 ? along / length : 0.0;
            stamp(a + (b - a) * t, run[i].pressure + (run[i + 1].pressure - run[i].pressure) * t, headingOf(a, b));
            step = nextStep();
            along += step;
        }
        carried = length - (along - step);
    }
    if (run.size() > 1)
        stamp(run.last().pos, run.last().pressure, headingOf(run[run.size() - 2].pos, run.last().pos));
    onto.restore();
}

QVector<QImage> Canvas::readAbr(QString const& path, QString* why)
{
    QVector<QImage> tips;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (why)
            *why = file.errorString();
        return tips;
    }
    QByteArray const bytes = file.readAll();
    qsizetype at = 0;
    auto u8 = [&]() -> int { return at < bytes.size() ? quint8(bytes[at++]) : (at++, 0); };
    auto u16 = [&]() -> int { int const a = u8(); return (a << 8) | u8(); };
    auto u32 = [&]() -> quint32 { quint32 const a = quint32(u16()); return (a << 16) | quint32(u16()); };
    int const version = u16(), sub = u16();
    if (version != 6 && version != 7 && version != 9 && version != 10) {
        if (why)
            *why = tr("Unsupported ABR Brush File Version - Detected version %1, supported versions are 6, 9 and 10.").arg(version);
        return tips;
    }
    // The "samp" section.
    qsizetype end = -1;
    while (at + 12 <= bytes.size()) {
        QByteArray const tag = bytes.mid(at, 4), name = bytes.mid(at + 4, 4);
        at += 8;
        quint32 const length = u32();
        if (tag != "8BIM")
            break;
        if (name == "samp") {
            end = std::min<qsizetype>(bytes.size(), at + length);
            break;
        }
        at += length;
    }
    if (end < 0) {
        if (why)
            *why = tr("The file has no sampled brushes.");
        return tips;
    }
    while (at + 4 < end) {
        quint32 const size = u32();
        qsizetype const next = at + ((size + 3) / 4) * 4;
        at += sub == 1 ? 47 : 301;      // its key and what goes with it
        int const top = int(qint32(u32())), left = int(qint32(u32())), bottom = int(qint32(u32())), right = int(qint32(u32()));
        int const depth = u16(), packed = u8();
        int const width = right - left, height = bottom - top;
        if (width <= 0 || height <= 0 || width > 5000 || height > 5000 || (depth != 8 && depth != 16)) {
            at = next;
            continue;
        }
        int const bytesPer = depth / 8;
        QByteArray pixels(qsizetype(width) * height * bytesPer, '\0');
        if (!packed) {
            pixels = bytes.mid(at, pixels.size());
        } else {
            QVector<int> lines(height);
            for (int y = 0; y < height; ++y)
                lines[y] = u16();
            qsizetype out = 0;
            for (int y = 0; y < height; ++y) {
                qsizetype const lineEnd = at + lines[y];
                while (at < lineEnd && at < bytes.size()) {
                    int n = u8();
                    if (n >= 128)
                        n -= 256;
                    if (n == -128)
                        continue;
                    if (n < 0) {
                        int const value = u8();
                        for (int c = 0; c < -n + 1 && out < pixels.size(); ++c)
                            pixels[out++] = char(value);
                    } else {
                        for (int c = 0; c < n + 1 && out < pixels.size(); ++c)
                            pixels[out++] = char(u8());
                    }
                }
                at = lineEnd;
            }
        }
        QImage image(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                qsizetype const k = (qsizetype(y) * width + x) * bytesPer;
                image.scanLine(y)[x] = k < pixels.size() ? uchar(pixels[k]) : 0;
            }
        tips.push_back(image);
        at = next;
    }
    if (tips.isEmpty() && why)
        *why = tr("The file has no sampled brushes.");
    return tips;
}

void Canvas::rasterize(Drawing& drawing, int sheet, Scene const& scene, double fineness, QRectF const& canvas)
{
    QVector<Stroke>& lines = drawing.strokesOf(sheet);
    QVector<Fill>& areas = drawing.fillsOf(sheet);
    if (lines.isEmpty() && areas.isEmpty())
        return;
    QRectF reach;
    for (Stroke const& line : std::as_const(lines))
        reach = reach.united(line.bounds().adjusted(-line.width, -line.width, line.width, line.width));
    for (Fill const& area : std::as_const(areas))
        reach = reach.united(area.bounds());
    // The pixels of that art layer: those it has, made bigger where the art reaches beyond them; or new ones.
    int at = -1;
    for (int i = 0; i < drawing.pictures.size() && at < 0; ++i)
        if (drawing.pictures[i].painted && drawing.pictures[i].sheet == sheet)
            at = i;
    if (at < 0) {
        QRectF const area = (canvas.isValid() ? canvas : QRectF(0.0, 0.0, scene.width, scene.height)).united(reach);
        double const fine = std::clamp(fineness, 0.25, 8.0);
        Picture pixels;
        pixels.painted = true;
        pixels.sheet = sheet;
        pixels.size = QSize(int(std::ceil(area.width() * fine)), int(std::ceil(area.height() * fine)));
        pixels.image = QImage(pixels.size, QImage::Format_ARGB32_Premultiplied);
        pixels.image.fill(Qt::transparent);
        pixels.placement = QTransform::fromScale(1.0 / fine, 1.0 / fine) * QTransform::fromTranslate(area.left(), area.top());
        at = int(drawing.pictures.size());
        drawing.pictures.push_back(pixels);
    }
    Picture& pixels = drawing.pictures[at];
    if (!pixels.placement.isInvertible())
        return;
    QImage image = pixels.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect const wanted = pixels.placement.inverted().mapRect(reach).toAlignedRect();
    if (!image.rect().contains(wanted)) {
        QRect const grown = image.rect().united(wanted);
        QImage bigger(grown.size(), QImage::Format_ARGB32_Premultiplied);
        bigger.fill(Qt::transparent);
        QPainter copy(&bigger);
        copy.drawImage(-grown.topLeft(), image);
        copy.end();
        image = bigger;
        pixels.placement = QTransform::fromTranslate(grown.left(), grown.top()) * pixels.placement;
    }
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setTransform(pixels.placement.inverted());
        // Areas under lines, each in the order of the art layer.
        QVector<Fill> sortedAreas = areas;
        std::stable_sort(sortedAreas.begin(), sortedAreas.end(), [](Fill const& a, Fill const& b) { return a.order < b.order; });
        QVector<Stroke> sortedLines = lines;
        std::stable_sort(sortedLines.begin(), sortedLines.end(), [](Stroke const& a, Stroke const& b) { return a.order < b.order; });
        for (Fill const& area : std::as_const(sortedAreas))
            paintFill(painter, area, scene.brushFor(area));
        for (Stroke const& line : std::as_const(sortedLines))
            paintStroke(painter, line, scene.brushFor(line));
    }
    pixels.image = image;
    pixels.size = image.size();
    lines.clear();
    areas.clear();
}

void Canvas::resampleBitmaps(Drawing& drawing, double factor)
{
    if (factor <= 0.0 || std::abs(factor - 1.0) < 1e-9)
        return;
    for (Picture& pixels : drawing.pictures) {
        if (!pixels.painted || pixels.image.isNull())
            continue;
        QSize const size(std::max(1, int(std::lround(pixels.image.width() * factor))), std::max(1, int(std::lround(pixels.image.height() * factor))));
        double const sx = double(pixels.image.width()) / size.width(), sy = double(pixels.image.height()) / size.height();
        pixels.image = pixels.image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        pixels.size = size;
        pixels.placement = QTransform::fromScale(sx, sy) * pixels.placement;
    }
}

void Canvas::onActionConvertToBitmap()
{
    if (!canEditDrawing())
        return;
    Drawing const* const real = realDrawing();
    Scene const& scene = m_document->scene();
    if (!real || m_layer < 0 || m_layer >= scene.layers.size()) {
        emit message(tr("The selection is not a bitmap drawing."));
        return;
    }
    Drawing whole = *real;
    int const sheet = sheetOf(m_art);
    Layer const& layer = scene.layers[m_layer];
    if (animatedSheet(layer, layer.drawingAt(m_frame), sheet)) {
        emit message(tr("Convert Deformed Drawings to Drawings before converting animated points to bitmap art."));
        return;
    }
    if (whole.strokesOf(sheet).isEmpty() && whole.fillsOf(sheet).isEmpty()) {
        emit message(tr("There is no vector art in this art layer to convert."));
        return;
    }
    rasterize(whole, sheet, scene, bitmapFineness(), bitmapArea());
    clearSelection();
    m_committing = true;
    keepWholeDrawing(whole, tr("Convert to Bitmap Drawing"));
    m_committing = false;
    update();
}

bool Canvas::vectorizeSheet(Drawing& drawing, int sheet)
{
    bool any = false;
    QVector<Picture> kept;
    for (Picture const& pixels : std::as_const(drawing.pictures)) {
        if (!pixels.painted || pixels.sheet != sheet || pixels.image.isNull()) {
            kept.push_back(pixels);
            continue;
        }
        any = true;
        // Over white paper, then each colour's areas as a shape, placed where the pixels were.
        QImage flat(pixels.image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        {
            QPainter p(&flat);
            p.drawImage(0, 0, pixels.image);
        }
        QTransform const placed = QTransform::fromScale(double(pixels.size.width()) / std::max(1, pixels.image.width()), double(pixels.size.height()) / std::max(1, pixels.image.height()))
                                  * pixels.placement;
        for (Vectorize::ColourShape const& area : Vectorize::traceColours(flat, 16, QColor(Qt::white))) {
            Fill shape;
            shape.color = area.colour;
            shape.swatch = -1;
            for (QPolygonF const& outline : area.outlines)
                shape.outlines.push_back(placed.map(outline));
            shape.giveContours(std::max(0.75, placed.m11()));
            drawing.fillsOf(sheet).push_back(shape);
        }
    }
    drawing.pictures = kept;
    return any;
}

void Canvas::onActionConvertToVector()
{
    if (!canEditDrawing())
        return;
    Drawing const* const real = realDrawing();
    if (!real) {
        emit message(tr("The selection is not a bitmap drawing."));
        return;
    }
    Drawing whole = *real;
    if (!vectorizeSheet(whole, sheetOf(m_art))) {
        emit message(tr("There is no bitmap art in this art layer to convert."));
        return;
    }
    clearSelection();
    m_committing = true;
    keepWholeDrawing(whole, tr("Convert to Vector Drawing"));
    m_committing = false;
    update();
}

void Canvas::onActionConvertToVectorLayer()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings())
        return;
    Layer const layer = scene.layers[m_layer];
    int const at = m_layer;
    m_document->beginMacro(tr("Convert to Vector Layer"));
    for (int index = 0; index < layer.drawings.size(); ++index) {
        Drawing whole = layer.drawings[index];
        bool any = false;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            any = vectorizeSheet(whole, sheet) || any;
        if (any)
            m_document->editLayers({at}, tr("Convert to Vector Layer"), [index, whole](Layer& l) {
                if (index < l.drawings.size())
                    l.drawings[index] = whole;
            });
    }
    m_document->setLayerBitmap(at, false);
    m_document->editLayers({at}, tr("Convert to Vector Layer"), [](Layer& l) { l.bitmapArt = 0; });
    m_document->endMacro();
    clearSelection();
    update();
}

void Canvas::onActionMatchArtLayerType()
{
    if (!canEditDrawing())
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const layer = scene.layers[m_layer];
    for (int drawing = 0; drawing < layer.drawings.size(); ++drawing)
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            if (layer.bitmapSheet(sheet) && animatedSheet(layer, drawing, sheet)) {
                emit message(tr("Convert Deformed Drawings to Drawings before converting animated points to bitmap art."));
                return;
            }
    int made = 0;
    m_document->beginMacro(tr("Convert Drawings to Match Art Layer Type"));
    QSet<int> done;
    for (int frame = 0; frame < layer.cells.size(); ++frame) {
        int const index = layer.cells[frame];
        if (index < 0 || index >= layer.drawings.size() || done.contains(index))
            continue;
        done.insert(index);
        Drawing whole = layer.drawings[index];
        bool any = false;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
            if (layer.bitmapSheet(sheet) && (!whole.strokesOf(sheet).isEmpty() || !whole.fillsOf(sheet).isEmpty())) {
                rasterize(whole, sheet, scene, bitmapFineness(), bitmapArea());
                any = true;
            } else if (!layer.bitmapSheet(sheet)) {
                // (A vector art layer: its pixels traced into shapes.)
                any = vectorizeSheet(whole, sheet) || any;
            }
        }
        if (any) {
            m_committing = true;
            m_document->setDrawing(m_layer, frame, whole, tr("Convert Drawings to Match Art Layer Type"));
            m_committing = false;
            ++made;
        }
    }
    m_document->endMacro();
    clearSelection();
    update();
    emit message(tr("%n drawing(s) converted.", nullptr, made));
}

void Canvas::onActionBitmapResolution()
{
    if (!canEditDrawing())
        return;
    Drawing const* const real = realDrawing();
    bool hasPixels = false;
    if (real)
        for (Picture const& pixels : real->pictures)
            hasPixels = hasPixels || pixels.painted;
    if (!hasPixels) {
        emit message(tr("The selection is not a bitmap drawing."));
        return;
    }
    bool ok = false;
    // How much finer (per cent of what it is now), and on this drawing or on every drawing of the layer.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Change Bitmap Drawing Resolution"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const density = new QSpinBox(&dialog);
    density->setRange(10, 800);
    density->setSingleStep(10);
    density->setValue(100);
    density->setSuffix(QStringLiteral(" %"));
    form->addRow(tr("By Percentage"), density);
    QCheckBox* const all = new QCheckBox(tr("Apply to All Drawings"), &dialog);
    form->addRow(all);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    ok = dialog.exec() == QDialog::Accepted;
    if (!ok || density->value() == 100)
        return;
    changeBitmapResolution(density->value() / 100.0, all->isChecked());
}

void Canvas::changeBitmapResolution(double factor, bool allDrawings)
{
    if (!canEditDrawing())
        return;
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    Layer const layer = scene.layers[m_layer];
    QVector<int> frames;
    if (allDrawings) {
        QSet<int> done;
        for (int frame = 0; frame < layer.cells.size(); ++frame)
            if (layer.cells[frame] >= 0 && !done.contains(layer.cells[frame])) {
                done.insert(layer.cells[frame]);
                frames.push_back(frame);
            }
    } else {
        frames = {m_frame};
    }
    m_document->beginMacro(tr("Change Bitmap Drawing Resolution"));
    for (int frame : std::as_const(frames)) {
        int const index = frame < layer.cells.size() ? layer.cells[frame] : -1;
        if (index < 0 || index >= layer.drawings.size())
            continue;
        Drawing whole = layer.drawings[index];
        resampleBitmaps(whole, factor);
        m_committing = true;
        m_document->setDrawing(m_layer, frame, whole, tr("Change Bitmap Drawing Resolution"));
        m_committing = false;
    }
    m_document->endMacro();
    update();
}
