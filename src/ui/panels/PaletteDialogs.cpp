// Eclipse - the Colour view: its dialogs - colours in use when deleted, Recolour Drawings, Tint Panel, Optimize Palettes,
// Palette Overrides.
#include "ui/panels/PalettePanel.h"

#include <QButtonGroup>
#include "ui/panels/ColourEditor.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "model/document/Commands.h"

namespace {

QIcon patch(QColor const& colour)
{
    QPixmap pixmap(20, 12);
    pixmap.fill(colour);
    QPainter painter(&pixmap);
    painter.setPen(QColor(16, 16, 16));
    painter.drawRect(0, 0, 19, 11);
    return QIcon(pixmap);
}

// A choice of every colour of the scene but those left out: "name (palette)", the id as data.
QComboBox* colourChoice(Scene const& scene, QVector<int> const& leftOut, int chosen, QWidget* parent)
{
    QComboBox* const box = new QComboBox(parent);
    box->setObjectName(QStringLiteral("ColourChoice"));
    for (Swatch const& s : scene.palette) {
        if (leftOut.contains(s.id))
            continue;
        box->addItem(patch(s.color), QStringLiteral("%1  (%2)").arg(s.name, scene.paletteNames.value(s.palette)), s.id);
        if (s.id == chosen)
            box->setCurrentIndex(box->count() - 1);
    }
    return box;
}

// How many lines and areas the colours are used for, on the given layers (all when empty).
int usesOf(Scene const& scene, QVector<int> const& ids, QVector<int> const& layers = {})
{
    int count = 0;
    for (int l = 0; l < scene.layers.size(); ++l) {
        if (!layers.isEmpty() && !layers.contains(l))
            continue;
        for (Drawing const& d : scene.layers[l].drawings)
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (Stroke const& stroke : d.strokesOf(sheet))
                    count += ids.contains(stroke.swatch);
                for (Fill const& fill : d.fillsOf(sheet))
                    count += ids.contains(fill.swatch);
            }
    }
    return count;
}

}  // namespace

