// Eclipse - the Xsheet view: its panel (toolbar, column list, status strip) and its commands.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/panels/ColourEditor.h"
#include "ui/timeline/Timeline.h"
#include "ui/timeline/Xsheet.h"

#include "ui/window/MainWindowPrivate.h"

void MainWindow::createXsheetView()
{
    m_xsheet = new Xsheet(m_document, this);
    QWidget* const panel = new QWidget(this);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (QToolBar* const own = m_actions->createToolbar(QStringLiteral("XsheetViewToolbar"), this)) {
        own->setIconSize(QSize(20, 20));
        layout->addWidget(own);
    }
    QSplitter* const split = new QSplitter(Qt::Horizontal, panel);
    split->setObjectName(QStringLiteral("XsheetSplitter"));
    m_xsheetList = new QListWidget(split);
    m_xsheetList->setObjectName(QStringLiteral("XsheetColumnList"));
    m_xsheetList->setToolTip(tr("The columns: ticked ones are shown in the Xsheet. A click makes a column the current one."));
    m_xsheetList->hide();
    split->addWidget(m_xsheetList);
    split->addWidget(m_xsheet);
    split->setStretchFactor(1, 1);
    split->setSizes({110, 400});
    layout->addWidget(split, 1);
    QLabel* const status = new QLabel(panel);
    status->setObjectName(QStringLiteral("XsheetStatus"));
    status->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(status);
    addView(tr("Xsheet"), QStringLiteral("XsheetDock"), panel, Qt::RightDockWidgetArea);

    connect(m_xsheet, &Xsheet::statusChanged, status, [this, status] { status->setText(m_xsheet->statusText()); });
    connect(m_xsheet, &Xsheet::columnsChanged, this, &MainWindow::refreshXsheetList);
    connect(m_xsheetList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        int const l = item->data(Qt::UserRole).toInt();
        bool const hidden = item->checkState() != Qt::Checked;
        if (l >= 0 && l < m_document->scene().layers.size() && m_document->scene().layers[l].columnHidden != hidden)
            m_document->editLayers({l}, tr("Show/Hide Xsheet Column"), [hidden](Layer& layer) { layer.columnHidden = hidden; });
    });
    connect(m_xsheetList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int const l = item->data(Qt::UserRole).toInt();
        if (l >= 0 && l < m_document->scene().layers.size())
            m_timeline->setCurrentCell(l, frame());
    });
    // The block selected in the Xsheet is the block the commands work on (they read it from the Timeline).
    connect(m_xsheet, &Xsheet::selectionChanged, this, [this] {
        m_xsheetMirroring = true;
        QVector<int> const layers = m_xsheet->selectedLayers();
        if (m_xsheet->selectionExtent() >= 0)
            m_timeline->extendSelection(layer(), m_xsheet->selectionExtent());
        else
            m_timeline->clearRange();
        QVector<int> others;
        for (int l : layers)
            if (l != layer())
                others.push_back(l);
        m_timeline->setExtraLayers(others);
        m_xsheetMirroring = false;
    });
    auto followSelection = [this] {
        if (!m_xsheetMirroring && m_xsheet->selectionOnly())
            m_xsheet->setSelectionLayers(m_timeline->selectedLayers());
    };
    connect(m_timeline, &Timeline::selectionChanged, this, followSelection);
    connect(m_timeline, &Timeline::currentCellChanged, this, followSelection);
    connect(m_xsheet, &Xsheet::cellPicked, this, [this] {
        m_xsheetMirroring = true;
        QTimer::singleShot(0, this, [this] { m_xsheetMirroring = false; });
    });
    auto popup = [this](QString const& id) {
        return [this, id](QPoint const& globalPos) {
            if (QMenu* const menu = m_actions->createMenu(id, this)) {
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->popup(globalPos);
            }
        };
    };
    connect(m_xsheet, &Xsheet::headerMenuRequested, this, popup(QStringLiteral("menu.xsheetHeader")));
    connect(m_xsheet, &Xsheet::sheetMarkerMenuRequested, this, popup(QStringLiteral("menu.xsheetMarker")));
    connect(m_xsheet, &Xsheet::renameRequested, this, &MainWindow::renameLayer);
    connect(m_xsheet, &Xsheet::removeFramesRequested, this, [this](int first, int last) {
        QMessageBox::StandardButton const answer = QMessageBox::question(
            this, tr("Delete"), tr("You have selected a range containing all the columns.\nThis could also remove the frames. Do you want to also\nremove the frames?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            m_document->removeFrames(first, last);
        else if (answer == QMessageBox::No)
            m_document->clearCells(m_xsheet->selectedLayers(), first, last, tr("Clear Cells"));
    });
    connect(m_canvas, &Canvas::onionChanged, m_xsheet, [this] { m_xsheet->setOnion(m_canvas->onionSkin(), m_canvas->onionPrevious(), m_canvas->onionNext()); });
    m_xsheet->setOnion(m_canvas->onionSkin(), m_canvas->onionPrevious(), m_canvas->onionNext());
}

void MainWindow::connectXsheetToggles()
{
    if (QAction* const light = m_actions->action(QStringLiteral("LightTable")))
        connect(light, &QAction::toggled, m_xsheet, &Xsheet::setLightTable);
    m_actions->setChecked(QStringLiteral("XsheetGesturalDrag"), m_xsheet->gesturalDrag());
    m_actions->setChecked(QStringLiteral("XsheetInsertMode"), m_xsheet->insertMode());
    syncXsheetChecks();
}

void MainWindow::syncXsheetChecks()
{
    int const shown = m_xsheet->shown();
    m_actions->setChecked(QStringLiteral("XsheetThumbnails"), m_xsheet->thumbnails());
    m_actions->setChecked(QStringLiteral("XsheetColumnList"), m_xsheetList && !m_xsheetList->isHidden());
    m_actions->setChecked(QStringLiteral("XsheetShowDrawingColumns"), shown & Xsheet::ShowDrawings);
    m_actions->setChecked(QStringLiteral("XsheetShowFunctionColumns"), shown & Xsheet::ShowFunctions);
    m_actions->setChecked(QStringLiteral("XsheetShowPathColumns"), shown & Xsheet::ShowPaths);
    m_actions->setChecked(QStringLiteral("XsheetShowSelection"), m_xsheet->selectionOnly());
    m_actions->setChecked(QStringLiteral("XsheetShowGrouping"), m_xsheet->grouping());
    m_actions->setChecked(QStringLiteral("XsheetMinimalHeaders"), m_xsheet->minimalHeaders());
    m_actions->setChecked(QStringLiteral("XsheetHeldLine"), m_xsheet->heldLook() == Xsheet::HeldLine);
    m_actions->setChecked(QStringLiteral("XsheetHeldValue"), m_xsheet->heldLook() == Xsheet::HeldValue);
    m_actions->setChecked(QStringLiteral("XsheetUnitsFrames"), !m_xsheet->feetAndFrames());
    m_actions->setChecked(QStringLiteral("XsheetUnitsFeet"), m_xsheet->feetAndFrames());
    m_actions->setChecked(QStringLiteral("XsheetAnnotationDrawing"), m_xsheet->annotationDrawing());
}

void MainWindow::refreshXsheetList()
{
    if (!m_xsheetList)
        return;
    QSignalBlocker const block(m_xsheetList);
    Scene const& scene = m_document->scene();
    m_xsheetList->clear();
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        Layer const& column = scene.layers[l];
        QListWidgetItem* const item = new QListWidgetItem(column.name, m_xsheetList);
        item->setData(Qt::UserRole, l);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(column.columnHidden ? Qt::Unchecked : Qt::Checked);
        if (l == layer())
            item->setSelected(true);
    }
}

