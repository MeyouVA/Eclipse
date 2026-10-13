// Eclipse - the Camera view: the Cutter and painting inside a lasso; the line style of what is selected.
#include "ui/canvas/Canvas.h"

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

// The selected lines - or the one the contour editor shows - restyled as one undo step.
template <class Change> void Canvas::restyleSelection(QString const& text, Change change)
{
    Drawing const* const d = currentDrawing();
    if (!d)
        return;
    QVector<int> which = selecting() ? m_selStrokes : QVector<int>();
    if (m_tool == ContourEditor && m_ceStroke >= 0 && m_ceStroke < d->strokes.size())
        which.push_back(m_ceStroke);
    if (which.isEmpty())
        return;
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(which)) {
        Stroke const before = edited.strokes[i];
        change(edited.strokes[i]);
        changed = changed || before.join != edited.strokes[i].join || before.capStart != edited.strokes[i].capStart
                  || before.capEnd != edited.strokes[i].capEnd || before.width != edited.strokes[i].width
                  || before.texture != edited.strokes[i].texture || before.textureAmount != edited.strokes[i].textureAmount
                  || before.textureCrisp != edited.strokes[i].textureCrisp || before.textureLow != edited.strokes[i].textureLow
                  || before.textureHigh != edited.strokes[i].textureHigh;
    }
    if (changed)
        commitDrawing(edited, text);
    update();
}

void Canvas::setJoinStyle(int join)
{
    m_join = Stroke::Join(std::clamp(join, 0, 2));
    restyleSelection(tr("Change Line Join"), [this](Stroke& s) { s.join = m_join; });
}

void Canvas::setStartCap(int cap)
{
    m_capStart = Stroke::Cap(std::clamp(cap, 0, 1));
    restyleSelection(tr("Change Line Start"), [this](Stroke& s) { s.capStart = m_capStart; });
}

void Canvas::setEndCap(int cap)
{
    m_capEnd = Stroke::Cap(std::clamp(cap, 0, 1));
    restyleSelection(tr("Change Line End"), [this](Stroke& s) { s.capEnd = m_capEnd; });
}

void Canvas::textureNewLine(Stroke& stroke) const
{
    if (m_tool == StrokeTool || m_tool == CloseGap)
        return;
    if (m_tool == Brush) {
        // (The brush drawing centre lines: its own texture, laid along the line.)
        stroke.texture = m_brushTexture;
        stroke.textureAmount = float(m_brushTextureAmount) / 100.0f;
        stroke.textureCrisp = m_brushTextureCrisp;
        stroke.textureLow = float(m_brushRampLow) / 100.0f;
        stroke.textureHigh = float(m_brushRampHigh) / 100.0f;
        return;
    }
    stroke.texture = m_lineTexture;
    stroke.textureAmount = float(m_lineTextureAmount) / 100.0f;
    stroke.textureCrisp = m_lineTextureCrisp;
    stroke.textureLow = float(m_lineRampLow) / 100.0f;
    stroke.textureHigh = float(m_lineRampHigh) / 100.0f;
}

void Canvas::setLineTextureRamp(int low, int high)
{
    m_lineRampLow = std::clamp(low, 0, 100);
    m_lineRampHigh = std::clamp(high, m_lineRampLow, 100);
    restyleSelection(tr("Change Texture Ramp"), [this](Stroke& s) {
        if (s.texture != 0) {
            s.textureLow = float(m_lineRampLow) / 100.0f;
            s.textureHigh = float(m_lineRampHigh) / 100.0f;
        }
    });
}

void Canvas::setBrushTextureRamp(int low, int high)
{
    m_brushRampLow = std::clamp(low, 0, 100);
    m_brushRampHigh = std::clamp(high, m_brushRampLow, 100);
    retextureSelectedFills(tr("Change Texture Ramp"), false);
}

void Canvas::setLineTextureCrisp(bool crisp)
{
    m_lineTextureCrisp = crisp;
    restyleSelection(tr("Change Texture Edge"), [this](Stroke& s) {
        if (s.texture != 0)
            s.textureCrisp = m_lineTextureCrisp;
    });
}

void Canvas::setBrushTextureCrisp(bool crisp)
{
    m_brushTextureCrisp = crisp;
    retextureSelectedFills(tr("Change Texture Edge"), false);
}

void Canvas::setLineTexture(int texture)
{
    m_lineTexture = PencilTexture::known(texture) ? texture : 0;
    restyleSelection(tr("Change Pencil Texture"), [this](Stroke& s) {
        s.texture = m_lineTexture;
        s.textureAmount = float(m_lineTextureAmount) / 100.0f;
        s.textureCrisp = m_lineTextureCrisp;
        s.textureLow = float(m_lineRampLow) / 100.0f;
        s.textureHigh = float(m_lineRampHigh) / 100.0f;
    });
}