void PalettePanel::deleteColours(QVector<int> const& ids, QString const& text)
{
    Scene const& scene = m_document->scene();
    if (usesOf(scene, ids) == 0) {
        m_document->removeColours(ids, {}, text);
        return;
    }
    // Recolour what uses them, unpaint it, or let it keep the look it has.
    QDialog ask(this);
    ask.setObjectName(QStringLiteral("DeleteColourDialog"));
    ask.setWindowTitle(tr("Delete Colour"));
    QVBoxLayout* const column = new QVBoxLayout(&ask);
    QLabel* const why = new QLabel(tr("The colour(s) you are trying to delete are used by some of the drawings. You can select to recolour "
                                      "the drawings using another colour, unpaint the regions, or delete the colour(s) and let the drawings keep "
                                      "the look they have."),
                                   &ask);
    why->setWordWrap(true);
    column->addWidget(why);
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Recolour with:"), &ask));
    QComboBox* const with = colourChoice(scene, ids, -1, &ask);
    row->addWidget(with, 1);
    column->addLayout(row);
    QDialogButtonBox* const buttons = new QDialogButtonBox(&ask);
    QPushButton* const recolour = buttons->addButton(tr("Recolour"), QDialogButtonBox::AcceptRole);
    recolour->setObjectName(QStringLiteral("Recolour"));
    recolour->setEnabled(with->count() > 0);
    QPushButton* const unpaint = buttons->addButton(tr("Unpaint"), QDialogButtonBox::AcceptRole);
    unpaint->setObjectName(QStringLiteral("Unpaint"));
    QPushButton* const keep = buttons->addButton(tr("Delete Colour"), QDialogButtonBox::AcceptRole);
    keep->setObjectName(QStringLiteral("DeleteAnyway"));
    buttons->addButton(QDialogButtonBox::Cancel);
    int choice = 0;
    connect(recolour, &QPushButton::clicked, &ask, [&] { choice = 1; ask.accept(); });
    connect(unpaint, &QPushButton::clicked, &ask, [&] { choice = 2; ask.accept(); });
    connect(keep, &QPushButton::clicked, &ask, [&] { choice = 3; ask.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    column->addWidget(buttons);
    if (ask.exec() != QDialog::Accepted || choice == 0)
        return;
    int const to = with->currentData().toInt();
    m_document->beginMacro(text);
    for (int id : ids) {
        if (choice == 1)
            m_document->recolour(id, to);
        else if (choice == 2)
            m_document->unpaintColour(id);
    }
    m_document->removeColours(ids, {}, text);
    m_document->endMacro();
}

void PalettePanel::onActionRecolourDrawings()
{
    Scene const& scene = m_document->scene();
    Swatch const* const from = scene.findSwatch(m_current);
    if (!from)
        return;
    int const fromId = from->id;
    QDialog ask(this);
    ask.setObjectName(QStringLiteral("RecolourDialog"));
    ask.setWindowTitle(tr("Recolour Drawings"));
    QFormLayout* const form = new QFormLayout(&ask);
    QLabel* const was = new QLabel(from->name, &ask);
    was->setPixmap(patch(from->color).pixmap(20, 12));
    QHBoxLayout* const fromRow = new QHBoxLayout;
    fromRow->addWidget(was);
    fromRow->addWidget(new QLabel(from->name, &ask), 1);
    form->addRow(tr("Replace colour:"), fromRow);
    QComboBox* const with = colourChoice(scene, {fromId}, -1, &ask);
    form->addRow(tr("Recolour with:"), with);
    QComboBox* const scope = new QComboBox(&ask);
    scope->setObjectName(QStringLiteral("RecolourScope"));
    scope->addItems({tr("The current layer"), tr("The selected layers"), tr("Every layer of the scene")});
    scope->setCurrentIndex(1);
    form->addRow(tr("In:"), scope);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    form->addRow(buttons);
    if (ask.exec() != QDialog::Accepted || with->count() == 0)
        return;
    int const changed = m_document->recolour(fromId, with->currentData().toInt(), layersInScope(scope->currentIndex()));
    if (changed == 0)
        QMessageBox::information(this, tr("Recolour Drawings"), tr("Nothing there is drawn or painted in %1.").arg(m_document->scene().findSwatch(fromId)->name));
}

void PalettePanel::onActionTintPanel()
{
    // The colours of the palette (or those selected) mixed toward a tint: by blending, or by adding it (an offset).
    QDialog panel(this);
    panel.setObjectName(QStringLiteral("TintPanel"));
    panel.setWindowTitle(tr("Tint Panel"));
    QFormLayout* const form = new QFormLayout(&panel);
    QComboBox* const which = new QComboBox(&panel);
    which->setObjectName(QStringLiteral("TintWhich"));
    which->addItems({tr("The whole palette \"%1\"").arg(m_document->scene().paletteNames.value(m_palette)), tr("The selected colours")});
    form->addRow(tr("Tint:"), which);
    QColor tint(255, 160, 60);
    QPushButton* const colour = new QPushButton(&panel);
    colour->setObjectName(QStringLiteral("TintColour"));
    auto showTint = [&] { colour->setIcon(patch(tint)); colour->setText(tint.name()); };
    showTint();
    connect(colour, &QPushButton::clicked, &panel, [&] {
        QColor const chosen = ColourEditor::getColour(tint, &panel, tr("Tint Colour"));
        if (chosen.isValid()) {
            tint = chosen;
            showTint();
        }
    });
    form->addRow(tr("Tint Colour:"), colour);
    QRadioButton* const blend = new QRadioButton(tr("Blend: toward the tint"), &panel);
    blend->setObjectName(QStringLiteral("TintBlend"));
    QRadioButton* const offset = new QRadioButton(tr("Offset: the tint added"), &panel);
    offset->setObjectName(QStringLiteral("TintOffset"));
    blend->setChecked(true);
    QVBoxLayout* const modes = new QVBoxLayout;
    modes->addWidget(blend);
    modes->addWidget(offset);
    form->addRow(tr("Tint Offset/Blend:"), modes);
    QSpinBox* const amount = new QSpinBox(&panel);
    amount->setObjectName(QStringLiteral("TintAmount"));
    amount->setRange(-100, 100);
    amount->setSuffix(QStringLiteral(" %"));
    amount->setValue(25);
    form->addRow(tr("Amount:"), amount);
    QDialogButtonBox* const buttons = new QDialogButtonBox(&panel);
    QPushButton* const apply = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    apply->setObjectName(QStringLiteral("TintApply"));
    QPushButton* const reset = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("TintReset"));
    buttons->addButton(QDialogButtonBox::Close);
    form->addRow(buttons);
    int applied = 0;    // the tints applied from this panel, which Reset takes back
    connect(apply, &QPushButton::clicked, &panel, [&] {
        QVector<int> const ids = which->currentIndex() == 0 ? m_document->scene().paletteSwatches(m_palette) : selectedSwatches();
        if (m_document->tintColours(ids, tint, amount->value() / 100.0, offset->isChecked()))
            ++applied;
    });
    connect(reset, &QPushButton::clicked, &panel, [&] {
        for (; applied > 0; --applied)
            m_document->undoStack()->undo();
    });
    connect(buttons, &QDialogButtonBox::rejected, &panel, &QDialog::reject);
    panel.exec();
}

void PalettePanel::onActionOptimizePalettes()
{
    // The colours nothing is drawn in, and the palettes none of whose colours are used: ticked, to be taken out.
    Scene const& scene = m_document->scene();
    QSet<int> const used = scene.usedSwatches();
    QSet<int> cloned;
    for (Swatch const& s : scene.palette)
        if (s.cloneOf != 0)
            cloned.insert(s.cloneOf);
    QDialog ask(this);
    ask.setObjectName(QStringLiteral("OptimizePalettes"));
    ask.setWindowTitle(tr("Optimize Palettes"));
    QVBoxLayout* const column = new QVBoxLayout(&ask);
    column->addWidget(new QLabel(tr("Colours and palettes that no drawing uses. Those ticked are removed."), &ask));
    QTreeWidget* const tree = new QTreeWidget(&ask);
    tree->setObjectName(QStringLiteral("UnusedColours"));
    tree->setHeaderHidden(true);
    int offered = 0;
    for (int p = 0; p < scene.paletteNames.size(); ++p) {
        QVector<int> const ids = scene.paletteSwatches(p);
        QVector<int> unused;
        for (int id : ids)
            if (!used.contains(id) && !cloned.contains(id))
                unused.push_back(id);
        if (unused.isEmpty())
            continue;
        bool const whole = unused.size() == ids.size() && p != 0 && scene.paletteNames.size() > 1;
        QTreeWidgetItem* const top = new QTreeWidgetItem(tree, {whole ? tr("%1 (the whole palette)").arg(scene.paletteNames[p]) : scene.paletteNames[p]});
        top->setData(0, Qt::UserRole, whole ? p : -1);
        top->setFlags(top->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        top->setCheckState(0, Qt::Checked);
        for (int id : std::as_const(unused)) {
            Swatch const* const s = scene.findSwatch(id);
            QTreeWidgetItem* const leaf = new QTreeWidgetItem(top, {s->name});
            leaf->setIcon(0, patch(s->color));
            leaf->setData(0, Qt::UserRole, id);
            leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
            leaf->setCheckState(0, Qt::Checked);
            ++offered;
        }
        top->setExpanded(true);
    }
    if (offered == 0) {
        QMessageBox::information(this, tr("Optimize Palettes"), tr("No palettes need to be cleaned up."));
        return;
    }
    column->addWidget(tree, 1);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &ask);
    QPushButton* const remove = buttons->addButton(tr("Remove"), QDialogButtonBox::AcceptRole);
    remove->setObjectName(QStringLiteral("RemoveUnused"));
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    column->addWidget(buttons);
    ask.resize(360, 420);
    if (ask.exec() != QDialog::Accepted)
        return;
    QVector<int> colours, palettes;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* const top = tree->topLevelItem(i);
        if (top->checkState(0) == Qt::Checked && top->data(0, Qt::UserRole).toInt() >= 0)
            palettes.push_back(top->data(0, Qt::UserRole).toInt());
        for (int j = 0; j < top->childCount(); ++j)
            if (top->child(j)->checkState(0) == Qt::Checked)
                colours.push_back(top->child(j)->data(0, Qt::UserRole).toInt());
    }
    m_document->removeColours(colours, palettes, tr("Optimize Palettes"));
}

