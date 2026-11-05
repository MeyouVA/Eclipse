// Eclipse - the Morphing Inspector.
#include "ui/views/MorphInspector.h"

#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "model/geometry/Morph.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QPushButton* smallButton(QString const& text, QString const& name, QString const& tip, QWidget* parent)
{
    QPushButton* const b = new QPushButton(text, parent);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setAutoDefault(false);
    return b;
}

QTableWidgetItem* checkItem(bool on)
{
    QTableWidgetItem* const item = new QTableWidgetItem;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    return item;
}

QVector<int> chosenRows(QTableWidget* table)
{
    QVector<int> rows;
    for (QModelIndex const& index : table->selectionModel()->selectedRows())
        rows.push_back(index.row());
    if (rows.isEmpty() && table->currentRow() >= 0)
        rows.push_back(table->currentRow());
    std::sort(rows.begin(), rows.end());
    return rows;
}

}  // namespace

MorphInspector::MorphInspector(Document* document, Canvas* canvas, QWidget* parent) : QWidget(parent), m_document(document), m_canvas(canvas)
{
    setObjectName(QStringLiteral("MorphInspector"));
    setWindowTitle(tr("Morphing Inspector"));
    QVBoxLayout* const top = new QVBoxLayout(this);
    m_current = new QLabel(this);
    m_current->setObjectName(QStringLiteral("MorphCurrent"));
    m_current->setToolTip(tr("Current Morphing: the morphing on the current cell."));
    top->addWidget(m_current);
    m_body = new QWidget(this);
    top->addWidget(m_body);
    QVBoxLayout* const body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);

    // ---- Morphing Properties: quality, flatten, eases, time.
    QGroupBox* const properties = new QGroupBox(tr("Morphing Properties"), m_body);
    QFormLayout* const form = new QFormLayout(properties);
    m_quality = new QDoubleSpinBox(properties);
    m_quality->setObjectName(QStringLiteral("MorphQuality"));
    m_quality->setRange(0.0, 1.0);
    m_quality->setSingleStep(0.05);
    m_quality->setDecimals(2);
    m_quality->setToolTip(tr("How finely the lines and outlines in between follow the key drawings (more: finer, slower)."));
    form->addRow(tr("Quality :"), m_quality);
    m_flatten = new QCheckBox(tr("Flatten"), properties);
    m_flatten->setObjectName(QStringLiteral("MorphFlatten"));
    m_flatten->setToolTip(tr("Used when morphing a sequence with semi-transparent or transparent colours: the areas in between of one "
                             "colour made one, so that they do not darken where they overlap."));
    form->addRow(QString(), m_flatten);
    m_easeIn = new QDoubleSpinBox(properties);
    m_easeIn->setObjectName(QStringLiteral("MorphEaseIn"));
    m_easeOut = new QDoubleSpinBox(properties);
    m_easeOut->setObjectName(QStringLiteral("MorphEaseOut"));
    for (QDoubleSpinBox* const spin : {m_easeIn, m_easeOut}) {
        spin->setRange(0.0, 1.0);
        spin->setSingleStep(0.1);
        spin->setDecimals(2);
    }
    form->addRow(tr("Ease In"), m_easeIn);
    form->addRow(tr("Ease Out"), m_easeOut);
    m_time = new QLabel(properties);
    m_time->setObjectName(QStringLiteral("MorphTime"));
    form->addRow(tr("Time"), m_time);
    body->addWidget(properties);

    // ---- Hints.
    QGroupBox* const hints = new QGroupBox(tr("Hints"), m_body);
    QFormLayout* const hintForm = new QFormLayout(hints);
    m_hintType = new QComboBox(hints);
    m_hintType->setObjectName(QStringLiteral("MorphHintType"));
    m_hintType->addItem(tr("Contour Hint"), int(Morph::Contour));
    m_hintType->addItem(tr("Zone Hint"), int(Morph::Zone));
    m_hintType->addItem(tr("Pencil Hint"), int(Morph::Pencil));
    m_hintType->addItem(tr("Appearing Point Hint"), int(Morph::Appearing));
    m_hintType->addItem(tr("Vanishing Point Hint"), int(Morph::Vanishing));
    m_hintType->addItem(tr("Delete Hint"), int(Canvas::kDeleteHints));
    m_hintType->setToolTip(tr("What a click on the first key drawing puts down (or, Delete Hint, what a click on a hint does)."));
    hintForm->addRow(tr("Hint Type"), m_hintType);
    QHBoxLayout* const hintButtons = new QHBoxLayout;
    QPushButton* const suggest = smallButton(tr("Suggest Hints"), QStringLiteral("MorphSuggestHints"),
                                             tr("Automatically sets hint points on your key drawings as a help tool."), hints);
    QPushButton* const clear = smallButton(tr("Delete All"), QStringLiteral("MorphDeleteHints"), tr("Delete All Morphing Hints"), hints);
    QPushButton* const swap = smallButton(tr("Switch Between Drawings"), QStringLiteral("MorphSwitchDrawings"),
                                          tr("Use to toggle between two key drawings in the morphing sequence. Useful while setting hints."), hints);
    hintButtons->addWidget(suggest);
    hintButtons->addWidget(clear);
    hintButtons->addWidget(swap);
    hintForm->addRow(hintButtons);
    m_hide = new QCheckBox(tr("Hide Hints"), hints);
    m_hide->setObjectName(QStringLiteral("MorphHideHints"));
    m_hide->setToolTip(tr("Temporarily hides the hints from the key drawings."));
    hintForm->addRow(QString(), m_hide);
    body->addWidget(hints);

    // ---- Morphing layers, each with its own timing and deformation path.
    QGroupBox* const layers = new QGroupBox(tr("Timing"), m_body);
    QVBoxLayout* const layerBox = new QVBoxLayout(layers);
    m_inPlace = new QCheckBox(tr("Show Layer In Place"), layers);
    m_inPlace->setObjectName(QStringLiteral("MorphLayerInPlace"));
    m_inPlace->setToolTip(tr("Enabling this option prevents the currently selected morphing layer from being shown on top of the others."));
    layerBox->addWidget(m_inPlace);
    m_layers = new QTableWidget(0, 4, layers);
    m_layers->setObjectName(QStringLiteral("MorphLayerTable"));
    m_layers->setHorizontalHeaderLabels({tr("Layer"), tr("Start"), tr("Stop"), tr("Path")});
    m_layers->horizontalHeader()->setStretchLastSection(true);
    m_layers->verticalHeader()->hide();
    m_layers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_layers->setMaximumHeight(140);
    m_layers->setToolTip(tr("The morphing layers: the shapes put in one change only into shapes of the same one, starting and stopping on "
                            "frames of their own, along a deformation path if one is chosen."));
    layerBox->addWidget(m_layers);
    QHBoxLayout* const layerButtons = new QHBoxLayout;
    QPushButton* const addLayer = smallButton(tr("Add"), QStringLiteral("MorphInspectorAddLayer"), tr("Add Morphing Layer"), layers);
    QPushButton* const removeLayer = smallButton(tr("Remove"), QStringLiteral("MorphInspectorRemoveLayer"), tr("Remove Selected Morphing Layer(s)"), layers);
    QPushButton* const upLayer = smallButton(QStringLiteral("↑"), QStringLiteral("MorphInspectorLayerUp"), tr("Move Selected Morphing Layer Up"), layers);
    QPushButton* const downLayer = smallButton(QStringLiteral("↓"), QStringLiteral("MorphInspectorLayerDown"), tr("Move Selected Morphing Layer Down"), layers);
    for (QPushButton* const b : {addLayer, removeLayer, upLayer, downLayer})
        layerButtons->addWidget(b);
    layerButtons->addStretch(1);
    layerBox->addLayout(layerButtons);
    body->addWidget(layers);

    // ---- Deformation paths.
    QGroupBox* const paths = new QGroupBox(tr("Deformation Paths"), m_body);
    QVBoxLayout* const pathBox = new QVBoxLayout(paths);
    m_paths = new QTableWidget(0, 4, paths);
    m_paths->setObjectName(QStringLiteral("MorphPathTable"));
    m_paths->setHorizontalHeaderLabels({tr("Name"), tr("Used"), tr("Translation"), tr("Rotation")});
    m_paths->horizontalHeader()->setStretchLastSection(true);
    m_paths->verticalHeader()->hide();
    m_paths->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paths->setMaximumHeight(110);
    pathBox->addWidget(m_paths);
    QHBoxLayout* const pathButtons = new QHBoxLayout;
    QPushButton* const addPath = smallButton(tr("Create Deformation Path"), QStringLiteral("MorphCreatePath"),
                                             tr("A path for the morphing layer chosen to go along, from where it is to where it goes; drag its points "
                                                "with the Morphing tool."), paths);
    QPushButton* const dropPath = smallButton(tr("Delete"), QStringLiteral("MorphDeletePath"), tr("Delete Selected Deformation Path(s)"), paths);
    pathButtons->addWidget(addPath);
    pathButtons->addWidget(dropPath);
    pathButtons->addStretch(1);
    pathBox->addLayout(pathButtons);
    body->addWidget(paths);
    top->addStretch(1);

    // ---- what they do.
    connect(m_quality, &QDoubleSpinBox::valueChanged, this, [this](double v) { change(tr("Morphing Quality"), [v](Morph& m) { m.quality = v; }); });
    connect(m_flatten, &QCheckBox::toggled, this, [this](bool on) { change(tr("Changed Morphing Flatten"), [on](Morph& m) { m.flatten = on; }); });
    connect(m_easeIn, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        change(tr("Change Morphing Ease In"), [v](Morph& m) {
            m.easeIn = v;
            m.velocity.clear();
        });
    });
    connect(m_easeOut, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        change(tr("Change Morphing Ease Out"), [v](Morph& m) {
            m.easeOut = v;
            m.velocity.clear();
        });
    });
    connect(m_hintType, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_filling)
            m_canvas->setHintMode(m_hintType->currentData().toInt());
    });
    connect(m_hide, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_filling)
            m_canvas->setHintsHidden(on);
    });
    connect(m_inPlace, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_filling)
            m_canvas->setMorphLayerInPlace(on);
    });
    connect(suggest, &QPushButton::clicked, this, [this] {
        if (int layer, index; current(layer, index))
            m_document->suggestMorphHints(layer, index);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        change(tr("Delete All Morphing Hints"), [](Morph& m) {
            m.hints.clear();
            m.hintTypes.clear();
        });
    });
    connect(swap, &QPushButton::clicked, this, &MorphInspector::switchRequested);
    connect(addLayer, &QPushButton::clicked, this, [this] {
        int added = -1;
        change(tr("Add Morphing Layer"), [&added](Morph& m) {
            m.groups.push_back({tr("Morphing Layer %1").arg(m.groups.size() + 1), {}, {}});
            added = int(m.groups.size()) - 1;
        });
        m_canvas->setMorphGroup(added);
        refresh();
    });
    connect(removeLayer, &QPushButton::clicked, this, [this] {
        QVector<int> const rows = chosenRows(m_layers);
        change(tr("Remove Morphing Layer(s)"), [rows](Morph& m) {
            for (int i = int(rows.size()) - 1; i >= 0; --i)
                if (rows[i] < m.groups.size())
                    m.groups.removeAt(rows[i]);
        });
    });
    auto move = [this](int by) {
        int const row = m_layers->currentRow();
        change(by < 0 ? tr("Move Morphing Layer Up") : tr("Move Morphing Layer Down"), [row, by](Morph& m) {
            if (row >= 0 && row + by >= 0 && row + by < m.groups.size())
                m.groups.swapItemsAt(row, row + by);
        });
        m_canvas->setMorphGroup(std::max(0, row + by));
        refresh();
    };
    connect(upLayer, &QPushButton::clicked, this, [move] { move(-1); });
    connect(downLayer, &QPushButton::clicked, this, [move] { move(1); });
    connect(m_layers, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!m_filling)
            m_canvas->setMorphGroup(row);
    });
    connect(m_layers, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_filling)
            return;
        int layer = -1, index = -1;
        if (!current(layer, index))
            return;
        Layer const& l = m_document->scene().layers[layer];
        int const from = l.morphs[index].from, to = l.morphEnd(index);
        int const row = item->row(), column = item->column();
        QString const text = item->text().trimmed();
        if (column == 0) {
            change(tr("Rename Morphing Layer"), [row, text](Morph& m) {
                if (row < m.groups.size() && !text.isEmpty())
                    m.groups[row].name = text;
            });
        } else if (column == 1 || column == 2) {
            // Start and Stop as frames of the scene, kept as how far into the morphing they are.
            bool ok = false;
            int const frame = text.toInt(&ok) - 1;
            double const at = ok && to > from ? std::clamp(double(frame - from) / double(to - from), 0.0, 1.0) : -1.0;
            if (at >= 0.0)
                change(column == 1 ? tr("Changed Start Time of Morphing Layer") : tr("Changed Stop Time of Morphing Layer"), [row, column, at](Morph& m) {
                    if (row < m.groups.size())
                        (column == 1 ? m.groups[row].start : m.groups[row].stop) = at;
                });
            else
                refresh();
        }
    });
    connect(addPath, &QPushButton::clicked, this, [this] {
        int layer = -1, index = -1;
        if (!current(layer, index))
            return;
        Layer const& l = m_document->scene().layers[layer];
        Morph const& morph = l.morphs[index];
        int const group = m_layers->currentRow();
        // From the middle of what the morphing layer is in the first drawing to its middle in the second, bowed a little.
        auto middle = [&](int frame, QVector<int> const* keys) {
            QRectF box;
            int const d = l.drawingAt(frame);
            if (d < 0 || d >= l.drawings.size())
                return QPointF();
            Drawing const& drawing = l.drawings[d];
            for (int sheet = 0; sheet < Drawing::SheetCount; ++sheet) {
                for (int i = 0; i < drawing.fillsOf(sheet).size(); ++i)
                    if (!keys || keys->contains(Morphing::shapeKey(sheet, false, i)))
                        box = box.united(drawing.fillsOf(sheet)[i].bounds());
                for (int i = 0; i < drawing.strokesOf(sheet).size(); ++i)
                    if (!keys || keys->contains(Morphing::shapeKey(sheet, true, i)))
                        box = box.united(drawing.strokesOf(sheet)[i].bounds());
            }
            return box.center();
        };
        bool const forGroup = group >= 0 && group < morph.groups.size();
        QPointF const a = middle(morph.from, forGroup ? &morph.groups[group].first : nullptr);
        QPointF const b = middle(l.morphEnd(index), forGroup ? &morph.groups[group].second : nullptr);
        QPointF const along = b - a;
        QPointF const bow = QPointF(-along.y(), along.x()) * 0.25 + (along == QPointF() ? QPointF(0, -40) : QPointF());
        change(tr("Add Deformation Path in Morphing"), [&](Morph& m) {
            Morph::Path path;
            path.name = tr("Path %1").arg(m.paths.size() + 1);
            path.points = QPolygonF({a, (a + b) / 2.0 + bow, b});
            m.paths.push_back(path);
            if (forGroup)
                m.groups[group].path = int(m.paths.size()) - 1;
        });
    });
    connect(dropPath, &QPushButton::clicked, this, [this] {
        QVector<int> const rows = chosenRows(m_paths);
        change(tr("Delete Deformation Path in Morphing"), [rows](Morph& m) {
            for (int i = int(rows.size()) - 1; i >= 0; --i) {
                int const gone = rows[i];
                if (gone >= m.paths.size())
                    continue;
                m.paths.removeAt(gone);
                for (Morph::Group& g : m.groups)
                    g.path = g.path == gone ? -1 : g.path > gone ? g.path - 1 : g.path;
            }
        });
    });
    connect(m_paths, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_filling)
            return;
        int const row = item->row(), column = item->column();
        bool const on = item->checkState() == Qt::Checked;
        QString const text = item->text().trimmed();
        change(column == 1 ? tr("Changed Deformation Path Usage for Morphing Layers") : tr("Modify Deformation Path"), [=](Morph& m) {
            if (row >= m.paths.size())
                return;
            if (column == 0 && !text.isEmpty())
                m.paths[row].name = text;
            else if (column == 1)
                m.paths[row].used = on;
            else if (column == 2)
                m.paths[row].translation = on;
            else if (column == 3)
                m.paths[row].rotation = on;
        });
    });
    connect(m_document, &Document::changed, this, &MorphInspector::refresh, Qt::QueuedConnection);
    connect(m_canvas, &Canvas::morphHereChanged, this, &MorphInspector::refresh);
    refresh();
}