void Canvas::setLineTextureAmount(int percent)
{
    m_lineTextureAmount = std::clamp(percent, 0, 100);
    restyleSelection(tr("Adjust Line Texture Opacity"), [this](Stroke& s) {
        if (s.texture != 0)
            s.textureAmount = float(m_lineTextureAmount) / 100.0f;
    });
}

void Canvas::retextureSelectedFills(QString const& text, bool kindToo)
{
    Drawing const* const d = currentDrawing();
    if (!d || !selecting() || m_selFills.isEmpty())
        return;
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selFills)) {
        if (i < 0 || i >= edited.fills.size() || !edited.fills[i].text.isEmpty())
            continue;
        Fill& fill = edited.fills[i];
        int const kind = kindToo ? m_brushTexture : fill.texture;
        float const shows = kind != 0 ? float(m_brushTextureAmount) / 100.0f : fill.textureAmount;
        bool const crisp = kind != 0 ? m_brushTextureCrisp : fill.textureCrisp;
        float const low = kind != 0 ? float(m_brushRampLow) / 100.0f : fill.textureLow, high = kind != 0 ? float(m_brushRampHigh) / 100.0f : fill.textureHigh;
        changed = changed || fill.texture != kind || fill.textureAmount != shows || fill.textureCrisp != crisp || fill.textureLow != low || fill.textureHigh != high;
        fill.textureLow = low;
        fill.textureHigh = high;
        fill.texture = kind;
        fill.textureAmount = shows;
        fill.textureCrisp = crisp;
    }
    if (changed)
        commitDrawing(edited, text);
    update();
}

void Canvas::setBrushTexture(int texture)
{
    m_brushTexture = PencilTexture::known(texture) ? texture : 0;
    retextureSelectedFills(tr("Change Paper Texture"), true);
}

void Canvas::setBrushTextureAmount(int percent)
{
    m_brushTextureAmount = std::clamp(percent, 0, 100);
    retextureSelectedFills(tr("Adjust Paper Texture Opacity"), false);
}

// A texture just made or brought in is the one in hand: for the brush when that is the tool, else for lines.
void Canvas::takeUpTexture(int id)
{
    if (id == 0)
        return;
    if (m_tool == Brush)
        setBrushTexture(id);
    else
        setLineTexture(id);
    emit texturesChanged();
    emit message(tr("Texture \"%1\" is ready to draw with.").arg(PencilTexture::name(id)));
}

void Canvas::onActionImportTexture()
{
    takeUpTexture(TextureDialogs::importTexture(this));
}

void Canvas::onActionNewTexture()
{
    takeUpTexture(TextureDialogs::newTexture(this));
}

void Canvas::onActionRenameTexture()
{
    int const id = m_tool == Brush ? m_brushTexture : m_lineTexture;
    if (id < TextureBank::kFirst) {
        emit message(tr("Choose a texture of your own in Tool Properties to rename it."));
        return;
    }
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Texture"), tr("Name:"), QLineEdit::Normal, PencilTexture::name(id), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    PencilTexture::rename(id, name);
    emit texturesChanged();
}

