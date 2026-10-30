// Eclipse - the Camera view: the gradient matrix given to what is selected, and the selection filtered.
#include "canvas/Canvas.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "model/document/Commands.h"

void Canvas::onActionPasteGradientMatrix()
{
    Drawing const* const d = currentDrawing();
    if (!d || !canEditDrawing() || !hasSelection())
        return;
    if (!m_hasStoredGradient) {
        emit message(tr("Copy a texture or gradient matrix first (Copy Texture/Gradient Matrix, or Store Colour Gradient)."));
        return;
    }
    Drawing edited = *d;
    bool changed = false;
    for (int i : std::as_const(m_selFills))
        if (i >= 0 && i < edited.fills.size()) {
            Fill& fill = edited.fills[i];
            fill.gradPlaced = true;
            fill.gradFrom = m_storedGradFrom;
            fill.gradTo = m_storedGradTo;
            changed = true;
        }
    for (int i : std::as_const(m_selStrokes))
        if (i >= 0 && i < edited.strokes.size()) {
            Stroke& line = edited.strokes[i];
            line.gradPlaced = true;
            line.gradFrom = m_storedGradFrom;
            line.gradTo = m_storedGradTo;
            changed = true;
        }
    if (changed)
        commitDrawing(edited, tr("Paste Texture/Gradient Matrix"));
}

void Canvas::onActionFilterSelection()
{
    Drawing const* const d = currentDrawing();
    if (!d || !hasSelection()) {
        emit message(tr("Select something first: the filter keeps part of the selection."));
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("FilterSelectionDialog"));
    dialog.setWindowTitle(tr("Filter Current Selection"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    QCheckBox* const lines = new QCheckBox(tr("Lines"), &dialog);
    lines->setObjectName(QStringLiteral("FilterLines"));
    lines->setChecked(true);
    QCheckBox* const paint = new QCheckBox(tr("Paint"), &dialog);
    paint->setObjectName(QStringLiteral("FilterPaint"));
    paint->setChecked(true);
    QCheckBox* const pictures = new QCheckBox(tr("Pictures"), &dialog);
    pictures->setObjectName(QStringLiteral("FilterPictures"));
    pictures->setChecked(true);
    QCheckBox* const colour = new QCheckBox(tr("Only the colour in hand"), &dialog);
    colour->setObjectName(QStringLiteral("FilterColour"));
    for (QWidget* w : {static_cast<QWidget*>(lines), static_cast<QWidget*>(paint), static_cast<QWidget*>(pictures), static_cast<QWidget*>(colour)})
        layout->addWidget(w);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    bool const onlyColour = colour->isChecked();
    QVector<int> strokes, fills, kept;
    if (lines->isChecked())
        for (int i : std::as_const(m_selStrokes))
            if (i >= 0 && i < d->strokes.size() && (!onlyColour || d->strokes[i].swatch == m_swatch))
                strokes.push_back(i);
    if (paint->isChecked())
        for (int i : std::as_const(m_selFills))
            if (i >= 0 && i < d->fills.size() && (!onlyColour || d->fills[i].swatch == m_swatch))
                fills.push_back(i);
    if (pictures->isChecked() && !onlyColour)
        kept = m_selPictures;
    setSelection(strokes, fills, kept);
    emit message(tr("%n thing(s) left selected.", nullptr, int(strokes.size() + fills.size() + kept.size())));
}