bool MorphInspector::current(int& layer, int& index) const
{
    Scene const& scene = m_document->scene();
    layer = m_canvas->currentLayer();
    int const frame = m_canvas->currentFrame();
    index = -1;
    if (layer < 0 || layer >= scene.layers.size())
        return false;
    Layer const& l = scene.layers[layer];
    index = l.morphIndexAt(frame);
    if (index < 0)
        for (int i = 0; i < l.morphs.size(); ++i)
            if (l.morphEnd(i) == frame)
                index = i;
    return index >= 0;
}

void MorphInspector::change(QString const& text, std::function<void(Morph&)> const& edit)
{
    if (m_filling)
        return;
    int layer = -1, index = -1;
    if (!current(layer, index))
        return;
    Morph morph = m_document->scene().layers[layer].morphs[index];
    edit(morph);
    m_document->setMorph(layer, index, morph, text);
}

void MorphInspector::refresh()
{
    m_filling = true;
    int layer = -1, index = -1;
    bool const here = current(layer, index);
    m_body->setEnabled(here);
    m_layers->setRowCount(0);
    m_paths->setRowCount(0);
    if (!here) {
        m_current->setText(tr("No Morphing Selected"));
        m_time->clear();
    } else {
        Layer const& l = m_document->scene().layers[layer];
        Morph const& morph = l.morphs[index];
        int const from = morph.from, to = l.morphEnd(index), frame = m_canvas->currentFrame();
        m_current->setText(tr("Current Morphing: %1, frames %2 to %3").arg(l.name).arg(from + 1).arg(to + 1));
        m_time->setText(tr("frame %1 of %2 (from %3)").arg(frame - from + 1).arg(to - from + 1).arg(from + 1));
        m_quality->setValue(morph.quality);
        m_flatten->setChecked(morph.flatten);
        m_easeIn->setValue(morph.easeIn);
        m_easeOut->setValue(morph.easeOut);
        m_layers->setRowCount(int(morph.groups.size()));
        for (int r = 0; r < morph.groups.size(); ++r) {
            Morph::Group const& g = morph.groups[r];
            m_layers->setItem(r, 0, new QTableWidgetItem(g.name));
            m_layers->setItem(r, 1, new QTableWidgetItem(QString::number(from + int(std::lround(g.start * (to - from))) + 1)));
            m_layers->setItem(r, 2, new QTableWidgetItem(QString::number(from + int(std::lround(g.stop * (to - from))) + 1)));
            QComboBox* const path = new QComboBox(m_layers);
            path->addItem(tr("(none)"), -1);
            for (int p = 0; p < morph.paths.size(); ++p)
                path->addItem(morph.paths[p].name, p);
            path->setCurrentIndex(std::clamp(g.path + 1, 0, path->count() - 1));
            connect(path, &QComboBox::currentIndexChanged, this, [this, r, path](int) {
                int const chosen = path->currentData().toInt();
                change(tr("Changed Deformation Path Usage for Morphing Layers"), [r, chosen](Morph& m) {
                    if (r < m.groups.size())
                        m.groups[r].path = chosen;
                });
            });
            m_layers->setCellWidget(r, 3, path);
        }
        int const group = m_canvas->morphGroup();
        if (group >= 0 && group < m_layers->rowCount())
            m_layers->selectRow(group);
        m_paths->setRowCount(int(morph.paths.size()));
        for (int r = 0; r < morph.paths.size(); ++r) {
            m_paths->setItem(r, 0, new QTableWidgetItem(morph.paths[r].name));
            m_paths->setItem(r, 1, checkItem(morph.paths[r].used));
            m_paths->setItem(r, 2, checkItem(morph.paths[r].translation));
            m_paths->setItem(r, 3, checkItem(morph.paths[r].rotation));
        }
    }
    m_hintType->setCurrentIndex(std::max(0, m_hintType->findData(m_canvas->hintMode())));
    m_hide->setChecked(m_canvas->hintsHidden());
    m_inPlace->setChecked(m_canvas->morphLayerInPlace());
    m_filling = false;
}
