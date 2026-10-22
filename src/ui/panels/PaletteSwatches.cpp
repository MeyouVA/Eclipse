// Eclipse - the Colour view: a colour added, changed, renamed, removed; the gradient editor.
#include "ui/panels/PalettePanel.h"
#include "ui/panels/GradientBar.h"

#include "ui/panels/ColourEditor.h"
#include <QFileDialog>

#include <algorithm>
#include <cmath>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "model/document/Commands.h"
#include <QListWidget>

void PalettePanel::addSwatch()
{
    onActionNewColour();
}

void PalettePanel::removeSwatch()
{
    deleteSelection();
}

void PalettePanel::editColor(QListWidgetItem* item)
{
    int const id = item->data(Qt::UserRole).toInt();
    Swatch const* const swatch = m_document->scene().findSwatch(id);
    if (!swatch)
        return;
    // A gradient is changed in its own editor.
    if (swatch->isGradient()) {
        editGradient(id);
        return;
    }
    // A texture: another picture for it.
    if (swatch->isTexture()) {
        QString const file = QFileDialog::getOpenFileName(this, tr("Texture"), QString(), tr("Pictures (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)"));
        QImage const picture = file.isEmpty() ? QImage() : QImage(file);
        if (!picture.isNull())
            m_document->setSwatchTexture(id, picture);
        return;
    }
    // A plain colour: in the Colour Editor, which changes it as you go.
    m_current = id;
    emit colourEditorWanted();
}

void PalettePanel::itemRenamed(QListWidgetItem* item)
{
    if (m_rebuilding)
        return;
    int const id = item->data(Qt::UserRole).toInt();
    Swatch const* const swatch = m_document->scene().findSwatch(id);
    if (!swatch || m_grid)
        return;
    QString const name = item->text().trimmed();
    if (name.isEmpty() || name == swatch->name) {
        rebuild();  // put the old name back
        return;
    }
    m_document->editSwatch(id, name, swatch->color);
}

int PalettePanel::addGradientSwatch()
{
    m_document->beginMacro(tr("New Gradient Colour"));
    int const id = m_document->addSwatch(tr("Gradient %1").arg(m_document->scene().palette.size() + 1), QColor(Qt::black), m_palette);
    m_document->setSwatchGradient(id, Swatch::Linear, {{0.0, QColor(Qt::black)}, {1.0, QColor(Qt::white)}});
    m_document->endMacro();
    m_current = id;
    rebuild();
    return id;
}

int PalettePanel::addTextureSwatch(QImage const& texture, QString const& name)
{
    if (texture.isNull())
        return -1;
    m_document->beginMacro(tr("New Texture Colour"));
    int const id = m_document->addSwatch(name.isEmpty() ? tr("Texture %1").arg(m_document->scene().palette.size() + 1) : name, QColor(Qt::gray), m_palette);
    m_document->setSwatchTexture(id, texture);
    m_document->endMacro();
    m_current = id;
    rebuild();
    return id;
}

void PalettePanel::editGradient(int id)
{
    Swatch const* const swatch = m_document->scene().findSwatch(id);
    if (!swatch)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Gradient: %1").arg(swatch->name));
    QComboBox* const kind = new QComboBox(&dialog);
    kind->setObjectName(QStringLiteral("GradientKind"));
    kind->addItems({tr("One colour"), tr("Gradient in a line"), tr("Round gradient")});
    kind->setCurrentIndex(int(swatch->kind));
    GradientBar* const bar = new GradientBar(&dialog);
    bar->setObjectName(QStringLiteral("GradientBar"));
    bar->setStops(swatch->isGradient() ? swatch->stops : QVector<GradientStop>{{0.0, swatch->color}, {1.0, QColor(Qt::white)}});
    QPushButton* const colour = new QPushButton(tr("Colour..."), &dialog);
    colour->setObjectName(QStringLiteral("GradientColour"));
    QPushButton* const remove = new QPushButton(tr("Remove Colour"), &dialog);
    remove->setObjectName(QStringLiteral("GradientRemove"));
    connect(colour, &QPushButton::clicked, &dialog, [bar, &dialog] {
        QColor const chosen = ColourEditor::getColour(bar->stops().value(bar->selected()).color, &dialog, tr("Colour"));
        bar->setSelectedColour(chosen);
    });
    connect(remove, &QPushButton::clicked, bar, [bar] { bar->removeSelected(); });
    auto follow = [=] {
        bool const gradient = kind->currentIndex() != 0;
        bar->setEnabled(gradient);
        colour->setEnabled(gradient);
        remove->setEnabled(gradient && bar->stops().size() > 2);
    };
    connect(kind, &QComboBox::currentIndexChanged, &dialog, follow);
    connect(bar, &GradientBar::changed, &dialog, follow);
    follow();
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(colour);
    row->addWidget(remove);
    row->addStretch(1);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(tr("Kind"), kind);
    form->addRow(new QLabel(tr("Press a marker to choose a colour, drag it along; double-click the bar to add one."), &dialog));
    form->addRow(bar);
    form->addRow(row);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_document->setSwatchGradient(id, Swatch::Kind(kind->currentIndex()), bar->stops());
}
