// Eclipse - the Camera view: the cutter - cutting inside a shape, painting inside a shape; lasso.
#include "ui/canvas/Canvas.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numeric>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QSpinBox>

#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "model/scene/TextureBank.h"
#include "ui/canvas/PencilTexture.h"
#include "ui/canvas/TextureDialogs.h"

#include "model/document/Commands.h"
#include "model/geometry/FillRegion.h"

void Canvas::cutInside(QPolygonF const& shape)
{
    Drawing const* const d = currentDrawing();
    if (!d || shape.size() < 3) {
        clearSelection();
        return;
    }
    double const fine = std::max(0.25 / m_zoom, 0.05);
    // Everything but the shape: a sheet far larger than the drawing, with the shape a hole in it.
    QRectF all = shape.boundingRect();
    for (Fill const& fill : d->fills)
        all |= fill.bounds();
    for (Stroke const& stroke : d->strokes)
        all |= stroke.bounds();
    all.adjust(-1000, -1000, 1000, 1000);
    QVector<QPolygonF> const within{shape};
    QVector<QPolygonF> const without{QPolygonF(all), shape};
    // The ends the cut makes on pencil lines are left as the Cutter's tip says.
    auto tipped = [this](QVector<Stroke> pieces, Stroke const& whole) {
        if (whole.points.isEmpty() || whole.width <= 0.0f)
            return pieces;
        QPointF const first = whole.points.first().pos, last = whole.points.last().pos;
        auto made = [&](QPointF p) { return whole.closed || (QLineF(p, first).length() > 1e-3 && QLineF(p, last).length() > 1e-3); };
        for (Stroke& piece : pieces) {
            if (piece.points.isEmpty())
                continue;
            if (made(piece.points.first().pos))
                piece.capStart = Stroke::Cap(m_cutterTip);
            if (made(piece.points.last().pos))
                piece.capEnd = Stroke::Cap(m_cutterTip);
        }
        return pieces;
    };
    Drawing art = *d;
    art.fills.clear();
    art.strokes.clear();
    QVector<int> fills, strokes;
    bool cut = false;
    for (Fill const& fill : d->fills) {
        bool lost = false;
        QVector<Fill> const inside = fill.minus(without, fine, &lost);
        if (!lost) {                    // none of it is outside: taken whole
            fills.push_back(int(art.fills.size()));
            art.fills.push_back(fill);
        } else if (inside.isEmpty()) {  // all of it is outside
            art.fills.push_back(fill);
        } else {
            art.fills += fill.minus(within, fine);
            for (Fill const& piece : inside) {
                fills.push_back(int(art.fills.size()));
                art.fills.push_back(piece);
            }
            cut = true;
        }
    }
    for (Stroke const& stroke : d->strokes) {
        bool lost = false;
        QVector<Stroke> const inside = tipped(stroke.minus(without, fine, &lost), stroke);
        if (!lost) {
            strokes.push_back(int(art.strokes.size()));
            art.strokes.push_back(stroke);
        } else if (inside.isEmpty()) {
            art.strokes.push_back(stroke);
        } else {
            art.strokes += tipped(stroke.minus(within, fine), stroke);
            for (Stroke const& piece : inside) {
                strokes.push_back(int(art.strokes.size()));
                art.strokes.push_back(piece);
            }
            cut = true;
        }
    }
    QVector<int> pictures;
    if (picturesHere()) {
        int const count = int(art.pictures.size());
        for (int i = 0; i < count; ++i) {
            Picture const whole = art.pictures[i];
            if (whole.image.isNull() || !whole.placement.isInvertible() || !whole.bounds().intersects(shape.boundingRect()) || !pictureHere(whole))
                continue;
            QRectF const frame(QPointF(0, 0), QSizeF(whole.image.size()));
            QPolygonF const corners = whole.placement.map(QPolygonF(frame));
            bool allIn = true;
            for (int c = 0; c < 4; ++c)
                allIn = allIn && shape.containsPoint(corners[c], Qt::WindingFill);
            if (allIn) {                // none of it is outside: taken whole
                pictures.push_back(i);
                continue;
            }
            if (whole.linked)
                continue;               // (a linked picture is what its file is)
            QPainterPath hole;
            hole.setFillRule(Qt::WindingFill);
            hole.addPolygon(whole.placement.inverted().map(shape));
            QRect const reach = hole.boundingRect().toAlignedRect().intersected(whole.image.rect());
            if (reach.isEmpty())
                continue;
            // The piece: only what is inside the shape, cropped to it.
            QImage piece = whole.image.copy(reach).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            {
                QPainter painter(&piece);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.translate(-reach.topLeft());
                painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                painter.fillPath(hole, Qt::black);
            }
            bool any = false;
            for (int y = 0; y < piece.height() && !any; ++y) {
                QRgb const* const row = reinterpret_cast<QRgb const*>(piece.constScanLine(y));
                for (int x = 0; x < piece.width() && !any; ++x)
                    any = qAlpha(row[x]) != 0;
            }
            if (!any)
                continue;
            QImage rest = whole.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            {
                QPainter painter(&rest);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                painter.fillPath(hole, Qt::black);
            }
            art.pictures[i].image = rest;
            Picture loose = whole;
            loose.image = piece;
            loose.size = piece.size();
            loose.file.clear();
            loose.placement = QTransform::fromTranslate(reach.left(), reach.top()) * whole.placement;
            pictures.push_back(int(art.pictures.size()));
            art.pictures.push_back(loose);
            cut = true;
        }
    }
    if (cut)
        commitDrawing(art, tr("Cut Artwork"));
    setSelection(strokes, fills, pictures);
}