// ---- view ----

void MainWindow::onActionXsheetGesturalDrag(bool on)
{
    m_xsheet->setGesturalDrag(on);
}

void MainWindow::onActionXsheetInsertMode(bool on)
{
    m_xsheet->setInsertMode(on);
    statusBar()->showMessage(on ? tr("Xsheet: typed drawings are inserted.") : tr("Xsheet: typed drawings overwrite the cells."), 3000);
}

void MainWindow::onActionXsheetView(QString const& what)
{
    if (what == QLatin1String("zoomIn"))
        m_xsheet->zoomIn();
    else if (what == QLatin1String("zoomOut"))
        m_xsheet->zoomOut();
    else if (what == QLatin1String("resetZoom"))
        m_xsheet->resetZoom();
    else if (what == QLatin1String("resetPan"))
        m_xsheet->resetPan();
    else if (what == QLatin1String("resetView"))
        m_xsheet->resetView();
    else if (what == QLatin1String("extents"))
        m_xsheet->zoomExtents();
    else if (what == QLatin1String("heldLine"))
        m_xsheet->setHeldLook(Xsheet::HeldLine);
    else if (what == QLatin1String("heldValue"))
        m_xsheet->setHeldLook(Xsheet::HeldValue);
    else if (what == QLatin1String("frames"))
        m_xsheet->setFeetAndFrames(false);
    else if (what == QLatin1String("feet"))
        m_xsheet->setFeetAndFrames(true);
    else if (what == QLatin1String("thumbnails"))
        m_xsheet->setThumbnails(!m_xsheet->thumbnails());
    else if (what == QLatin1String("grouping"))
        m_xsheet->setGrouping(!m_xsheet->grouping());
    else if (what == QLatin1String("minimal"))
        m_xsheet->setMinimalHeaders(!m_xsheet->minimalHeaders());
    else if (what == QLatin1String("drawingColumns"))
        m_xsheet->setShown(m_xsheet->shown() ^ Xsheet::ShowDrawings);
    else if (what == QLatin1String("functionColumns"))
        m_xsheet->setShown(m_xsheet->shown() ^ (Xsheet::ShowFunctions | Xsheet::ShowRotations));
    else if (what == QLatin1String("pathColumns"))
        m_xsheet->setShown(m_xsheet->shown() ^ Xsheet::ShowPaths);
    else if (what == QLatin1String("columnList")) {
        m_xsheetList->setVisible(m_xsheetList->isHidden());
        refreshXsheetList();
    } else if (what == QLatin1String("selection")) {
        m_xsheet->setSelectionOnly(!m_xsheet->selectionOnly(), m_timeline->selectedLayers());
    } else if (what == QLatin1String("collapse") || what == QLatin1String("expand")) {
        m_xsheet->collapseColumns(what == QLatin1String("collapse"), false);
    } else if (what == QLatin1String("collapseAll") || what == QLatin1String("expandAll")) {
        m_xsheet->collapseColumns(what == QLatin1String("collapseAll"), true);
    }
    syncXsheetChecks();
}

