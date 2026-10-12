// Eclipse - the Coord. and Control Points view: the keyframes of the current layer as a table of numbers.
#include "ui/views/CoordView.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "ui/canvas/Canvas.h"

namespace {
// The columns of the table.
enum CoordColumn { ColFrame, ColX, ColY, ColZ, ColAngle, ColScaleX, ColScaleY, ColCount };
}

CoordView::CoordView(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("CoordTitle"));
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setObjectName(QStringLiteral("CoordTable"));
    m_table->setHorizontalHeaderLabels({tr("Frame"), tr("X"), tr("Y"), tr("Z"), tr("Angle"), tr("Scale X"), tr("Scale Y")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    QPushButton* const add = new QPushButton(tr("Add Keyframe"), this);
    add->setObjectName(QStringLiteral("CoordAdd"));
    add->setToolTip(tr("A keyframe on the current frame, with the pose the layer has there."));
    QPushButton* const remove = new QPushButton(tr("Delete Keyframe"), this);
    remove->setObjectName(QStringLiteral("CoordRemove"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_title);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        if (m_layer >= 0 && m_layer < scene.layers.size() && scene.layers[m_layer].keyIndex(m_frame) < 0)
            m_document->setKeyframe(m_layer, m_frame, scene.layers[m_layer].poseAt(m_frame), tr("Add Keyframe"));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        Scene const& scene = m_document->scene();
        int const row = m_table->currentRow();
        if (m_layer >= 0 && m_layer < scene.layers.size() && row >= 0 && row < scene.layers[m_layer].keys.size())
            m_document->removeKeyframe(m_layer, scene.layers[m_layer].keys[row].frame);
    });
    connect(m_table, &QTableWidget::cellChanged, this, &CoordView::cellEdited);
    // A row chosen is that keyframe's frame.
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int before, int) {
        Scene const& scene = m_document->scene();
        if (m_rebuilding || row == before || m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].keys.size())
            return;
        emit framePicked(scene.layers[m_layer].keys[row].frame);
    });
    connect(m_document, &Document::changed, this, &CoordView::rebuild);
    rebuild();
}

void CoordView::setCurrentCell(int layer, int frame)
{
    bool const other = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (other) {
        rebuild();
        return;
    }
    // The same layer: only the row of the frame is shown as the one in hand.
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return;
    int const key = scene.layers[m_layer].keyIndex(m_frame);
    m_rebuilding = true;
    if (key >= 0 && key < m_table->rowCount())
        m_table->selectRow(key);
    else
        m_table->clearSelection();
    m_rebuilding = false;
}

void CoordView::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    m_table->setRowCount(0);
    if (m_layer < 0 || m_layer >= scene.layers.size()) {
        m_title->setText(tr("No layer"));
        m_rebuilding = false;
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    m_title->setText(layer.keys.isEmpty() ? tr("%1 has no keyframes: it does not move.").arg(layer.name) : tr("%1: %n keyframe(s)", nullptr, int(layer.keys.size())).arg(layer.name));
    m_table->setRowCount(int(layer.keys.size()));
    for (int row = 0; row < layer.keys.size(); ++row) {
        Keyframe const& key = layer.keys[row];
        double const values[ColCount] = {double(key.frame + 1), key.pose.x, key.pose.y, key.pose.z, key.pose.rotation, key.pose.scaleX, key.pose.scaleY};
        for (int column = 0; column < ColCount; ++column) {
            QString text = QString::number(values[column], 'f', column == ColFrame ? 0 : 3);
            if (column != ColFrame) {
                while (text.endsWith(QLatin1Char('0')))
                    text.chop(1);
                if (text.endsWith(QLatin1Char('.')))
                    text.chop(1);
            }
            QTableWidgetItem* const item = new QTableWidgetItem(text == QLatin1String("-0") ? QStringLiteral("0") : text);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, column, item);
        }
    }
    int const key = layer.keyIndex(m_frame);
    if (key >= 0)
        m_table->selectRow(key);
    m_rebuilding = false;
}

void CoordView::cellEdited(int row, int column)
{
    Scene const& scene = m_document->scene();
    if (m_rebuilding || m_layer < 0 || m_layer >= scene.layers.size() || row < 0 || row >= scene.layers[m_layer].keys.size())
        return;
    Keyframe const key = scene.layers[m_layer].keys[row];
    bool ok = false;
    double const value = m_table->item(row, column)->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok) {
        rebuild();      // not a number: what was there is shown again
        return;
    }
    if (column == ColFrame) {
        // The keyframe goes to another frame - if that frame is in the scene and has none.
        int const to = int(std::lround(value)) - 1;
        if (to == key.frame || to < 0 || to >= scene.frameCount() || !m_document->moveKeyframe(m_layer, key.frame, to))
            rebuild();
        return;
    }
    Pose pose = key.pose;
    double* const fields[ColCount] = {nullptr, &pose.x, &pose.y, &pose.z, &pose.rotation, &pose.scaleX, &pose.scaleY};
    // A scale of nothing could not be dragged back with the Transform tool.
    *fields[column] = (column == ColScaleX || column == ColScaleY) && std::abs(value) < 0.001 ? 0.001 : value;
    if (pose == key.pose) {
        rebuild();
        return;
    }
    m_document->setKeyframe(m_layer, key.frame, pose, tr("Change Keyframe"));
}
