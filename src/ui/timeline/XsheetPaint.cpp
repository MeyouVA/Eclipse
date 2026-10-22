// Eclipse - the Xsheet: painting the cells, the frame numbers and the column headers.
#include "ui/timeline/Xsheet.h"

#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include "actions/theme/InterfaceColours.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

namespace {

QColor mixed(QColor const& under, QColor const& over, double amount)
{
    return QColor::fromRgbF(under.redF() + (over.redF() - under.redF()) * amount, under.greenF() + (over.greenF() - under.greenF()) * amount,
                            under.blueF() + (over.blueF() - under.blueF()) * amount);
}

QColor const kText(210, 210, 210), kLine(48, 48, 48), kHeader(58, 58, 58), kPegColumn(74, 74, 74), kCurrentRow(100, 100, 100);

}  // namespace

QColor Xsheet::columnColour(Layer const& layer, int function) const
{
    if (layer.trackColor.isValid())
        return layer.trackColor;
    if (function >= 0) {
        for (Driver const& d : layer.drivers)
            if (d.value == function)
                return InterfaceColours::get("xsheetExpressionColumn");
        return InterfaceColours::get("xsheetFunctionColumn");
    }
    switch (layer.kind) {
    case Layer::DrawingLayer:
        return layer.cloneGroup != 0 && !layer.cloneTiming ? InterfaceColours::get("xsheetTimingColumn") : InterfaceColours::get("xsheetDrawingColumn");
    case Layer::Annotation: return InterfaceColours::get("xsheetAnnotationColumn");
    case Layer::Sound: return InterfaceColours::get("nodeSound");
    case Layer::Peg: return InterfaceColours::get("nodePeg");
    case Layer::Camera: return InterfaceColours::get("nodeCamera");
    case Layer::Group: return InterfaceColours::get("nodeGroup");
    default: return InterfaceColours::get("xsheetDrawingColumn");
    }
}

