// Eclipse - the main window: locking, enabling, solo, track colours, merging, stepping, marking, renaming, attaching layers; column properties.
#include "ui/window/MainWindow.h"

#include "actions/manager/ActionManager.h"
#include <QCheckBox>
#include <QSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "ui/panels/ColourEditor.h"
#include <QInputDialog>
#include <QMessageBox>

#include "actions/dialogs/ShortcutsDialog.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/views/NodeView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"
#include "ui/timeline/Xsheet.h"
#include "ui/views/DepthView.h"
#include "ui/views/LipSyncView.h"
#include "audio/lipsync/LipSync.h"
#include "ui/views/PerspectiveView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QSettings>
#include <QStatusBar>

void MainWindow::onActionLockAll()
{
    m_document->setAllLocked(true);
}

void MainWindow::onActionUnlockAll()
{
    m_document->setAllLocked(false);
}

void MainWindow::onActionLockAllOthers()
{
    m_document->setAllLocked(true, layer());
}

void MainWindow::onActionToggleLayerEnabled()
{
    // As the current layer goes, so go the others selected.
    bool const enable = !m_document->scene().layers[layer()].visible;
    m_document->editLayers(m_timeline->selectedLayers(), enable ? tr("Enable") : tr("Disable"), [enable](Layer& l) { l.visible = enable; });
}

void MainWindow::onActionEnableAll()
{
    m_document->setAllVisible(true);
}

void MainWindow::onActionDisableAllOthers()
{
    m_document->setAllVisible(false, layer());
}

void MainWindow::onActionToggleSolo()
{
    m_document->setLayerSolo(layer(), !m_document->scene().layers[layer()].solo);
}

void MainWindow::onActionCollapseExpand()
{
    if (m_document->scene().hasChildren(layer()))
        m_document->setLayerCollapsed(layer(), !m_document->scene().layers[layer()].collapsed);
}

void MainWindow::onActionCollapseAll()
{
    m_document->setAllCollapsed(true);
}

void MainWindow::onActionExpandAll()
{
    m_document->setAllCollapsed(false);
}

void MainWindow::pickTrackColour(int l)
{
    if (l < 0 || l >= m_document->scene().layers.size())
        return;
    QColor const now = m_document->scene().layers[l].trackColor;
    QColor const chosen = ColourEditor::getColour(now.isValid() ? now : QColor(120, 120, 120), this, tr("Track Colour"));
    if (!chosen.isValid())
        return;
    // The layers selected with it take the colour too.
    QVector<int> const selected = m_timeline->selectedLayers();
    if (selected.contains(l) && selected.size() > 1)
        m_document->editLayers(selected, tr("Change Track Colour"), [chosen](Layer& layer) { layer.trackColor = chosen; });
    else
        m_document->setLayerColor(l, chosen);
}

void MainWindow::onActionChangeTrackColour()
{
    pickTrackColour(layer());
}

void MainWindow::onActionDefaultTrackColour()
{
    m_document->editLayers(m_timeline->selectedLayers(), tr("Default Track Colour"), [](Layer& l) { l.trackColor = QColor(); });
}

void MainWindow::onActionMergeLayers()
{
    int const at = m_document->mergeLayers(m_timeline->selectedLayers());
    if (at < 0) {
        statusBar()->showMessage(tr("Select two or more drawing layers to merge."), 5000);
        return;
    }
    m_timeline->clearRange();
    m_timeline->setCurrentCell(at, frame());
}

void MainWindow::onActionToggleLayerOnion()
{
    Scene const& scene = m_document->scene();
    bool const on = !scene.layers[layer()].onion;
    QVector<int> drawingLayers;
    for (int l : m_timeline->selectedLayers())
        if (scene.layers[l].holdsDrawings())
            drawingLayers.push_back(l);
    m_document->editLayers(drawingLayers, on ? tr("Onion Skin On") : tr("Onion Skin Off"), [on](Layer& l) { l.onion = on; });
}

void MainWindow::stepSibling(int direction)
{
    // The next layer, towards the front or the back, attached to the same thing as this one.
    Scene const& scene = m_document->scene();
    int const parent = scene.parentIndex(layer());
    for (int l = layer() + direction; l >= 0 && l < scene.layers.size(); l += direction) {
        if (scene.parentIndex(l) == parent) {
            m_timeline->setCurrentCell(l, frame());
            return;
        }
    }
}

