// Eclipse - the main window: more exposure commands - Set Exposure To, Hold N Cells, Fill Cells Randomly, Extend Exposure to a frame or the scene's end.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QRadioButton>
#include <QStatusBar>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

void MainWindow::cellRange(int l, int* first, int* last) const
{
    // The frames selected; with one cell only, from it to the last frame the layer shows a drawing on.
    Scene const& scene = m_document->scene();
    *first = m_timeline->selectionFirstFrame();
    *last = m_timeline->selectionLastFrame();
    if (*last > *first || l < 0 || l >= scene.layers.size())
        return;
    Layer const& layer = scene.layers[l];
    *last = *first;
    for (int f = int(layer.cells.size()) - 1; f > *first; --f)
        if (layer.cells[f] != Layer::kNoDrawing) {
            *last = f;
            break;
        }
}

void MainWindow::onActionSetExposureAsk()
{
    bool ok = false;
    int const length = QInputDialog::getInt(this, tr("Set Exposure"), tr("Set Exposure to:"), 2, 1, 999, 1, &ok);
    if (ok)
        onActionSetExposure(length);
}

int MainWindow::holdCells(int every)
{
    // Each drawing of the frames selected held for so many frames, the selection as long as it was: on twos, on threes.
    if (every < 1)
        return 0;
    Scene const& scene = m_document->scene();
    QVector<int> const layers = exposureLayers();
    int changed = 0;
    m_document->beginMacro(tr("Hold Exposure"));
    for (int l : layers) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        int first = 0, last = 0;
        cellRange(l, &first, &last);
        QVector<int> const cells = m_document->scene().layers[l].cells;
        QVector<int> held;
        for (int f = first; f <= last && f < cells.size(); ++f)
            held.push_back(cells[first + ((f - first) / every) * every]);
        if (held != cells.mid(first, held.size())) {
            m_document->setCells(l, first, held, tr("Hold Exposure"));
            ++changed;
        }
    }
    m_document->endMacro();
    return changed;
}

void MainWindow::onActionHoldCells(QString const& every)
{
    int count = every.toInt();
    if (count < 1) {
        bool ok = false;
        count = QInputDialog::getInt(this, tr("Hold Exposure"), tr("Hold each drawing for (frames):"), 2, 1, 99, 1, &ok);
        if (!ok)
            return;
    }
    if (holdCells(count) == 0)
        statusBar()->showMessage(tr("Hold Exposure: nothing to change in the frames selected."), 4000);
}

bool MainWindow::fillCellsRandomly(int l, int first, int last, int shortest, int longest, quint32 seed)
{
    // The layer's drawings, in no order, each held between so many frames and so many, never the same twice running.
    Scene const& scene = m_document->scene();
    if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings() || scene.layers[l].locked || scene.layers[l].drawings.isEmpty() || last < first)
        return false;
    int const count = int(scene.layers[l].drawings.size());
    shortest = std::max(1, shortest);
    longest = std::max(shortest, longest);
    QRandomGenerator random(seed);
    QVector<int> cells;
    int previous = -1;
    while (cells.size() < last - first + 1) {
        int drawing = random.bounded(count);
        if (count > 1 && drawing == previous)
            drawing = (drawing + 1 + random.bounded(count - 1)) % count;
        int const hold = shortest + random.bounded(longest - shortest + 1);
        for (int i = 0; i < hold && cells.size() < last - first + 1; ++i)
            cells.push_back(drawing);
        previous = drawing;
    }
    m_document->setCells(l, first, cells, tr("Fill Cells Randomly"));
    return true;
}

void MainWindow::onActionFillCellsRandomly()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Fill Cells Randomly"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const shortest = new QSpinBox(&dialog);
    shortest->setObjectName(QStringLiteral("RandomShortest"));
    shortest->setRange(1, 99);
    shortest->setValue(1);
    QSpinBox* const longest = new QSpinBox(&dialog);
    longest->setObjectName(QStringLiteral("RandomLongest"));
    longest->setRange(1, 99);
    longest->setValue(2);
    form->addRow(tr("Each drawing held at least (frames)"), shortest);
    form->addRow(tr("... and at most (frames)"), longest);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    quint32 const seed = QRandomGenerator::global()->generate();
    m_document->beginMacro(tr("Fill Cells Randomly"));
    for (int l : exposureLayers()) {
        int const first = m_timeline->selectionFirstFrame();
        int const last = std::max(m_timeline->selectionLastFrame(), first);
        fillCellsRandomly(l, first, last, shortest->value(), longest->value(), seed + quint32(l));
    }
    m_document->endMacro();
}

bool MainWindow::extendExposureTo(int l, int from, int to)
{
    return m_document->extendExposureTo(l, from, to);
}

void MainWindow::onActionExtendExposureTo()
{
    int const first = m_timeline->selectionFirstFrame();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ExtendExposureDialog"));
    dialog.setWindowTitle(tr("Extend Exposure"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QSpinBox* const to = new QSpinBox(&dialog);
    to->setObjectName(QStringLiteral("ExposureToFrame"));
    to->setRange(first + 1, 999999);
    to->setValue(first + 2);
    QRadioButton* const overwrite = new QRadioButton(tr("Overwrite later exposures"), &dialog);
    overwrite->setObjectName(QStringLiteral("ExposureOverwrite"));
    overwrite->setChecked(true);
    QRadioButton* const insert = new QRadioButton(tr("Insert and shift later exposures"), &dialog);
    insert->setObjectName(QStringLiteral("ExposureInsert"));
    form->addRow(tr("Extend to frame"), to);
    form->addRow(overwrite);
    form->addRow(insert);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    bool changed = false;
    int const end = to->value() - 1;
    bool const shifting = insert->isChecked();
    onExposure(tr("Extend Exposure"), [this, end, shifting, &changed](int l, int from) {
        changed = m_document->extendExposureTo(l, from, end, shifting) || changed;
    });
    if (!changed)
        statusBar()->showMessage(tr("No exposure changed. Select an unlocked drawing layer and a later frame."), 4000);
}

void MainWindow::onActionExtendExposureToEnd()
{
    // Extend Exposure for Scene Duration: on to the scene's last frame.
    int const end = m_document->scene().frameCount() - 1;
    onExposure(tr("Extend Exposure"), [this, end](int l, int from) { m_document->extendExposureTo(l, from, end); });
}