void Canvas::onActionExportTexture()
{
    int const id = m_tool == Brush ? m_brushTexture : m_lineTexture;
    if (id < TextureBank::kFirst) {
        emit message(tr("Choose a texture of your own in Tool Properties to export it."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export Texture"), PencilTexture::name(id) + QStringLiteral(".png"), tr("Eclipse texture (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        path += QStringLiteral(".png");
    if (PencilTexture::exportTo(id, path))
        emit message(tr("Texture written. Bring it in elsewhere with Import Texture from Picture."));
    else
        QMessageBox::warning(this, tr("Export Texture"), tr("The file could not be written."));
}

void Canvas::onActionDeleteTexture()
{
    int const id = m_tool == Brush ? m_brushTexture : m_lineTexture;
    if (id < TextureBank::kFirst) {
        emit message(tr("Choose a texture of your own in Tool Properties to delete it (the ones that come with the program stay)."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete Texture"),
                              tr("Delete the texture \"%1\"?\n\nWhat is drawn with it will be solid from then on.").arg(PencilTexture::name(id)))
        != QMessageBox::Yes)
        return;
    PencilTexture::discard(id);
    if (m_lineTexture == id)
        m_lineTexture = 0;
    if (m_brushTexture == id)
        m_brushTexture = 0;
    emit texturesChanged();
    update();
}

void Canvas::onActionAdjustLineTextureOpacity()
{
    Drawing const* const d = currentDrawing();
    int first = -1;
    if (selecting() && d)
        for (int i : std::as_const(m_selStrokes))
            if (first < 0 && i >= 0 && i < d->strokes.size() && d->strokes[i].texture != 0)
                first = i;
    if (first < 0) {
        emit message(tr("Select lines drawn with a pencil texture with the Select tool."));
        return;
    }
    bool ok = false;
    int const percent = QInputDialog::getInt(this, tr("Adjust Line Texture Opacity"), tr("How much of the texture shows (0: a solid line):"),
                                             int(std::lround(d->strokes[first].textureAmount * 100.0f)), 0, 100, 5, &ok);
    if (!ok)
        return;
    restyleSelection(tr("Adjust Line Texture Opacity"), [percent](Stroke& s) {
        if (s.texture != 0)
            s.textureAmount = float(percent) / 100.0f;
    });
}

void Canvas::onActionPencilLineProperties()
{
    Drawing const* const d = currentDrawing();
    int first = -1;
    if (selecting() && d)
        for (int i : std::as_const(m_selStrokes))
            if (first < 0 && i >= 0 && i < d->strokes.size() && d->strokes[i].width > 0.0f)
                first = i;
    if (first < 0) {
        emit message(tr("Select pencil lines with the Select tool."));
        return;
    }
    Stroke const& shown = d->strokes[first];
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pencil Line Properties"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QDoubleSpinBox* const thickness = new QDoubleSpinBox(&dialog);
    thickness->setRange(0.1, 500.0);
    thickness->setDecimals(1);
    thickness->setValue(double(shown.width));
    QComboBox* const texture = new QComboBox(&dialog);
    texture->setIconSize(QSize(72, 20));
    for (int kind : PencilTexture::kinds())
        texture->addItem(PencilTexture::icon(kind), PencilTexture::name(kind), kind);
    texture->setCurrentIndex(std::max(0, texture->findData(shown.texture)));
    QSpinBox* const amount = new QSpinBox(&dialog);
    amount->setRange(0, 100);
    amount->setSuffix(QStringLiteral(" %"));
    amount->setValue(int(std::lround(shown.textureAmount * 100.0f)));
    QComboBox* const join = new QComboBox(&dialog);
    join->addItems({tr("Round"), tr("Mitre"), tr("Bevel")});
    join->setCurrentIndex(int(shown.join));
    form->addRow(tr("Thickness"), thickness);
    form->addRow(tr("Texture"), texture);
    form->addRow(tr("Texture opacity"), amount);
    QCheckBox* const crispBox = new QCheckBox(tr("Crisp texture edge"), &dialog);
    crispBox->setChecked(shown.textureCrisp);
    form->addRow(crispBox);
    form->addRow(tr("Corners"), join);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    float const width = float(thickness->value()), shows = float(amount->value()) / 100.0f;
    bool const crispEdge = crispBox->isChecked();
    int const kind = texture->currentData().toInt(), corners = join->currentIndex();
    restyleSelection(tr("Pencil Line Properties"), [=](Stroke& s) {
        if (s.width <= 0.0f)
            return;     // an invisible line stays one
        s.width = width;
        s.texture = kind;
        s.textureAmount = shows;
        s.textureCrisp = crispEdge;
        s.join = Stroke::Join(corners);
    });
}

QVector<int> Canvas::contourPointsPicked() const
{
    QVector<int> picked;
    Drawing const* const d = currentDrawing();
    if (!d || !contourValid())
        return picked;
    int const count = int(contourLine(*d)->points.size());
    for (int i : m_ceExtra)
        if (i >= 0 && i < count && i != m_cePoint && !picked.contains(i))
            picked.push_back(i);
    return picked;
}

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
        QVector<Stroke> const inside = stroke.minus(without, fine, &lost);
        if (!lost) {
            strokes.push_back(int(art.strokes.size()));
            art.strokes.push_back(stroke);
        } else if (inside.isEmpty()) {
            art.strokes.push_back(stroke);
        } else {
            art.strokes += stroke.minus(within, fine);
            for (Stroke const& piece : inside) {
                strokes.push_back(int(art.strokes.size()));
                art.strokes.push_back(piece);
            }
            cut = true;
        }
    }
    if (cut)
        commitDrawing(art, tr("Cut Artwork"));
    setSelection(strokes, fills);
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
        // The closed areas with no paint that lie wholly inside what was drawn round: found
        // from a few places across it, each filled as a click there would fill it.
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
                if (m_art != LineArt && realDrawing()) {
                    bounded.strokes += realDrawing()->strokes;
                    for (Fill const& shape : realDrawing()->fills)
                        if (!shape.contours.isEmpty())
                            bounded.fills.push_back(shape);
                }
                QVector<QPolygonF> outlines;
                if (FillRegion::compute(bounded, FillRegion::paintArea(bounded), at, m_gapSize, outlines) != FillRegion::Ok || outlines.isEmpty())
                    continue;
                QRectF area;
                for (QPolygonF const& outline : std::as_const(outlines))
                    area |= outline.boundingRect();
                if (!reach.adjusted(-1, -1, 1, 1).contains(area))
                    continue;       // it runs on outside what was drawn round: left alone
                Fill fill;
                fill.outlines = outlines;
                fill.swatch = m_swatch;
                fill.color = color;
                int under = int(edited.fills.size());
                for (int i = 0; i < edited.fills.size(); ++i)
                    if (!edited.fills[i].contours.isEmpty()) {
                        under = i;
                        break;
                    }
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