void MainWindow::onActionResetAll()
{
    // On this frame the layer stands as it was made: not moved, turned, scaled or faded.
    Scene const& scene = m_document->scene();
    if (!m_timeline->hasParameters(layer()))
        return;
    Pose rest;
    rest.fx = scene.layers[layer()].poseAt(frame()).fx;     // the amounts of its effects are not a transformation
    if (scene.layers[layer()].poseAt(frame()) == rest)
        return;
    m_document->setKeyframe(layer(), frame(), rest, tr("Reset All"));
}

void MainWindow::stepLayer(int direction)
{
    // Up or down the rows of the Timeline, so past layers that are folded away.
    QVector<int> const rows = m_timeline->rows();
    int const row = int(rows.indexOf(layer())) + direction;
    if (row >= 0 && row < rows.size())
        m_timeline->setCurrentCell(rows[row], frame());
}

void MainWindow::stepColumn(int direction)
{
    // Across the columns of the Xsheet, in its order (left to right).
    if (!m_xsheet) {
        stepLayer(direction);
        return;
    }
    int column = m_xsheet->columnOfLayer(layer());
    if (column < 0)
        return;
    // (A layer's value columns come after its own: they are passed over.)
    do
        column += direction;
    while (column >= 0 && m_xsheet->layerOfColumn(column) == layer());
    int const to = column >= 0 ? m_xsheet->layerOfColumn(column) : -1;
    if (to >= 0)
        m_timeline->setCurrentCell(to, frame());
}

// `as` is "key", "breakdown", "inbetween" or "none".
void MainWindow::onActionMarkDrawing(QString const& as)
{
    int const mark = as == QLatin1String("key") ? Drawing::KeyMark : as == QLatin1String("breakdown") ? Drawing::BreakdownMark
                     : as == QLatin1String("inbetween") ? Drawing::InBetweenMark : Drawing::NoMark;
    Scene const& scene = m_document->scene();
    // The drawings of the cells selected (the current cell's, with no block selected), each once.
    QVector<QPair<int, int>> cells;
    QVector<int> const layers = m_timeline->hasRange() ? m_timeline->selectedLayers() : QVector<int>{layer()};
    int const first = m_timeline->hasRange() ? m_timeline->selectionFirstFrame() : frame();
    int const last = m_timeline->hasRange() ? m_timeline->selectionLastFrame() : frame();
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size())
            continue;
        QSet<int> seen;
        for (int f = first; f <= last; ++f) {
            int const d = scene.layers[l].drawingAt(f);
            if (d != Layer::kNoDrawing && !seen.contains(d)) {
                seen.insert(d);
                cells.push_back({l, f});
            }
        }
    }
    if (cells.isEmpty()) {
        statusBar()->showMessage(tr("There is no drawing on this frame to mark."), 4000);
        return;
    }
    if (cells.size() > 1)
        m_document->beginMacro(tr("Mark Drawing"));
    for (auto const& [l, f] : std::as_const(cells))
        m_document->markDrawing(l, f, mark);
    if (cells.size() > 1)
        m_document->endMacro();
}

void MainWindow::onActionOnionLayers(QString const& which)
{
    Scene const& scene = m_document->scene();
    QVector<int> const selected = m_timeline->selectedLayers();
    bool const on = which == QLatin1String("add") || which == QLatin1String("addAll");
    QVector<int> layers;
    for (int l = 0; l < scene.layers.size(); ++l) {
        bool const mine = selected.contains(l);
        bool const wanted = which == QLatin1String("addAll") || which == QLatin1String("removeAll") ? true
                            : which == QLatin1String("removeUnselected") ? !mine : mine;
        if (wanted && scene.layers[l].holdsDrawings() && scene.layers[l].onion != on)
            layers.push_back(l);
    }
    if (!layers.isEmpty())
        m_document->editLayers(layers, on ? tr("Onion Skin On") : tr("Onion Skin Off"), [on](Layer& l) { l.onion = on; });
}

// The frames selected in the Timeline, of the layers selected, marked with a colour by name ("none": unmarked).
void MainWindow::onActionMarkFrame(QString const& colour)
{
    m_document->markFrames(m_timeline->selectedLayers(), m_timeline->selectionFirstFrame(), m_timeline->selectionLastFrame(),
                           colour == QLatin1String("none") ? QColor() : QColor(colour));
}

