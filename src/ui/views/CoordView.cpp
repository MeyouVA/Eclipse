// Eclipse - the Coord. and Control Points view: the current layer on the current frame as numbers, and its keyframes as a table.
#include "ui/views/CoordView.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolButton>
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
// The boxes of the form.
enum CoordField { FieldX, FieldY, FieldZ, FieldScaleX, FieldScaleY, FieldAngle, FieldSkew, FieldDepth, FieldCount };
}

// A number as short as it can be written, to four places.
static QString shortNumber(double value)
{
    QString text = QString::number(value, 'f', 4);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

// How many pixels a field is: twelve of them from the middle of the picture to its top, and
// across the picture four thirds as wide (the 4:3 field chart).
static double fieldSize(int axis, double sceneHeight)
{
    double const up = sceneHeight / 24.0;
    return axis == 0 ? up * 4.0 / 3.0 : up;
}

QString CoordView::fieldText(double pixels, int axis, double sceneHeight)
{
    double const size = fieldSize(axis, sceneHeight);
    if (size <= 0.0)
        return QStringLiteral("0");
    // Across: east is to the right.  Up and down: the scene counts downwards, north is up.
    // Depth: forward is towards the camera.
    double const fields = (axis == 1 ? -pixels : pixels) / size;
    static char const* const letters[3][2] = {{"E", "W"}, {"N", "S"}, {"F", "B"}};
    QString const number = shortNumber(std::abs(fields));
    if (number == QLatin1String("0"))
        return number;
    return number + QLatin1Char(' ') + QLatin1String(letters[axis][fields < 0.0 ? 1 : 0]);
}

bool CoordView::fieldValue(QString const& text, int axis, double sceneHeight, double* pixels)
{
    static QRegularExpression const pattern(QStringLiteral("^\\s*([-+]?[0-9]*[.,]?[0-9]+)\\s*([a-zA-Z]?)\\s*$"));
    QRegularExpressionMatch const match = pattern.match(text);
    if (!match.hasMatch())
        return false;
    bool ok = false;
    double fields = match.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok)
        return false;
    static char const* const letters[3] = {"EW", "NS", "FB"};
    QString const letter = match.captured(2).toUpper();
    if (!letter.isEmpty()) {
        if (letter[0] == QLatin1Char(letters[axis][1]))
            fields = -fields;
        else if (letter[0] != QLatin1Char(letters[axis][0]))
            return false;       // the letter of another direction
    }
    *pixels = (axis == 1 ? -fields : fields) * fieldSize(axis, sceneHeight);
    return true;
}

// A part of the form under a heading that folds it away.
QWidget* CoordView::section(QString const& title, char const* objectName, QFormLayout** form)
{
    QWidget* const whole = new QWidget(this);
    QToolButton* const heading = new QToolButton(whole);
    heading->setObjectName(QLatin1String(objectName));
    heading->setText(title);
    heading->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    heading->setArrowType(Qt::DownArrow);
    heading->setAutoRaise(true);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);
    QWidget* const body = new QWidget(whole);
    *form = new QFormLayout(body);
    (*form)->setContentsMargins(12, 0, 0, 4);
    QVBoxLayout* const column = new QVBoxLayout(whole);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    column->addWidget(heading);
    column->addWidget(body);
    connect(heading, &QToolButton::clicked, body, [heading, body] {
        bool const open = body->isHidden();
        heading->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        body->setVisible(open);
    });
    return whole;
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
    // At the left, the layer as it is on the current frame: Selection Coordinates.
    QWidget* const coordinates = new QWidget(this);
    coordinates->setObjectName(QStringLiteral("SelectionCoordinates"));
    {
        QVBoxLayout* const column = new QVBoxLayout(coordinates);
        column->setContentsMargins(0, 0, 8, 0);
        column->setSpacing(2);
        QLabel* const heading = new QLabel(tr("Selection Coordinates"), coordinates);
        column->addWidget(heading);
        m_name = new QLineEdit(coordinates);
        m_name->setObjectName(QStringLiteral("CoordName"));
        QFormLayout* const top = new QFormLayout;
        top->addRow(tr("Name"), m_name);
        column->addLayout(top);
        connect(m_name, &QLineEdit::editingFinished, this, [this] {
            Scene const& scene = m_document->scene();
            if (m_layer < 0 || m_layer >= scene.layers.size())
                return;
            QString const wanted = m_name->text().trimmed();
            if (!wanted.isEmpty() && wanted != scene.layers[m_layer].name)
                m_document->renameLayer(m_layer, wanted);
            showCoordinates();      // names stay unique: what it was given is shown
        });
        auto box = [this, coordinates](int which, char const* objectName) {
            QLineEdit* const edit = new QLineEdit(coordinates);
            edit->setObjectName(QLatin1String(objectName));
            edit->setMaximumWidth(140);
            m_fields[which] = edit;
            connect(edit, &QLineEdit::editingFinished, this, [this, which] { applyCoordinate(which); });
            return edit;
        };
        QFormLayout* form = nullptr;
        column->addWidget(section(tr("Position"), "CoordPositionHeading", &form));
        form->addRow(tr("x"), box(FieldX, "CoordPosX"));
        form->addRow(tr("y"), box(FieldY, "CoordPosY"));
        form->addRow(tr("z"), box(FieldZ, "CoordPosZ"));
        form->addRow(tr("Cumulative z"), box(FieldDepth, "CoordCumulativeZ"));
        m_fields[FieldDepth]->setReadOnly(true);
        m_fields[FieldDepth]->setToolTip(tr("How far the layer stands from where the scene's layers stand, with all it hangs under counted in."));
        column->addWidget(section(tr("Scale"), "CoordScaleHeading", &form));
        m_separateScale = new QCheckBox(coordinates);
        m_separateScale->setObjectName(QStringLiteral("CoordSeparateScale"));
        m_separateScale->setChecked(true);
        m_separateScale->setToolTip(tr("Off: a number typed for one of the two is given to both."));
        form->addRow(tr("Separate Scale"), m_separateScale);
        form->addRow(tr("Scale X"), box(FieldScaleX, "CoordScaleX"));
        form->addRow(tr("Scale Y"), box(FieldScaleY, "CoordScaleY"));
        column->addWidget(section(tr("Rotation"), "CoordRotationHeading", &form));
        form->addRow(tr("Angle"), box(FieldAngle, "CoordAngle"));
        column->addWidget(section(tr("Skew"), "CoordSkewHeading", &form));
        form->addRow(tr("Skew"), box(FieldSkew, "CoordSkew"));
        column->addStretch(1);
    }
    // At the right, its keyframes.
    QVBoxLayout* const keys = new QVBoxLayout;
    keys->addWidget(m_title);
    keys->addWidget(m_table, 1);
    keys->addLayout(buttons);
    QHBoxLayout* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(coordinates);
    layout->addLayout(keys, 1);
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
    showCoordinates();
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

