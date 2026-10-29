// Eclipse - the Camera view: line ends and joints restyled, paper textures on lines and brushes, pencil line properties.
#include "canvas/Canvas.h"

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
#include "canvas/PencilTexture.h"
#include "canvas/TextureDialogs.h"

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
    // The thickness multiplied (the line as it is, thicker or thinner), the tips with it or not, and grown besides.
    QSpinBox* const multiplier = new QSpinBox(&dialog);
    multiplier->setObjectName(QStringLiteral("PencilThicknessMultiplier"));
    multiplier->setRange(1, 1000);
    multiplier->setSuffix(QStringLiteral(" %"));
    multiplier->setValue(100);
    multiplier->setToolTip(tr("The lines made thicker or thinner, along all their length."));
    QCheckBox* const tipsToo = new QCheckBox(tr("Apply Multiplier to Tips"), &dialog);
    tipsToo->setObjectName(QStringLiteral("PencilMultiplyTips"));
    tipsToo->setChecked(true);
    tipsToo->setToolTip(tr("Grow the lines tips using the Thickness Multiplier."));
    QSpinBox* const tipExtra = new QSpinBox(&dialog);
    tipExtra->setObjectName(QStringLiteral("PencilTipMultiplier"));
    tipExtra->setRange(10, 1000);
    tipExtra->setSuffix(QStringLiteral(" %"));
    tipExtra->setValue(100);
    tipExtra->setToolTip(tr("Additional Tip Multiplier: apply an additional multiplier to extend line tips."));
    form->addRow(tr("Thickness Multiplier"), multiplier);
    form->addRow(tipsToo);
    form->addRow(tr("Additional Tip Multiplier"), tipExtra);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    float const width = float(thickness->value()), shows = float(amount->value()) / 100.0f;
    bool const crispEdge = crispBox->isChecked();
    double const times = multiplier->value() / 100.0, tipTimes = tipExtra->value() / 100.0;
    bool const tips = tipsToo->isChecked();
    int const kind = texture->currentData().toInt(), corners = join->currentIndex();
    restyleSelection(tr("Pencil Line Properties"), [=](Stroke& s) {
        if (s.width <= 0.0f)
            return;     // an invisible line stays one
        s.width = width;
        s.texture = kind;
        s.textureAmount = shows;
        s.textureCrisp = crispEdge;
        s.join = Stroke::Join(corners);
        if (times != 1.0 || tipTimes != 1.0)
            multiplyThickness(s, times, tips, tipTimes);
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

void Canvas::onActionAdjustLineTexture()
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
    dialog.setObjectName(QStringLiteral("AdjustLineTextureDialog"));
    dialog.setWindowTitle(tr("Adjust Line Texture"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("LineTextureKind"));
    for (int k : PencilTexture::kinds())
        kind->addItem(PencilTexture::icon(k), k == 0 ? tr("None (a solid line)") : PencilTexture::name(k), k);
    kind->setCurrentIndex(std::max(0, kind->findData(shown.texture)));
    form->addRow(tr("Texture"), kind);
    QSpinBox* const amount = new QSpinBox(&dialog);
    amount->setObjectName(QStringLiteral("LineTextureAmount"));
    amount->setRange(0, 100);
    amount->setSuffix(QStringLiteral(" %"));
    amount->setValue(int(std::lround(shown.textureAmount * 100.0f)));
    amount->setToolTip(tr("How much of the texture shows (0: a solid line)."));
    form->addRow(tr("Opacity"), amount);
    QCheckBox* const crisp = new QCheckBox(tr("Crisp edges"), &dialog);
    crisp->setObjectName(QStringLiteral("LineTextureCrisp"));
    crisp->setChecked(shown.textureCrisp);
    form->addRow(crisp);
    QSpinBox* const low = new QSpinBox(&dialog);
    QSpinBox* const high = new QSpinBox(&dialog);
    low->setObjectName(QStringLiteral("LineTextureLow"));
    high->setObjectName(QStringLiteral("LineTextureHigh"));
    for (QSpinBox* box : {low, high}) {
        box->setRange(0, 100);
        box->setSuffix(QStringLiteral(" %"));
    }
    low->setValue(int(std::lround(shown.textureLow * 100.0f)));
    high->setValue(int(std::lround(shown.textureHigh * 100.0f)));
    low->setToolTip(tr("Grain fainter than this leaves no ink."));
    high->setToolTip(tr("Grain at least this strong is solid ink."));
    form->addRow(tr("Ramp from"), low);
    form->addRow(tr("Ramp to"), high);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    int const texture = kind->currentData().toInt();
    float const opacity = float(amount->value()) / 100.0f;
    bool const sharp = crisp->isChecked();
    float const from = float(std::min(low->value(), high->value())) / 100.0f;
    float const to = float(std::max(low->value(), high->value())) / 100.0f;
    restyleSelection(tr("Adjust Line Texture"), [=](Stroke& s) {
        if (s.width <= 0.0f)
            return;
        s.texture = texture;
        s.textureAmount = opacity;
        s.textureCrisp = sharp;
        s.textureLow = from;
        s.textureHigh = to;
    });
}