void MainWindow::onActionLayerKeys(QString const& how)
{
    Scene const& scene = m_document->scene();
    bool const clear = how == QLatin1String("clear"), tween = how == QLatin1String("motion");
    QVector<int> layers;
    for (int l : m_timeline->selectedLayers()) {
        bool differs = false;
        for (Keyframe const& key : scene.layers[l].keys)
            differs = differs || clear || key.tween != tween;
        if (differs)
            layers.push_back(l);
    }
    if (layers.isEmpty())
        return;
    m_document->editLayers(layers, clear ? tr("Clear All Values") : tween ? tr("Set Motion Keyframes") : tr("Set Stop-Motion Keyframes"), [clear, tween](Layer& l) {
        if (clear)
            l.keys.clear();
        for (Keyframe& key : l.keys)
            key.tween = tween;
    });
}

void MainWindow::onActionRenameWithPrefix()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drawings.isEmpty()) {
        statusBar()->showMessage(tr("This layer has no drawings to rename."), 4000);
        return;
    }
    bool ok = false;
    QString const prefix = QInputDialog::getText(this, tr("Rename Drawing with Prefix"), tr("What every drawing of the layer is to be called, before its number:"),
                                                 QLineEdit::Normal, scene.layers[layer()].name + QLatin1Char('-'), &ok);
    if (ok)
        m_document->renameDrawingsWithPrefix(layer(), prefix);
}

void MainWindow::onActionRenameByFrame()
{
    if (!m_document->renameDrawingsByFrame(layer()))
        statusBar()->showMessage(tr("The drawings of this layer are named after their frames already, or it has none."), 5000);
}

void MainWindow::onActionRenameDrawing()
{
    Scene const& scene = m_document->scene();
    if (layer() < 0 || layer() >= scene.layers.size() || scene.layers[layer()].drawingAt(frame()) == Layer::kNoDrawing) {
        statusBar()->showMessage(tr("There is no drawing on this frame to rename."), 4000);
        return;
    }
    Layer const& current = scene.layers[layer()];
    int const index = current.drawingAt(frame());
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Drawing"), tr("Name (empty: its number, %1):").arg(index + 1), QLineEdit::Normal,
                                               current.drawings[index].name, &ok);
    if (ok && name.trimmed() != current.drawings[index].name && !m_document->renameDrawing(layer(), frame(), name))
        QMessageBox::warning(this, tr("Eclipse"), tr("Another drawing of this layer is called \"%1\".").arg(name.trimmed()));
}

void MainWindow::onActionRenameLayer()
{
    renameLayer(layer());
}

void MainWindow::renameLayer(int l)
{
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size())
        return;
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("Rename Layer"), tr("Name:"), QLineEdit::Normal, scene.layers[l].name, &ok);
    if (ok)
        m_document->renameLayer(l, name);
}

void MainWindow::onActionMoveLayerUp()
{
    int const l = layer();
    if (l + 1 < m_document->scene().layers.size()) {
        m_document->moveLayer(l, +1);
        m_timeline->setCurrentCell(l + 1, frame());
    }
}

void MainWindow::onActionMoveLayerDown()
{
    int const l = layer();
    if (l > 0) {
        m_document->moveLayer(l, -1);
        m_timeline->setCurrentCell(l - 1, frame());
    }
}

void MainWindow::onActionAttachTo()
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l < 0 || l >= scene.layers.size())
        return;
    // Everything it could follow without going round in a circle; front layers first.
    QStringList names;
    QVector<int> indices;
    for (int i = int(scene.layers.size()) - 1; i >= 0; --i) {
        if (i == l || scene.isAncestor(l, i))
            continue;
        names << scene.layers[i].name;
        indices << i;
    }
    if (names.isEmpty()) {
        statusBar()->showMessage(tr("There is no other layer to attach %1 to.").arg(scene.layers[l].name), 6000);
        return;
    }
    bool ok = false;
    int const current = int(indices.indexOf(scene.parentIndex(l)));
    QString const chosen = QInputDialog::getItem(this, tr("Attach To"), tr("%1 follows:").arg(scene.layers[l].name), names,
                                                 std::max(0, current), false, &ok);
    if (ok)
        m_document->setParent(l, indices[names.indexOf(chosen)]);
}