void PalettePanel::onActionPaletteOverrides()
{
    // The palettes of clones the layer is drawn with in place of the colours they clone; the first in the list wins.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || !scene.layers[m_layer].holdsDrawings()) {
        QMessageBox::information(this, tr("Current Palette Overrides"), tr("Choose a drawing layer first: palette overrides belong to a layer."));
        return;
    }
    int const layer = m_layer;
    QVector<int> list = scene.layers[layer].paletteOverrides;
    QDialog ask(this);
    ask.setObjectName(QStringLiteral("PaletteOverrides"));
    ask.setWindowTitle(tr("Palette Overrides: %1").arg(scene.layers[layer].name));
    QVBoxLayout* const column = new QVBoxLayout(&ask);
    column->addWidget(new QLabel(tr("Palettes of clones the layer is drawn with in place of the colours they clone.\nThe first one in the list wins."), &ask));
    QListWidget* const shown = new QListWidget(&ask);
    shown->setObjectName(QStringLiteral("OverrideList"));
    auto refill = [&] {
        shown->clear();
        for (int p : std::as_const(list))
            shown->addItem(scene.paletteNames.value(p));
    };
    refill();
    column->addWidget(shown, 1);
    QHBoxLayout* const row = new QHBoxLayout;
    QComboBox* const clones = new QComboBox(&ask);
    clones->setObjectName(QStringLiteral("OverrideChoice"));
    for (int p = 0; p < scene.paletteNames.size(); ++p) {
        bool any = false;
        for (Swatch const& s : scene.palette)
            any = any || (s.palette == p && s.cloneOf != 0);
        if (any)
            clones->addItem(scene.paletteNames[p], p);
    }
    QPushButton* const add = new QPushButton(tr("Add"), &ask);
    add->setObjectName(QStringLiteral("OverrideAdd"));
    add->setEnabled(clones->count() > 0);
    QPushButton* const remove = new QPushButton(tr("Remove"), &ask);
    QPushButton* const clear = new QPushButton(tr("Clear"), &ask);
    QPushButton* const up = new QPushButton(tr("Up"), &ask);
    row->addWidget(clones, 1);
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(clear);
    row->addWidget(up);
    column->addLayout(row);
    if (clones->count() == 0)
        column->addWidget(new QLabel(tr("No palette has clones yet: Palettes > Clone... makes one."), &ask));
    connect(add, &QPushButton::clicked, &ask, [&] {
        int const p = clones->currentData().toInt();
        if (!list.contains(p))
            list.push_back(p);
        refill();
    });
    connect(remove, &QPushButton::clicked, &ask, [&] {
        if (int const at = shown->currentRow(); at >= 0)
            list.removeAt(at);
        refill();
    });
    connect(clear, &QPushButton::clicked, &ask, [&] {
        list.clear();
        refill();
    });
    connect(up, &QPushButton::clicked, &ask, [&] {
        if (int const at = shown->currentRow(); at > 0) {
            list.swapItemsAt(at, at - 1);
            refill();
            shown->setCurrentRow(at - 1);
        }
    });
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    column->addWidget(buttons);
    if (ask.exec() == QDialog::Accepted)
        m_document->setPaletteOverrides(layer, list);
}