void Canvas::paintInside(std::function<bool(QPointF)> const& within, QRectF const& reach)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    QColor const color = m_document->scene().resolve(m_swatch, Qt::black);
    Drawing edited;
    edited.pictures = d->pictures;
    edited.pinned = d->pinned;
    bool any = false;
    for (Stroke stroke : d->strokes) {
        bool inside = false;
        for (StrokePoint const& point : stroke.points)
            inside = inside || within(point.pos);
        // Lines take the colour from Paint and Repaint; they have one already, and are never unpainted.
        inside = inside && !keepsColour(stroke.swatch);      // a protected colour is left as it is
        if (inside && (m_tool == Paint || m_tool == Repaint) && (stroke.swatch != m_swatch || stroke.color != color)) {
            stroke.swatch = m_swatch;
            stroke.color = color;
            any = true;
        }
        edited.strokes.push_back(stroke);
    }
    for (Fill fill : d->fills) {
        bool inside = false;
        for (QPolygonF const& outline : fill.outlines)
            for (QPointF const& q : outline)
                inside = inside || within(q);
        inside = inside && !keepsColour(fill.swatch);
        if (inside && m_tool == Unpaint) {
            any = true;
            continue;       // the paint goes
        }
        if (inside && (m_tool == Paint || m_tool == Repaint) && (fill.swatch != m_swatch || fill.color != color)) {
            fill.swatch = m_swatch;
            fill.color = color;
            any = true;
        }
        edited.fills.push_back(fill);
    }
    if (m_tool == Paint || m_tool == PaintUnpainted) {
        int const across = 6;
        for (int row = 0; row < across; ++row) {
            for (int column = 0; column < across; ++column) {
                QPointF const at(reach.left() + reach.width() * (column + 0.5) / across, reach.top() + reach.height() * (row + 0.5) / across);
                if (!within(at))
                    continue;
                bool painted = false;
                for (Fill const& fill : std::as_const(edited.fills))
                    painted = painted || fill.contains(at);
                if (painted)
                    continue;
                Drawing bounded = edited;
                if (m_art != LineArt && !bothArtsNow() && realDrawing()) {
                    bounded.strokes += realDrawing()->strokes;
                    for (Fill const& shape : realDrawing()->fills)
                        if (!shape.contours.isEmpty())
                            bounded.fills.push_back(shape);
                }
                QVector<QPolygonF> outlines;
                if (FillRegion::compute(bounded, FillRegion::paintArea(bounded), at, m_gapSize, outlines) != FillRegion::Ok || outlines.isEmpty())
                    continue;
                underShapes(outlines, bounded);
                QRectF area;
                for (QPolygonF const& outline : std::as_const(outlines))
                    area |= outline.boundingRect();
                if (!reach.adjusted(-1, -1, 1, 1).contains(area))
                    continue;       // it runs on outside what was drawn round: left alone
                Fill fill;
                fill.outlines = outlines;
                fill.swatch = m_swatch;
                fill.color = color;
                int const under = underIndex(edited);
                edited.fills.insert(under, fill);
                any = true;
            }
        }
    }
    if (any)
        commitDrawing(edited, m_tool == Unpaint ? tr("Unpaint") : tr("Paint"));
}

void Canvas::setLasso(bool on)
{
    if (on == m_lasso)
        return;
    m_lasso = on;
    emit lassoChanged(on);
}