void MainWindow::onActionXsheetColumnTypes()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("XsheetColumnTypesDialog"));
    dialog.setWindowTitle(tr("Show Column Types"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    struct Kind {
        int flag;
        char const* name;
        char const* id;
    };
    static Kind const kKinds[] = {{Xsheet::ShowDrawings, QT_TR_NOOP("Drawing Columns"), "XsheetTypeDrawings"},
                                  {Xsheet::ShowFunctions, QT_TR_NOOP("Function Columns"), "XsheetTypeFunctions"},
                                  {Xsheet::ShowPaths, QT_TR_NOOP("3D Path Columns"), "XsheetTypePaths"},
                                  {Xsheet::ShowRotations, QT_TR_NOOP("3D Rotation Columns"), "XsheetTypeRotations"},
                                  {Xsheet::ShowAnnotations, QT_TR_NOOP("Annotation Columns"), "XsheetTypeAnnotations"},
                                  {Xsheet::ShowSounds, QT_TR_NOOP("Sound Columns"), "XsheetTypeSounds"},
                                  {Xsheet::ShowOthers, QT_TR_NOOP("Peg, Camera and Other Columns"), "XsheetTypeOthers"}};
    QVector<QCheckBox*> boxes;
    for (Kind const& k : kKinds) {
        QCheckBox* const box = new QCheckBox(tr(k.name), &dialog);
        box->setObjectName(QLatin1String(k.id));
        box->setChecked(m_xsheet->shown() & k.flag);
        layout->addWidget(box);
        boxes.push_back(box);
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    int shown = 0;
    for (int i = 0; i < boxes.size(); ++i)
        if (boxes[i]->isChecked())
            shown |= kKinds[i].flag;
    m_xsheet->setShown(shown);
    syncXsheetChecks();
}

// ---- columns ----

QVector<int> MainWindow::xsheetColumnLayers() const
{
    QVector<int> layers = m_xsheet->selectedLayers();
    if (layers.isEmpty() && layer() >= 0)
        layers.push_back(layer());
    return layers;
}

void MainWindow::onActionHideSelectedColumns()
{
    QVector<int> const layers = xsheetColumnLayers();
    m_document->editLayers(layers, tr("Show/Hide Xsheet Column"), [](Layer& l) { l.columnHidden = true; });
}

void MainWindow::onActionUnhideAllColumns()
{
    QVector<int> hidden;
    for (int l = 0; l < m_document->scene().layers.size(); ++l)
        if (m_document->scene().layers[l].columnHidden)
            hidden.push_back(l);
    if (!hidden.isEmpty())
        m_document->editLayers(hidden, tr("Show All Xsheet Columns"), [](Layer& l) { l.columnHidden = false; });
}

void MainWindow::onActionShowHiddenColumns()
{
    Scene const& scene = m_document->scene();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("XsheetShowHiddenDialog"));
    dialog.setWindowTitle(tr("Show Hidden Columns"));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Tick the columns to show again:"), &dialog));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("XsheetHiddenList"));
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        if (!scene.layers[l].columnHidden)
            continue;
        QListWidgetItem* const item = new QListWidgetItem(scene.layers[l].name, list);
        item->setData(Qt::UserRole, l);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton* const all = buttons->addButton(tr("Show All"), QDialogButtonBox::ActionRole);
    connect(all, &QPushButton::clicked, list, [list] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Checked);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (list->count() == 0) {
        statusBar()->showMessage(tr("No column is hidden."), 4000);
        return;
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    QVector<int> shown;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            shown.push_back(list->item(i)->data(Qt::UserRole).toInt());
    if (!shown.isEmpty())
        m_document->editLayers(shown, tr("Show/Hide Xsheet Column"), [](Layer& l) { l.columnHidden = false; });
}

void MainWindow::onActionChangeColumnsColour()
{
    QVector<int> const layers = xsheetColumnLayers();
    if (layers.isEmpty())
        return;
    QColor const was = m_document->scene().layers.value(layers.first()).trackColor;
    QColor const chosen = ColourEditor::getColour(was.isValid() ? was : QColor(90, 120, 160), this, tr("Columns Colour"));
    if (chosen.isValid())
        m_document->editLayers(layers, tr("Change Columns Colour"), [chosen](Layer& l) { l.trackColor = chosen; });
}

void MainWindow::onActionDefaultColumnsColour()
{
    m_document->editLayers(xsheetColumnLayers(), tr("Default Columns Colour"), [](Layer& l) { l.trackColor = QColor(); });
}

void MainWindow::onActionSetColumnsWidth()
{
    QVector<int> const layers = xsheetColumnLayers();
    if (layers.isEmpty())
        return;
    Layer const& first = m_document->scene().layers[layers.first()];
    bool ok = false;
    int const width = QInputDialog::getInt(this, tr("Set Columns Width"), tr("Width of the columns selected (pixels):"),
                                           first.columnWidth > 0 ? first.columnWidth : m_xsheet->columnWidth(), 20, 1000, 1, &ok);
    if (ok)
        m_document->editLayers(layers, tr("Xsheet Columns Width"), [width](Layer& l) { l.columnWidth = width; l.columnCollapsed = false; });
}

void MainWindow::onActionDefaultColumnsWidth()
{
    QVector<int> layers;
    for (int l = 0; l < m_document->scene().layers.size(); ++l)
        if (m_document->scene().layers[l].columnWidth != 0)
            layers.push_back(l);
    if (!layers.isEmpty())
        m_document->editLayers(layers, tr("Xsheet Columns Width"), [](Layer& l) { l.columnWidth = 0; });
}

// Add Columns: so many of a kind, named so, beside the current column (as the preference says) or at either end.
void MainWindow::onActionAddColumns()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("XsheetAddColumnsDialog"));
    dialog.setWindowTitle(tr("Add Columns"));
    QLineEdit* const name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("AddColumnsName"));
    QComboBox* const type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("AddColumnsType"));
    type->addItem(tr("Drawing"), int(Layer::DrawingLayer));
    type->addItem(tr("Annotation"), int(Layer::Annotation));
    type->addItem(tr("Peg"), int(Layer::Peg));
    type->addItem(tr("Colour-Card"), int(Layer::ColourCard));
    QSpinBox* const count = new QSpinBox(&dialog);
    count->setObjectName(QStringLiteral("AddColumnsNumber"));
    count->setRange(1, 100);
    QComboBox* const where = new QComboBox(&dialog);
    where->setObjectName(QStringLiteral("AddColumnsWhere"));
    where->addItems({tr("Right of the Current Column"), tr("Left of the Current Column"), tr("At the Start (left)"), tr("At the End (right)")});
    where->setCurrentIndex(std::clamp(settings().value(QStringLiteral("prefs/xsheetAddColumns"), 3).toInt(), 0, 3));
    auto suggest = [name, type] { name->setText(type->currentText()); };
    connect(type, &QComboBox::currentIndexChanged, &dialog, suggest);
    suggest();
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Type:"), type);
    form->addRow(tr("Number:"), count);
    form->addRow(tr("Insert:"), where);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    addXsheetColumns(Layer::Kind(type->currentData().toInt()), name->text().trimmed(), count->value(), where->currentIndex());
}