QString Xsheet::functionText(int layer, int function, int frame) const
{
    Scene const& scene = m_document->scene();
    if (layer < 0 || layer >= scene.layers.size() || function < 0)
        return QString();
    Layer const& l = scene.layers[layer];
    double value = 0.0;
    bool driven = false;
    for (Driver const& d : l.drivers)
        driven = driven || d.value == function;
    if (driven)
        value = Driver::read(scene.poseOf(layer, frame), function);
    else if (function == Timeline::Fov)
        value = l.fovAt(frame);
    else
        value = Timeline::parameterOf(l.poseAt(frame), Timeline::Parameter(function));
    if (function == Timeline::Opacity)
        value *= 100.0;
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

void Xsheet::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter p(viewport());
    int const w = viewport()->width();
    int const h = viewport()->height();
    int const sx = horizontalScrollBar()->value();
    int const sy = verticalScrollBar()->value();
    int const rh = rowHeight(), hh = headerHeight();
    QColor const accent = InterfaceColours::get("xsheetCurrentFrame");
    p.fillRect(0, 0, w, h, InterfaceColours::get("xsheetBackground"));

    int const firstRow = std::max(0, sy / rh);
    int const lastRow = std::min(scene.frameCount() - 1, (sy + h - hh) / rh);

    // ---- cells ----
    p.save();
    p.setClipRect(kRowHeaderWidth, hh, w - kRowHeaderWidth, h - hh);
    for (Column const& column : std::as_const(m_columns)) {
        int const x = kRowHeaderWidth + column.x - sx;
        if (x + column.width < kRowHeaderWidth || x > w || column.layer < 0 || column.layer >= scene.layers.size())
            continue;
        if (column.function >= 0)
            paintFunctionCells(p, column, x, firstRow, lastRow);
        else if (scene.layers[column.layer].kind == Layer::Annotation)
            paintAnnotation(p, column, x, firstRow, lastRow);
        else
            paintCells(p, column, x, firstRow, lastRow);
        p.setPen(kLine);
        p.drawLine(x + column.width - 1, hh, x + column.width - 1, h);
    }
    paintTempo(p, firstRow, lastRow);
    paintSheetMarkers(p);
    // the frames selected
    if (hasSelection()) {
        QVector<int> const across = selectedColumns();
        if (!across.isEmpty()) {
            Column const& a = m_columns[across.first()];
            Column const& b = m_columns[across.last()];
            QRect const block(kRowHeaderWidth + a.x - sx, hh + selectionFirstFrame() * rh - sy, b.x + b.width - a.x,
                              (selectionLastFrame() - selectionFirstFrame() + 1) * rh);
            p.fillRect(block, InterfaceColours::get("timelineSelection"));
            p.setPen(QPen(QColor(120, 220, 235), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(block.adjusted(0, 0, -2, -1));
            if (m_gestural) {
                // The handle on its left edge: drag it to move the block.
                p.fillRect(block.x(), block.y(), 4, block.height(), QColor(120, 220, 235, 150));
            }
        }
    }
    // a block being dragged
    if (m_drag == DragBlock) {
        QVector<int> const across = selectedColumns();
        int const first = std::clamp(across.isEmpty() ? m_column : across.first() + m_dragRows, 0, int(m_columns.size()) - 1);
        int const last = std::clamp(across.isEmpty() ? m_column : across.last() + m_dragRows, 0, int(m_columns.size()) - 1);
        if (first >= 0 && last >= 0) {
            QRect const to(kRowHeaderWidth + m_columns[first].x - sx, hh + (selectionFirstFrame() + m_dragFrames) * rh - sy,
                           m_columns[last].x + m_columns[last].width - m_columns[first].x, (selectionLastFrame() - selectionFirstFrame() + 1) * rh);
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(to.adjusted(0, 0, -2, -1));
        }
    }
    // the current cell
    if (m_column >= 0 && m_column < m_columns.size()) {
        QRect const cell = columnCellRect(m_column, m_frame);
        p.setPen(QPen(m_insert ? QColor(255, 210, 90) : QColor(Qt::white), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cell.adjusted(0, 0, -2, -2));
    }
    p.restore();

    // ---- frame numbers ----
    p.fillRect(0, 0, kRowHeaderWidth, h, kHeader);
    p.save();
    p.setClipRect(0, hh, kRowHeaderWidth, h - hh);
    QFont small = p.font();
    if (m_feet)
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1.0));
    p.setFont(small);
    for (int f = firstRow; f <= lastRow; ++f) {
        int const y = hh + f * rh - sy;
        if (f == m_frame)
            p.fillRect(0, y, kRowHeaderWidth, rh, QColor(accent.red(), accent.green(), accent.blue(), 150));
        if (int const m = scene.markerAt(f); m >= 0)
            p.fillRect(0, y, 3, rh, scene.markers[m].color);
        p.setPen(kText);
        if (rh >= 10 || f % 2 == 0)
            p.drawText(QRect(0, y, kRowHeaderWidth - 6, rh), Qt::AlignVCenter | Qt::AlignRight, frameLabel(f));
        p.setPen(m_feet && (f + 1) % 16 == 0 ? QColor(150, 150, 150) : kLine);
        p.drawLine(0, y + rh - 1, kRowHeaderWidth, y + rh - 1);
    }
    p.restore();

    // ---- column headers ----
    paintHeaders(p);
    p.fillRect(0, 0, kRowHeaderWidth, hh, kHeader);
    if (m_insert) {
        p.setPen(QColor(255, 210, 90));
        p.drawText(QRect(0, 0, kRowHeaderWidth, hh), Qt::AlignCenter, tr("Ins"));
    }
    p.setPen(kLine);
    p.drawLine(kRowHeaderWidth - 1, 0, kRowHeaderWidth - 1, h);
    p.drawLine(0, hh - 1, w, hh - 1);
}

void Xsheet::paintCells(QPainter& p, Column const& column, int x, int firstRow, int lastRow)
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[column.layer];
    int const rh = rowHeight(), hh = headerHeight(), sy = verticalScrollBar()->value();
    int const cw = column.width;
    QColor const paper = InterfaceColours::get("xsheetPaper"), paperAlt = paper.darker(106);
    QColor const exposed = mixed(InterfaceColours::get("xsheetExposed"), columnColour(layer, -1), layer.trackColor.isValid() ? 0.35 : 0.12);
    QColor const keyColor = InterfaceColours::get("xsheetKeyframe"), stopColor = InterfaceColours::get("timelineStopKeyframe");
    QColor const currentDrawing = InterfaceColours::get("xsheetCurrentDrawing"), onionColour = InterfaceColours::get("xsheetOnionSkin");
    QColor const lightTable = InterfaceColours::get("xsheetLightTable");
    bool const current = column.layer == m_layer;
    int const shownHere = current ? layer.drawingAt(m_frame) : Layer::kNoDrawing;
    bool const collapsed = layer.columnCollapsed;
    // How long the exposure each frame belongs to is (for the threshold of the held line).
    auto runLength = [&layer](int f) {
        int const start = layer.exposureStart(f);
        if (start < 0)
            return 0;
        int const d = layer.drawingAt(start);
        int end = start + 1;
        while (end < layer.cells.size() && layer.cells[end] == d && !layer.isKey(end))
            ++end;
        return end - start;
    };
    for (int f = firstRow; f <= lastRow; ++f) {
        int const y = hh + f * rh - sy;
        QRect const cell(x, y, cw, rh);
        int const drawing = layer.drawingAt(f);
        QColor fill = !layer.holdsDrawings() ? kPegColumn : (f % 2 ? paperAlt : paper);
        if (f == m_frame)
            fill = kCurrentRow;
        if (drawing != Layer::kNoDrawing) {
            fill = f == m_frame ? exposed.lighter(125) : exposed;
            if (current && drawing == shownHere && f != m_frame)
                fill = mixed(fill, currentDrawing, currentDrawing.alphaF() * 0.6);
            if (current && m_onion && f != m_frame && f >= m_frame - m_onionPrevious && f <= m_frame + m_onionNext && drawing != shownHere)
                fill = mixed(fill, onionColour, 0.35);
            if (m_lightTable && !current && f == m_frame && layer.holdsDrawings())
                fill = mixed(fill, lightTable, 0.35);
        }
        p.fillRect(cell, fill);
        if (!collapsed && drawing != Layer::kNoDrawing) {
            p.setPen(kText);
            if (layer.isKey(f)) {
                p.drawText(cell.adjusted(6, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, layer.drawingLabel(drawing));
                // Its mark: K for a key drawing, B for a breakdown, I for an in-between.
                int const mark = drawing < layer.drawings.size() ? layer.drawings[drawing].mark : Drawing::NoMark;
                if (mark != Drawing::NoMark) {
                    p.setPen(InterfaceColours::markColour(mark, Drawing::markColour(Drawing::NoMark)));
                    p.drawText(cell.adjusted(6, 0, -22, 0), Qt::AlignVCenter | Qt::AlignRight,
                               mark == Drawing::KeyMark ? QStringLiteral("K") : mark == Drawing::BreakdownMark ? QStringLiteral("B") : QStringLiteral("I"));
                    p.setPen(kText);
                }
            } else if (layer.morphing(f)) {
                // In a morphing: the drawing here is made from the two either side of it.
                p.setPen(QPen(QColor(170, 215, 120), 2));
                p.drawLine(x + 12, y, x + 12, y + rh - 1);
                p.setPen(QPen(QColor(170, 215, 120), 1));
                p.drawLine(x + 8, y + rh / 2 - 2, x + 12, y + rh / 2 + 2);
                p.drawLine(x + 16, y + rh / 2 - 2, x + 12, y + rh / 2 + 2);
            } else if (m_heldLook == HeldValue) {
                p.setPen(QColor(150, 150, 150));
                p.drawText(cell.adjusted(6, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, layer.drawingLabel(drawing));
            } else if (m_lineThreshold <= 0 || runLength(f) > m_lineThreshold) {
                p.drawLine(x + 12, y, x + 12, y + rh - 1);  // held
            }
        }
        if (int const k = layer.keyIndex(f); k >= 0 && !collapsed) {
            double const kx = x + cw - 9.0, ky = y + rh / 2.0, r = std::min(4.0, rh / 3.0);
            QPolygonF diamond;
            diamond << QPointF(kx, ky - r) << QPointF(kx + r, ky) << QPointF(kx, ky + r) << QPointF(kx - r, ky);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(layer.keys[k].tween ? keyColor : stopColor);
            p.drawPolygon(diamond);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        // A sound column: the shape of the mouth on the frame, when it has lip-sync.
        if (layer.kind == Layer::Sound && !layer.mouthAt(f).isNull() && !collapsed) {
            QChar const shape = layer.mouthAt(f);
            p.setPen(shape == QLatin1Char('X') ? QColor(140, 140, 140) : (f == 0 || layer.mouthAt(f - 1) != shape) ? keyColor : kText);
            p.drawText(cell, Qt::AlignCenter, QString(shape));
        }
        if (m_grid) {
            p.setPen(kLine);
            p.drawLine(x, y + rh - 1, x + cw - 1, y + rh - 1);
        }
    }
}

void Xsheet::paintFunctionCells(QPainter& p, Column const& column, int x, int firstRow, int lastRow)
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[column.layer];
    int const rh = rowHeight(), hh = headerHeight(), sy = verticalScrollBar()->value();
    QColor const base = mixed(kPegColumn, columnColour(layer, column.function), 0.18);
    QColor const keyColor = InterfaceColours::get("xsheetKeyframe");
    bool driven = false;
    for (Driver const& d : layer.drivers)
        driven = driven || d.value == column.function;
    QFont bold = p.font();
    bold.setBold(true);
    QFont const plain = p.font();
    for (int f = firstRow; f <= lastRow; ++f) {
        int const y = hh + f * rh - sy;
        QRect const cell(x, y, column.width, rh);
        p.fillRect(cell, f == m_frame ? kCurrentRow : (f % 2 ? base.darker(106) : base));
        int const k = layer.keyIndex(f);
        bool const keyed = k >= 0 && layer.keys[k].sets(1 << column.function);
        if (!layer.columnCollapsed && (rh >= 10 || keyed)) {
            p.setFont(keyed ? bold : plain);
            p.setPen(driven ? InterfaceColours::get("xsheetExpressionColumn").lighter(160) : keyed ? kText : QColor(150, 150, 150));
            p.drawText(cell.adjusted(4, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, functionText(column.layer, column.function, f));
        }
        if (keyed && !layer.columnCollapsed) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(layer.keys[k].tween ? keyColor : InterfaceColours::get("timelineStopKeyframe"));
            double const r = std::min(3.0, rh / 4.0);
            p.drawEllipse(QPointF(x + column.width - 6.0, y + rh / 2.0), r, r);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        if (m_grid) {
            p.setPen(kLine);
            p.drawLine(x, y + rh - 1, x + column.width - 1, y + rh - 1);
        }
    }
    p.setFont(plain);
}

void Xsheet::paintAnnotation(QPainter& p, Column const& column, int x, int firstRow, int lastRow)
{
    Scene const& scene = m_document->scene();
    Layer const& layer = scene.layers[column.layer];
    int const rh = rowHeight(), hh = headerHeight(), sy = verticalScrollBar()->value();
    int const cw = column.width;
    QColor const paper = columnColour(layer, -1);
    QColor const line = paper.darker(118);
    for (int f = firstRow; f <= lastRow; ++f) {
        int const y = hh + f * rh - sy;
        p.fillRect(x, y, cw, rh, f == m_frame ? paper.darker(125) : paper);
        // A small tick on the odd frames (1, 3, 5 ...).
        if (m_annotationMarks && f % 2 == 0) {
            p.setPen(line.darker(130));
            p.drawLine(x, y + rh / 2, x + 4, y + rh / 2);
        }
        if (m_grid) {
            p.setPen(line);
            p.drawLine(x, y + rh - 1, x + cw - 1, y + rh - 1);
        }
    }
    if (layer.columnCollapsed)
        return;
    QRect const visible(x, hh + firstRow * rh - sy, cw, (lastRow - firstRow + 1) * rh);
    p.save();
    p.setClipRect(visible, Qt::IntersectClip);
    // Pictures, under the rest.
    for (AnnotationPicture const& picture : layer.pictures) {
        QRect const box(x + 1, hh + picture.frame * rh - sy, cw - 2, picture.frames * rh);
        if (!box.intersects(visible))
            continue;
        QImage image;
        if (!image.loadFromData(picture.png) || image.isNull())
            continue;
        QSize const fit = image.size().scaled(box.size(), Qt::KeepAspectRatio);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_annotationHardness > 0.0);
        p.drawImage(QRect(box.topLeft(), fit), image);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    // Notes: from their frame down to the next one.
    QFont font = p.font();
    font.setPointSizeF(std::max(6.0, font.pointSizeF() * std::min(1.0, rh / 18.0)));
    p.setFont(font);
    p.setPen(QColor(30, 30, 30));
    for (auto it = layer.notes.constBegin(); it != layer.notes.constEnd(); ++it) {
        auto const next = std::next(it);
        int const end = next == layer.notes.constEnd() ? scene.frameCount() : next.key();
        QRect const box(x + 3, hh + it.key() * rh - sy + 1, cw - 6, (end - it.key()) * rh - 2);
        if (box.intersects(visible))
            p.drawText(box, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, it.value());
    }
    // Pen marks.
    p.setRenderHint(QPainter::Antialiasing, true);
    double const scale = rh / 18.0;
    auto stroke = [&](QVector<QPointF> const& points, QColor const& colour, double width) {
        if (points.isEmpty())
            return;
        QPainterPath path;
        path.moveTo(x + points[0].x() * cw, hh + points[0].y() * rh - sy);
        for (int i = 1; i < points.size(); ++i)
            path.lineTo(x + points[i].x() * cw, hh + points[i].y() * rh - sy);
        p.setPen(QPen(colour, std::max(0.5, width * scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        if (points.size() == 1)
            p.drawPoint(path.currentPosition());
        else
            p.drawPath(path);
    };
    for (AnnotationInk const& mark : layer.ink)
        stroke(mark.points, mark.colour, mark.width);
    if (m_drag == DragInk && column.layer == m_layer)
        stroke(m_ink, m_penColour, m_penWidth);
    p.restore();
}

void Xsheet::paintTempo(QPainter& p, int firstRow, int lastRow)
{
    Scene const& scene = m_document->scene();
    if (scene.framesPerBeat <= 0)
        return;
    int const rh = rowHeight(), hh = headerHeight(), sy = verticalScrollBar()->value();
    int const right = kRowHeaderWidth + m_contentWidth - horizontalScrollBar()->value();
    QColor const beat = InterfaceColours::get("xsheetFrameBeat"), bar = InterfaceColours::get("xsheetBeatBar");
    for (int f = firstRow; f <= lastRow; ++f) {
        if ((f + 1) % scene.framesPerBeat != 0)
            continue;
        bool const onBar = scene.beatsPerBar > 0 && (f + 1) % (scene.framesPerBeat * scene.beatsPerBar) == 0;
        int const y = hh + (f + 1) * rh - sy - 1;
        p.setPen(QPen(onBar ? bar : beat, onBar ? 2 : 1));
        p.drawLine(kRowHeaderWidth, y, right, y);
    }
}

void Xsheet::paintSheetMarkers(QPainter& p)
{
    Scene const& scene = m_document->scene();
    int const rh = rowHeight(), hh = headerHeight(), sy = verticalScrollBar()->value();
    for (int i = 0; i < scene.sheetMarkers.size(); ++i) {
        Scene::SheetMarker const& m = scene.sheetMarkers[i];
        int const column = columnOfLayer(scene.layerIndex(m.layer));
        if (column < 0)
            continue;
        int frame = m.frame, length = m.length;
        if ((m_drag == DragMarker || m_drag == DragMarkerEnd) && m_dragMarker == i) {
            frame = m_markerFrame;
            length = m_markerLength;
        }
        QRect const top = columnCellRect(column, frame);
        int const x = top.x() + 3, y0 = top.y() + 2, y1 = hh + (frame + length) * rh - sy - 3;
        QColor const colour = m.colour == Scene::SheetMarker::White ? QColor(245, 245, 245)
                              : m.colour == Scene::SheetMarker::Grey ? QColor(150, 150, 150) : QColor(10, 10, 10);
        p.setPen(QPen(colour, 3, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(x, y0, x, y1);
        p.setPen(QPen(colour, 2, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(x, y0, x + 7, y0);
        p.drawLine(x, y1, x + 7, y1);
    }
}

namespace {

QPixmap headerPicture(Scene const& scene, Drawing const& drawing, QSize size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QRectF const bounds = drawing.bounds();
    if (bounds.isEmpty() || size.width() < 6 || size.height() < 6)
        return pixmap;
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    double const scale = std::min((size.width() - 4) / bounds.width(), (size.height() - 4) / bounds.height());
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    Canvas::paintDrawing(painter, scene, drawing, 1.0);
    return pixmap;
}

}  // namespace

void Xsheet::paintHeaders(QPainter& p)
{
    Scene const& scene = m_document->scene();
    int const w = viewport()->width();
    int const hh = headerHeight();
    int const sx = horizontalScrollBar()->value();
    int const band = m_grouping ? 16 : 0;
    p.fillRect(kRowHeaderWidth, 0, w - kRowHeaderWidth, hh, kHeader);
    p.save();
    p.setClipRect(kRowHeaderWidth, 0, w - kRowHeaderWidth, hh);
    QFont const normal = p.font();
    QFont small = normal;
    small.setPointSizeF(std::max(6.0, normal.pointSizeF() - 1.5));
    // The groups the columns are in, over them.
    if (m_grouping) {
        auto groupOf = [&scene](int l) -> QString {
            for (int at = scene.layerIndex(scene.layers[l].parent); at >= 0; at = scene.layerIndex(scene.layers[at].parent))
                if (scene.layers[at].kind == Layer::Group)
                    return scene.layers[at].name;
            return tr("Top");
        };
        int c = 0;
        while (c < m_columns.size()) {
            QString const name = groupOf(m_columns[c].layer);
            int end = c + 1;
            while (end < m_columns.size() && groupOf(m_columns[end].layer) == name)
                ++end;
            QRect const span(kRowHeaderWidth + m_columns[c].x - sx, 0, m_columns[end - 1].x + m_columns[end - 1].width - m_columns[c].x, band);
            p.fillRect(span.adjusted(0, 0, -1, -1), QColor(48, 64, 70));
            p.setPen(kText);
            p.setFont(small);
            p.drawText(span.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(name, Qt::ElideRight, span.width() - 8));
            c = end;
        }
    }
    for (int c = 0; c < m_columns.size(); ++c) {
        Column const& column = m_columns[c];
        int const x = kRowHeaderWidth + column.x - sx;
        if (x + column.width < kRowHeaderWidth || x > w || column.layer < 0 || column.layer >= scene.layers.size())
            continue;
        Layer const& layer = scene.layers[column.layer];
        QRect const box(x, band, column.width, hh - band);
        if (c == m_column)
            p.fillRect(box, kCurrentRow);
        else if (column.layer == m_layer)
            p.fillRect(box, QColor(80, 80, 80));
        p.fillRect(x, band, column.width - 1, 4, columnColour(layer, column.function));
        if (!layer.columnCollapsed) {
            int const nameHeight = m_minimal ? hh - band - 4 : 16;
            QString const name = column.function >= 0 ? Timeline::parameterName(Timeline::Parameter(column.function)).section(QLatin1String(": "), -1)
                                                      : layer.name;
            p.setFont(normal);
            p.setPen(column.function >= 0 ? QColor(190, 190, 190) : layer.kind == Layer::Peg ? InterfaceColours::get("xsheetKeyframe")
                     : layer.kind == Layer::Camera ? QColor(111, 168, 232) : (layer.visible ? kText : QColor(130, 130, 130)));
            p.drawText(QRect(x + 4, band + 4, column.width - 8, nameHeight), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(name, Qt::ElideRight, column.width - 8));
            if (!m_minimal) {
                static char const* const kKinds[] = {QT_TR_NOOP("Drawing"), QT_TR_NOOP("Peg"), QT_TR_NOOP("Camera"), QT_TR_NOOP("Sound"),
                                                     QT_TR_NOOP("Colour-Card"), QT_TR_NOOP("Group"), QT_TR_NOOP("Bone"), QT_TR_NOOP("Quadmap"),
                                                     QT_TR_NOOP("Annotation")};
                QString kind = column.function >= 0 ? layer.name : tr(kKinds[layer.kind]);
                if (column.function < 0 && layer.holdsDrawings() && layer.cloneGroup != 0 && !layer.cloneTiming)
                    kind = tr("Timing");
                p.setFont(small);
                p.setPen(QColor(160, 160, 160));
                p.drawText(QRect(x + 4, band + 4 + nameHeight, column.width - 8, 13), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(kind, Qt::ElideRight, column.width - 8));
                // The drawing on the current frame, over the column.
                if (m_thumbnails && column.function < 0 && layer.holdsDrawings()) {
                    QRect const pic(x + 4, band + 4 + nameHeight + 14, column.width - 9, 40);
                    int const d = layer.drawingAt(m_frame);
                    if (d != Layer::kNoDrawing && d < layer.drawings.size() && pic.width() > 8)
                        p.drawPixmap(pic.topLeft(), headerPicture(scene, layer.drawings[d], pic.size()));
                    else
                        p.fillRect(pic, QColor(70, 70, 70));
                    p.setPen(QColor(40, 40, 40));
                    p.drawRect(pic.adjusted(0, 0, -1, -1));
                }
            }
        }
        p.setPen(kLine);
        p.drawLine(x + column.width - 1, band, x + column.width - 1, hh);
    }
    // A column being dragged: where it would go.
    if (m_drag == DragColumn && m_dragTarget >= 0) {
        int const at = m_dragTarget < m_columns.size() ? m_columns[m_dragTarget].x : m_contentWidth;
        p.fillRect(kRowHeaderWidth + at - sx - 1, 0, 3, hh, QColor(120, 220, 235));
    }
    p.setFont(normal);
    p.restore();
}