void MainWindow::onActionDetach()
{
    m_document->setParent(layer(), -1);
}

// Column Properties: what the current layer is as a column of the Timeline and the Xsheet, in one window.
void MainWindow::onActionColumnProperties()
{
    Scene const& scene = m_document->scene();
    int const l = layer();
    if (l < 0 || l >= scene.layers.size())
        return;
    Layer const was = scene.layers[l];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ColumnPropertiesDialog"));
    dialog.setWindowTitle(tr("Column Properties"));
    QLineEdit* const name = new QLineEdit(was.name, &dialog);
    name->setObjectName(QStringLiteral("ColumnName"));
    static char const* const kKinds[] = {QT_TR_NOOP("Drawing"), QT_TR_NOOP("Peg"), QT_TR_NOOP("Camera"), QT_TR_NOOP("Sound"),
                                         QT_TR_NOOP("Colour-Card"), QT_TR_NOOP("Group"), QT_TR_NOOP("Bone"), QT_TR_NOOP("Quadmap"), QT_TR_NOOP("Annotation")};
    QLabel* const kind = new QLabel(tr(kKinds[was.kind]) + (was.showsSymbol() ? tr(" (shows a symbol)") : QString()), &dialog);
    kind->setObjectName(QStringLiteral("ColumnKind"));
    QColor colour = was.trackColor;
    QPushButton* const colourButton = new QPushButton(&dialog);
    colourButton->setObjectName(QStringLiteral("ColumnColour"));
    QCheckBox* const defaultColour = new QCheckBox(tr("Default colour"), &dialog);
    defaultColour->setObjectName(QStringLiteral("ColumnDefaultColour"));
    auto showColour = [colourButton, defaultColour, &colour] {
        colourButton->setEnabled(!defaultColour->isChecked());
        colourButton->setText(defaultColour->isChecked() || !colour.isValid() ? QObject::tr("(default)") : colour.name());
        colourButton->setStyleSheet(defaultColour->isChecked() || !colour.isValid() ? QString() : QStringLiteral("background-color: %1;").arg(colour.name()));
    };
    defaultColour->setChecked(!colour.isValid());
    connect(defaultColour, &QCheckBox::toggled, &dialog, showColour);
    connect(colourButton, &QPushButton::clicked, &dialog, [&] {
        QColor const chosen = ColourEditor::getColour(colour.isValid() ? colour : QColor(90, 120, 160), &dialog, tr("Track Colour"));
        if (chosen.isValid())
            colour = chosen;
        showColour();
    });
    showColour();
    QCheckBox* const enabled = new QCheckBox(tr("Enabled (in the picture)"), &dialog);
    enabled->setObjectName(QStringLiteral("ColumnEnabled"));
    enabled->setChecked(was.visible);
    QCheckBox* const locked = new QCheckBox(tr("Locked"), &dialog);
    locked->setObjectName(QStringLiteral("ColumnLocked"));
    locked->setChecked(was.locked);
    QCheckBox* const bitmap = new QCheckBox(tr("Bitmap layer"), &dialog);
    bitmap->setObjectName(QStringLiteral("ColumnBitmap"));
    bitmap->setChecked(was.bitmap);
    bitmap->setVisible(was.holdsDrawings() && !was.showsSymbol());
    // The other art layers, each a bitmap one of its own or not.
    QCheckBox* artBitmap[Drawing::SheetCount] = {};
    QHBoxLayout* const artRow = new QHBoxLayout;
    for (auto const& [sheet, label, id] : {std::tuple<int, char const*, char const*>{Drawing::UnderlaySheet, QT_TR_NOOP("Underlay"), "ColumnBitmapUnderlay"},
                                           {Drawing::ColourSheet, QT_TR_NOOP("Colour Art"), "ColumnBitmapColour"},
                                           {Drawing::OverlaySheet, QT_TR_NOOP("Overlay"), "ColumnBitmapOverlay"}}) {
        QCheckBox* const box = new QCheckBox(tr(label), &dialog);
        box->setObjectName(QLatin1String(id));
        box->setToolTip(tr("What is drawn on this art layer is painted in pixels, not kept as lines."));
        box->setChecked(was.bitmapSheet(sheet));
        box->setVisible(was.holdsDrawings() && !was.showsSymbol());
        artBitmap[sheet] = box;
        artRow->addWidget(box);
    }
    artRow->addStretch(1);
    auto artBits = [&artBitmap] {
        int bits = 0;
        for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet)
            if (artBitmap[sheet] && artBitmap[sheet]->isChecked())
                bits |= 1 << sheet;
        return bits;
    };
    QSpinBox* const fineness = new QSpinBox(&dialog);
    fineness->setObjectName(QStringLiteral("ColumnBitmapResolution"));
    fineness->setRange(25, 400);
    fineness->setSingleStep(25);
    fineness->setSuffix(tr(" %"));
    fineness->setValue(was.bitmapResolution);
    fineness->setToolTip(tr("How fine the pixels of a bitmap layer are: 100 % is a pixel for each unit of the scene, 200 % two across each. "
                            "Drawings already painted keep the pixels they have."));
    fineness->setVisible(was.holdsDrawings() && !was.showsSymbol());
    QLabel* const drawings = new QLabel(&dialog);
    drawings->setObjectName(QStringLiteral("ColumnDrawings"));
    if (was.holdsDrawings())
        drawings->setText(tr("%n drawing(s)", nullptr, int(was.drawings.size())) + (was.syncGroup ? tr(", synced") : QString())
                          + (was.cloneGroup ? (was.cloneTiming ? tr(", a clone (drawings and timing)") : tr(", a clone (drawings)")) : QString()));
    QFormLayout* const form = new QFormLayout;
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Type:"), kind);
    form->addRow(tr("Colour:"), colourButton);
    form->addRow(QString(), defaultColour);
    form->addRow(QString(), enabled);
    form->addRow(QString(), locked);
    form->addRow(QString(), bitmap);
    if (was.holdsDrawings() && !was.showsSymbol()) {
        form->addRow(tr("Bitmap art layers:"), artRow);
        form->addRow(tr("Bitmap resolution:"), fineness);
    } else {
        delete artRow;
    }
    if (was.holdsDrawings())
        form->addRow(tr("Drawings:"), drawings);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (m_columnEditorNext) {
        // The Column Editor: Apply/Next keeps what was changed and goes on to the next column.
        dialog.setWindowTitle(tr("Column Editor"));
        QPushButton* const next = buttons->addButton(tr("Apply/Next"), QDialogButtonBox::ApplyRole);
        next->setObjectName(QStringLiteral("ColumnApplyNext"));
        connect(next, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
    }
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    int const result = dialog.exec();
    if (result == QDialog::Rejected)
        return;
    struct Next {
        MainWindow* window;
        bool go;
        ~Next()
        {
            if (go) {
                window->columnEditorNext();
                window->onActionColumnProperties();
            }
        }
    } const next{this, result == 2};
    QColor const newColour = defaultColour->isChecked() ? QColor() : colour;
    bool const nameChanged = !name->text().trimmed().isEmpty() && name->text().trimmed() != was.name;
    bool const rest = newColour != was.trackColor || enabled->isChecked() != was.visible || locked->isChecked() != was.locked
                      || (bitmap->isVisible() || was.bitmap) && bitmap->isChecked() != was.bitmap || fineness->value() != was.bitmapResolution
                      || (was.holdsDrawings() && !was.showsSymbol() && artBits() != was.bitmapArt);
    if (!nameChanged && !rest)
        return;
    m_document->beginMacro(tr("Column Properties"));
    if (nameChanged)
        m_document->renameLayer(l, name->text().trimmed());
    if (rest) {
        bool const on = enabled->isChecked(), lock = locked->isChecked(), pixels = bitmap->isChecked() && was.holdsDrawings() && !was.showsSymbol();
        int const resolution = fineness->value();
        int const artPixels = was.holdsDrawings() && !was.showsSymbol() ? artBits() : was.bitmapArt;
        m_document->editLayers({l}, tr("Column Properties"), [newColour, on, lock, pixels, resolution, artPixels](Layer& layer) {
            layer.bitmapResolution = resolution;
            layer.bitmapArt = artPixels;
            layer.trackColor = newColour;
            layer.visible = on;
            layer.locked = lock;
            layer.bitmap = pixels;
        });
    }
    m_document->endMacro();
}