void CoordView::showCoordinates()
{
    Scene const& scene = m_document->scene();
    bool const any = m_layer >= 0 && m_layer < scene.layers.size();
    m_name->setEnabled(any);
    for (QLineEdit* field : m_fields)
        field->setEnabled(any);
    if (!any) {
        m_name->clear();
        for (QLineEdit* field : m_fields)
            field->clear();
        return;
    }
    Layer const& layer = scene.layers[m_layer];
    Pose const pose = layer.poseAt(m_frame);
    // (A box that is being typed in is left alone.)
    auto show = [](QLineEdit* edit, QString const& text) {
        if (!edit->hasFocus() || edit->isReadOnly())
            edit->setText(text);
    };
    show(m_name, layer.name);
    show(m_fields[FieldX], fieldText(pose.x, 0, scene.height));
    show(m_fields[FieldY], fieldText(pose.y, 1, scene.height));
    show(m_fields[FieldZ], fieldText(pose.z, 2, scene.height));
    show(m_fields[FieldScaleX], shortNumber(pose.scaleX));
    show(m_fields[FieldScaleY], shortNumber(pose.scaleY));
    show(m_fields[FieldAngle], shortNumber(pose.rotation));
    show(m_fields[FieldSkew], shortNumber(pose.skew));
    double const field = scene.height / 24.0;
    m_fields[FieldDepth]->setText(QString::number(field > 0.0 ? scene.depthOf(m_layer, m_frame) / field : 0.0, 'f', 4));
}

void CoordView::applyCoordinate(int which)
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || which < 0 || which >= FieldDepth)
        return;
    QLineEdit* const edit = m_fields[which];
    Pose pose = scene.layers[m_layer].poseAt(m_frame);
    Pose const was = pose;
    double value = 0.0;
    bool ok = false;
    if (which <= FieldZ) {
        ok = fieldValue(edit->text(), which, scene.height, &value);
    } else {
        value = edit->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    }
    if (ok) {
        switch (which) {
        case FieldX: pose.x = value; break;
        case FieldY: pose.y = value; break;
        case FieldZ: pose.z = value; break;
        case FieldAngle: pose.rotation = value; break;
        case FieldSkew: pose.skew = std::clamp(value, -89.0, 89.0); break;
        default:
            // A scale of nothing could not be dragged back with the Transform tool.
            value = std::abs(value) < 0.001 ? 0.001 : value;
            if (which == FieldScaleX || !m_separateScale->isChecked())
                pose.scaleX = value;
            if (which == FieldScaleY || !m_separateScale->isChecked())
                pose.scaleY = value;
            break;
        }
    }
    // What is not a number, or changes nothing, leaves the layer as it is and shows what is there.
    bool const typing = edit->hasFocus();
    if (typing)
        edit->clearFocus();
    if (ok && !(pose == was))
        m_document->setKeyframe(m_layer, m_frame, pose, tr("Change Coordinates"));
    showCoordinates();
    if (typing)
        edit->setFocus();
}

void CoordView::rebuild()
{
    Scene const& scene = m_document->scene();
    showCoordinates();
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