int MainWindow::addXsheetColumns(int kind, QString const& name, int count, int where)
{
    Scene const& scene = m_document->scene();
    int const here = std::clamp(layer(), 0, std::max(0, int(scene.layers.size()) - 1));
    // Left in the Xsheet is in front: "right of" the current column is just behind it.
    int above = where == 0 ? here - 1 : where == 1 ? here : where == 2 ? int(scene.layers.size()) - 1 : -1;
    int added = -1;
    m_document->beginMacro(tr("Add Columns"));
    for (int i = 0; i < count; ++i) {
        int const at = kind == Layer::Annotation ? m_document->addAnnotation(above) : kind == Layer::Peg ? m_document->addPeg(above)
                       : kind == Layer::ColourCard ? m_document->addColourCard() : m_document->addLayer(above);
        if (at < 0)
            continue;
        if (!name.isEmpty())
            m_document->renameLayer(at, count > 1 && i > 0 ? QStringLiteral("%1_%2").arg(name).arg(i + 1) : name);
        added = at;
        // The next goes to the right of this one (just behind it).
        above = at - 1;
    }
    m_document->endMacro();
    if (added >= 0)
        m_timeline->setCurrentCell(added, frame());
    return added;
}

void MainWindow::onActionAddDrawingColumn()
{
    addXsheetColumns(Layer::DrawingLayer, QString(), 1, 0);
}

void MainWindow::onActionAddAnnotationColumn()
{
    addXsheetColumns(Layer::Annotation, QString(), 1, 0);
}

// Column Editor: Column Properties, with Apply/Next going on to the column beside.
void MainWindow::onActionColumnEditor()
{
    m_columnEditorNext = true;
    onActionColumnProperties();
    m_columnEditorNext = false;
}

void MainWindow::columnEditorNext()
{
    bool const rightwards = settings().value(QStringLiteral("prefs/xsheetApplyNextLeftToRight"), true).toBool();
    int const column = m_xsheet->columnOfLayer(layer());
    int c = column;
    do
        c += rightwards ? 1 : -1;
    while (c >= 0 && c < m_xsheet->columns().size() && m_xsheet->layerOfColumn(c) == layer());
    if (column >= 0 && c >= 0 && c < m_xsheet->columns().size())
        m_timeline->setCurrentCell(m_xsheet->layerOfColumn(c), frame());
}

// ---- Timesheet markers and tempo ----

void MainWindow::onActionAddSheetMarker()
{
    m_xsheet->addSheetMarker();
}

void MainWindow::onActionSheetMarkerColour(QString const& colour)
{
    int const marker = m_xsheet->menuMarker();
    m_xsheet->setSheetMarkerColour(marker, colour == QLatin1String("white") ? 1 : colour == QLatin1String("grey") ? 2 : 0);
}

void MainWindow::onActionDeleteSheetMarker()
{
    m_xsheet->removeSheetMarker(m_xsheet->menuMarker());
}

void MainWindow::onActionTempoMarks()
{
    Scene const& scene = m_document->scene();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("XsheetTempoDialog"));
    dialog.setWindowTitle(tr("Tempo Marks"));
    QSpinBox* const beat = new QSpinBox(&dialog);
    beat->setObjectName(QStringLiteral("TempoFramesPerBeat"));
    beat->setRange(0, 1000);
    beat->setSpecialValueText(tr("None"));
    beat->setValue(scene.framesPerBeat);
    QSpinBox* const bar = new QSpinBox(&dialog);
    bar->setObjectName(QStringLiteral("TempoBeatsPerBar"));
    bar->setRange(0, 1000);
    bar->setSpecialValueText(tr("None"));
    bar->setValue(scene.beatsPerBar);
    QFormLayout* const form = new QFormLayout(&dialog);
    form->addRow(tr("Frames per beat:"), beat);
    form->addRow(tr("Beats per bar:"), bar);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted || (beat->value() == scene.framesPerBeat && bar->value() == scene.beatsPerBar))
        return;
    int const fpb = beat->value(), bpb = bar->value();
    m_document->editScene(tr("Tempo Marks"), [fpb, bpb](Scene& s) {
        s.framesPerBeat = fpb;
        s.beatsPerBar = bpb;
    });
}

// ---- annotation columns ----

void MainWindow::onActionAnnotation(QString const& what)
{
    Scene const& scene = m_document->scene();
    bool const annotation = layer() >= 0 && layer() < scene.layers.size() && scene.layers[layer()].kind == Layer::Annotation;
    if (what == QLatin1String("pen")) {
        m_xsheet->setAnnotationDrawing(!m_xsheet->annotationDrawing());
        syncXsheetChecks();
        return;
    }
    if (what == QLatin1String("colour")) {
        QColor const chosen = ColourEditor::getColour(m_xsheet->penColour(), this, tr("Pen Colour"));
        if (chosen.isValid())
            m_xsheet->setPenColour(chosen);
        return;
    }
    if (what == QLatin1String("width")) {
        bool ok = false;
        double const width = QInputDialog::getDouble(this, tr("Pen Width"), tr("Width of the pen (pixels):"), m_xsheet->penWidth(), 0.5, 20.0, 1, &ok);
        if (ok)
            m_xsheet->setPenWidth(width);
        return;
    }
    if (!annotation) {
        QMessageBox::information(this, tr("Annotation"), tr("Please select the annotation column."));
        return;
    }
    if (what == QLatin1String("eraseAll"))
        m_xsheet->eraseAnnotation(true, true, true);
    else if (what == QLatin1String("eraseImages"))
        m_xsheet->eraseAnnotation(true, false, false);
    else if (what == QLatin1String("eraseTexts"))
        m_xsheet->eraseAnnotation(false, true, false);
    else if (what == QLatin1String("text"))
        m_xsheet->beginNote();
    else if (what == QLatin1String("picture")) {
        QString const file = QFileDialog::getOpenFileName(this, tr("Select Annotation Bitmap File"), QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif)"));
        if (file.isEmpty())
            return;
        QImageReader reader(file);
        QImage const image = reader.read();
        if (image.isNull() || !m_xsheet->addAnnotationPicture(image))
            QMessageBox::warning(this, tr("Annotation"), tr("The picture could not be read: %1").arg(QFileInfo(file).fileName()));
    }
}

// ---- printing and previewing ----

void MainWindow::onActionPrintXsheet()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setDocName(m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName());
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print Xsheet"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    QMessageBox side(QMessageBox::Question, tr("Print Xsheet"), tr("Do you want to print the xsheet on the left or right?"), QMessageBox::Cancel, this);
    QPushButton* const left = side.addButton(tr("Left"), QMessageBox::AcceptRole);
    QPushButton* const right = side.addButton(tr("Right"), QMessageBox::AcceptRole);
    side.exec();
    if (side.clickedButton() != left && side.clickedButton() != right)
        return;
    Xsheet::PrintOptions options;
    options.numbersOnRight = side.clickedButton() == left;      // the sheet on the left: its frame numbers to the right of it
    options.title = printer.docName();
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(this, tr("Print Xsheet"), tr("The Xsheet could not be printed."));
        return;
    }
    QRectF const page = printer.pageLayout().paintRectPixels(printer.resolution());
    int const pages = Xsheet::printPages(painter, QRectF(0, 0, page.width(), page.height()), m_document->scene(), options, [&printer] { return printer.newPage(); });
    painter.end();
    statusBar()->showMessage(tr("%n page(s) of the Xsheet sent to the printer.", nullptr, pages), 5000);
}

void MainWindow::onActionXsheetPreview()
{
    // Like Mini Playback, over the frames selected (or on from the current frame).
    if (m_playTimer.isActive()) {
        stop();
        return;
    }
    int const first = m_xsheet->hasSelection() ? m_xsheet->selectionFirstFrame() : frame();
    int const last = m_xsheet->hasSelection() ? m_xsheet->selectionLastFrame() : m_document->scene().frameCount() - 1;
    m_miniFrame = frame();
    m_miniStart = m_playStart;
    m_miniStop = m_playStop;
    m_miniLoop = m_loop;
    setPlayRange(first, last);
    m_loop = true;
    m_timeline->setCurrentFrame(m_playStart);
    m_mini = true;
    onActionTogglePlayback();
}
