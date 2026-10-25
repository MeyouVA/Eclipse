// Eclipse - the Function Editor: one function column, edited by itself.
#include "ui/views/FunctionEditor.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>

#include "model/document/Commands.h"
#include "ui/timeline/FunctionView.h"

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

QString number(double v)
{
    return QString::number(v, 'g', 8);
}

double cellValue(QTableWidget* table, int row, int column, double fallback = 0.0, bool* empty = nullptr)
{
    QTableWidgetItem* const item = table->item(row, column);
    QString const text = item ? item->text().trimmed() : QString();
    if (empty)
        *empty = text.isEmpty();
    bool ok = false;
    double const v = text.toDouble(&ok);
    return ok ? v : fallback;
}

QTableWidget* makeTable(QWidget* parent, QStringList const& headers, QString const& name)
{
    QTableWidget* const table = new QTableWidget(0, int(headers.size()), parent);
    table->setObjectName(name);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    return table;
}

void fillRow(QTableWidget* table, int row, QVector<QString> const& cells)
{
    for (int c = 0; c < cells.size(); ++c)
        table->setItem(row, c, new QTableWidgetItem(cells[c]));
}

}  // namespace

FunctionEditor::FunctionEditor(Document* document, int column, QWidget* parent) : QDialog(parent), m_document(document), m_id(column)
{
    setObjectName(QStringLiteral("FunctionEditor"));
    FunctionColumn const* c = m_document->scene().function(m_id);
    int const kind = c ? c->kind : FunctionColumn::Bezier;
    static char const* const kTitles[] = {QT_TR_NOOP("Bezier Editor"), QT_TR_NOOP("Ease Editor"), QT_TR_NOOP("Velobased Editor"),
                                          QT_TR_NOOP("Expression Editor"), QT_TR_NOOP("3D Path Editor")};
    setWindowTitle(tr(kTitles[std::clamp(kind, 0, 4)]));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    QFormLayout* const top = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("FunctionName"));
    m_kind = new QLabel(this);
    top->addRow(tr("Name"), m_name);
    top->addRow(tr("Type"), m_kind);
    layout->addLayout(top);
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_filling && !m_document->renameFunction(m_id, m_name->text()))
            refresh();
    });

    if (kind == FunctionColumn::Bezier || kind == FunctionColumn::Ease) {
        // The curve itself, to work on as in the Function view.
        m_graph = new FunctionGraph(m_document, this);
        m_graph->setCurrentCell(-1, 0);
        m_graph->setColumns({m_document->scene().functionIndex(m_id)});
        layout->addWidget(m_graph, 2);
    }
    if (kind != FunctionColumn::Expression) {
        QStringList const headers = kind == FunctionColumn::Bezier ? QStringList{tr("Frame"), tr("Value"), tr("Left X"), tr("Left Y"), tr("Right X"), tr("Right Y")}
                                    : kind == FunctionColumn::Ease ? QStringList{tr("Frame"), tr("Value"), tr("Ease In"), tr("Angle In"), tr("Ease Out"), tr("Angle Out")}
                                    : kind == FunctionColumn::VeloBased ? QStringList{tr("Frame"), tr("Value")}
                                                                        : QStringList{tr("X"), tr("Y"), tr("Z"), tr("Frame"), tr("Tension"), tr("Continuity"), tr("Bias")};
        m_points = makeTable(this, headers, QStringLiteral("FunctionPoints"));
        layout->addWidget(m_points, 2);
        QHBoxLayout* const buttons = new QHBoxLayout;
        QPushButton* const add = new QPushButton(tr("Add Point"), this);
        add->setObjectName(QStringLiteral("FunctionAddPoint"));
        QPushButton* const remove = new QPushButton(tr("Remove Point"), this);
        remove->setObjectName(QStringLiteral("FunctionRemovePoint"));
        buttons->addWidget(add);
        buttons->addWidget(remove);
        connect(add, &QPushButton::clicked, this, &FunctionEditor::addPoint);
        connect(remove, &QPushButton::clicked, this, &FunctionEditor::removePoint);
        if (kind == FunctionColumn::Path3D) {
            QPushButton* const lock = new QPushButton(tr("Lock/Unlock Point"), this);
            lock->setObjectName(QStringLiteral("FunctionLockPoint"));
            connect(lock, &QPushButton::clicked, this, &FunctionEditor::toggleLock);
            buttons->addWidget(lock);
            m_constantZ = new QCheckBox(tr("Constant Z"), this);
            m_constantZ->setObjectName(QStringLiteral("FunctionConstantZ"));
            buttons->addWidget(m_constantZ);
            connect(m_constantZ, &QCheckBox::toggled, this, [this](bool on) {
                if (m_filling)
                    return;
                FunctionColumn const* now = m_document->scene().function(m_id);
                if (!now)
                    return;
                FunctionColumn changed = *now;
                changed.constantZ = on;
                m_document->setFunction(changed, tr("Set Constant Z"));
            });
        }
        buttons->addStretch(1);
        layout->addLayout(buttons);
        connect(m_points, &QTableWidget::cellChanged, this, [this] {
            if (!m_filling)
                commitPoints();
        });
    }
    if (kind == FunctionColumn::VeloBased || kind == FunctionColumn::Path3D) {
        // The velocity: how far along the way (percent) on a frame; none, steadily.
        layout->addWidget(new QLabel(tr("Velocity"), this));
        m_velocity = makeTable(this, {tr("Frame"), tr("Percent")}, QStringLiteral("FunctionVelocity"));
        layout->addWidget(m_velocity, 1);
        QHBoxLayout* const buttons = new QHBoxLayout;
        QPushButton* const add = new QPushButton(tr("Create Velocity Point"), this);
        add->setObjectName(QStringLiteral("FunctionAddVelocity"));
        QPushButton* const remove = new QPushButton(tr("Remove Velocity Point"), this);
        remove->setObjectName(QStringLiteral("FunctionRemoveVelocity"));
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch(1);
        layout->addLayout(buttons);
        connect(add, &QPushButton::clicked, this, &FunctionEditor::addVelocityPoint);
        connect(remove, &QPushButton::clicked, this, &FunctionEditor::removeVelocityPoint);
        connect(m_velocity, &QTableWidget::cellChanged, this, [this] {
            if (!m_filling)
                commitVelocity();
        });
    }
    if (kind == FunctionColumn::Expression) {
        m_expression = new QPlainTextEdit(this);
        m_expression->setObjectName(QStringLiteral("FunctionExpression"));
        m_expression->setPlaceholderText(tr("For example: 100 * sin(frame * 10)"));
        layout->addWidget(m_expression, 1);
        QPushButton* const apply = new QPushButton(tr("Set Expression Value"), this);
        apply->setObjectName(QStringLiteral("FunctionSetExpression"));
        connect(apply, &QPushButton::clicked, this, &FunctionEditor::commitExpression);
        layout->addWidget(apply);
    }
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("FunctionMessage"));
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    // (Queued: a table is not refilled while its own cell is being changed.)
    connect(m_document, &Document::changed, this, &FunctionEditor::refresh, Qt::QueuedConnection);
    resize(460, kind == FunctionColumn::Expression ? 260 : 460);
    refresh();
}

void FunctionEditor::setCurrentFrame(int frame)
{
    m_frame = frame;
    if (m_graph)
        m_graph->setCurrentCell(-1, frame);
    refresh();
}

void FunctionEditor::refresh()
{
    Scene const& scene = m_document->scene();
    FunctionColumn const* c = scene.function(m_id);
    if (!c) {
        m_message->setText(tr("This function column is gone."));
        setEnabled(false);
        return;
    }
    setEnabled(true);
    m_filling = true;
    if (!m_name->hasFocus())
        m_name->setText(c->name);
    m_kind->setText(FunctionColumn::kindName(c->kind));
    if (m_graph)
        m_graph->setColumns({scene.functionIndex(m_id)});
    if (m_points) {
        int const row = m_points->currentRow();
        if (c->kind == FunctionColumn::Path3D) {
            m_points->setRowCount(int(c->path.size()));
            for (int i = 0; i < c->path.size(); ++i) {
                FunctionColumn::PathPoint const& p = c->path[i];
                fillRow(m_points, i, {number(p.x), number(p.y), number(p.z), p.timeLocked() ? QString::number(p.frame + 1) : QString(), number(p.tension),
                                      number(p.continuity), number(p.bias)});
            }
        } else {
            Layer const& l = c->curve;
            m_points->setRowCount(int(l.keys.size()));
            for (int k = 0; k < l.keys.size(); ++k) {
                Keyframe const& key = l.keys[k];
                QVector<QString> cells{QString::number(key.frame + 1), number(key.pose.x)};
                if (c->kind == FunctionColumn::Bezier || c->kind == FunctionColumn::Ease) {
                    ValueCurve const curve = l.curveAt(Driver::X, k);
                    if (c->kind == FunctionColumn::Bezier) {
                        cells << number(curve.in.x()) << number(curve.in.y()) << number(curve.out.x()) << number(curve.out.y());
                    } else {
                        // The ease: how many frames the handle reaches, and the angle it leaves or arrives at (value against frames).
                        auto angle = [](QPointF h) { return std::abs(h.x()) < 1e-12 ? 0.0 : std::atan(h.y() / h.x()) / kDegree; };
                        cells << number(-curve.in.x()) << number(angle(curve.in)) << number(curve.out.x()) << number(angle(curve.out));
                    }
                }
                fillRow(m_points, k, cells);
            }
        }
        if (row >= 0 && row < m_points->rowCount())
            m_points->selectRow(row);
    }
    if (m_constantZ)
        m_constantZ->setChecked(c->constantZ);
    if (m_velocity) {
        m_velocity->setRowCount(int(c->velocity.keys.size()));
        for (int k = 0; k < c->velocity.keys.size(); ++k)
            fillRow(m_velocity, k, {QString::number(c->velocity.keys[k].frame + 1), number(c->velocity.keys[k].pose.x)});
    }
    if (m_expression && !m_expression->hasFocus() && m_expression->toPlainText() != c->expression)
        m_expression->setPlainText(c->expression);
    // What it comes to on the current frame (or why it cannot).
    QString error;
    double const v = c->valueAt(m_frame, &scene, &error);
    m_message->setText(error.isEmpty() ? tr("Frame %1: %2").arg(m_frame + 1).arg(number(v)) : error);
    m_filling = false;
}

void FunctionEditor::commitPoints()
{
    Scene const& scene = m_document->scene();
    FunctionColumn const* now = scene.function(m_id);
    if (!now || !m_points)
        return;
    FunctionColumn changed = *now;
    int const last = scene.frameCount() - 1;
    int const firstAllowed = changed.extendedTiming ? -100000000 : 0;
    int const lastAllowed = changed.extendedTiming ? 100000000 : last;
    QString text;
    if (changed.kind == FunctionColumn::Path3D) {
        changed.path.clear();
        for (int r = 0; r < m_points->rowCount(); ++r) {
            FunctionColumn::PathPoint p;
            p.x = cellValue(m_points, r, 0);
            p.y = cellValue(m_points, r, 1);
            p.z = cellValue(m_points, r, 2);
            bool unlocked = false;
            double const frame = cellValue(m_points, r, 3, 0.0, &unlocked);
            p.frame = unlocked ? -1 : std::clamp(int(std::lround(frame)) - 1, firstAllowed, lastAllowed);
            p.locked = !unlocked;
            p.tension = std::clamp(cellValue(m_points, r, 4), -1.0, 1.0);
            p.continuity = std::clamp(cellValue(m_points, r, 5), -1.0, 1.0);
            p.bias = std::clamp(cellValue(m_points, r, 6), -1.0, 1.0);
            changed.path.push_back(p);
        }
        text = tr("Set 3D Path Point");
    } else {
        // The rows by frame (one point a frame), each with its handles.
        QMap<int, Keyframe> byFrame;
        for (int r = 0; r < m_points->rowCount(); ++r) {
            int const old = r < now->curve.keys.size() ? r : -1;
            Keyframe k = old >= 0 ? now->curve.keys[old] : Keyframe();
            k.frame = std::clamp(int(std::lround(cellValue(m_points, r, 0, 1.0))) - 1, firstAllowed, lastAllowed);
            k.pose.x = cellValue(m_points, r, 1);
            k.parts = Keyframe::PartX;
            if (changed.kind == FunctionColumn::Bezier || changed.kind == FunctionColumn::Ease) {
                ValueCurve curve = old >= 0 ? now->curve.curveAt(Driver::X, old) : ValueCurve();
                curve.value = Driver::X;
                if (changed.kind == FunctionColumn::Bezier) {
                    curve.in = QPointF(std::min(0.0, cellValue(m_points, r, 2)), cellValue(m_points, r, 3));
                    curve.out = QPointF(std::max(0.0, cellValue(m_points, r, 4)), cellValue(m_points, r, 5));
                } else {
                    double const inFrames = std::max(0.0, cellValue(m_points, r, 2)), outFrames = std::max(0.0, cellValue(m_points, r, 4));
                    double const inAngle = std::clamp(cellValue(m_points, r, 3), -89.9, 89.9), outAngle = std::clamp(cellValue(m_points, r, 5), -89.9, 89.9);
                    curve.in = QPointF(-inFrames, -inFrames * std::tan(inAngle * kDegree));
                    curve.out = QPointF(outFrames, outFrames * std::tan(outAngle * kDegree));
                }
                curve.corner = true;
                k.setCurve(curve);
            }
            byFrame.insert(k.frame, k);
        }
        changed.curve.keys = byFrame.values().toVector();
        text = changed.kind == FunctionColumn::Ease ? tr("Set Ease Point") : changed.kind == FunctionColumn::Bezier ? tr("Set Bezier Point") : tr("Set Velocity Point");
    }
    m_document->setFunction(changed, text);
}

void FunctionEditor::commitVelocity()
{
    Scene const& scene = m_document->scene();
    FunctionColumn const* now = scene.function(m_id);
    if (!now || !m_velocity)
        return;
    FunctionColumn changed = *now;
    QMap<int, Keyframe> byFrame;
    for (int r = 0; r < m_velocity->rowCount(); ++r) {
        Keyframe k = r < now->velocity.keys.size() ? now->velocity.keys[r] : Keyframe();
        k.frame = std::clamp(int(std::lround(cellValue(m_velocity, r, 0, 1.0))) - 1,
                             changed.extendedTiming ? -100000000 : 0, changed.extendedTiming ? 100000000 : scene.frameCount() - 1);
        k.pose.x = std::clamp(cellValue(m_velocity, r, 1), 0.0, 100.0);
        k.parts = Keyframe::PartX;
        byFrame.insert(k.frame, k);
    }
    changed.velocity.keys = byFrame.values().toVector();
    m_document->setFunction(changed, tr("Set Velocity Point"));
}

void FunctionEditor::commitExpression()
{
    FunctionColumn const* now = m_document->scene().function(m_id);
    if (!now || !m_expression)
        return;
    FunctionColumn changed = *now;
    changed.expression = m_expression->toPlainText().trimmed();
    m_document->setFunction(changed, tr("Set Expression Value"));
}

void FunctionEditor::addPoint()
{
    // A point on the current frame (a 3D path: where the path is there, kept to that frame).
    Scene const& scene = m_document->scene();
    FunctionColumn const* now = scene.function(m_id);
    if (!now)
        return;
    FunctionColumn changed = *now;
    if (changed.kind == FunctionColumn::Path3D) {
        QVector3D const at = now->pathAt(m_frame, scene.frameCount() - 1);
        FunctionColumn::PathPoint p{at.x(), at.y(), at.z(), m_frame};
        int place = int(changed.path.size());
        for (int i = 0; i < changed.path.size(); ++i)
            if (changed.path[i].frame > m_frame) {
                place = i;
                break;
            }
        changed.path.insert(place, p);
        m_document->setFunction(changed, tr("Set 3D Path Point"));
        return;
    }
    if (changed.curve.keyIndex(m_frame) >= 0)
        return;
    Keyframe k;
    k.frame = m_frame;
    k.pose.x = now->valueAt(m_frame, &scene);
    k.parts = Keyframe::PartX;
    int at = 0;
    while (at < changed.curve.keys.size() && changed.curve.keys[at].frame < m_frame)
        ++at;
    changed.curve.keys.insert(at, k);
    m_document->setFunction(changed, changed.kind == FunctionColumn::Ease ? tr("Set Ease Point") : tr("Set Bezier Point"));
}

void FunctionEditor::removePoint()
{
    FunctionColumn const* now = m_document->scene().function(m_id);
    int const row = m_points ? m_points->currentRow() : -1;
    if (!now || row < 0)
        return;
    FunctionColumn changed = *now;
    if (changed.kind == FunctionColumn::Path3D) {
        if (row >= changed.path.size())
            return;
        changed.path.removeAt(row);
    } else {
        if (row >= changed.curve.keys.size())
            return;
        changed.curve.keys.removeAt(row);
    }
    m_document->setFunction(changed, changed.kind == FunctionColumn::Ease ? tr("Remove Ease Point") : tr("Remove Bezier Point"));
}

void FunctionEditor::addVelocityPoint()
{
    FunctionColumn const* now = m_document->scene().function(m_id);
    if (!now || now->velocity.keyIndex(m_frame) >= 0)
        return;
    FunctionColumn changed = *now;
    // As far along as it is now on that frame.
    Keyframe k;
    k.frame = m_frame;
    k.parts = Keyframe::PartX;
    k.pose.x = changed.velocity.keys.isEmpty() ? 0.0 : changed.velocity.curveValue(Driver::X, changed.velocity.keysOf(Driver::X), m_frame);
    // (Preferences: Default Bezier Velocity off - an ease in and out of a third.)
    if (!FunctionColumn::defaultBezierVelocity)
        k.easeIn = k.easeOut = 0.33;
    int at = 0;
    while (at < changed.velocity.keys.size() && changed.velocity.keys[at].frame < m_frame)
        ++at;
    changed.velocity.keys.insert(at, k);
    m_document->setFunction(changed, tr("Create Velocity Point"));
}

void FunctionEditor::removeVelocityPoint()
{
    FunctionColumn const* now = m_document->scene().function(m_id);
    int const row = m_velocity ? m_velocity->currentRow() : -1;
    if (!now || row < 0 || row >= now->velocity.keys.size())
        return;
    FunctionColumn changed = *now;
    changed.velocity.keys.removeAt(row);
    m_document->setFunction(changed, tr("Remove Velocity Point"));
}

void FunctionEditor::toggleLock()
{
    FunctionColumn const* now = m_document->scene().function(m_id);
    int const row = m_points ? m_points->currentRow() : -1;
    if (!now || now->kind != FunctionColumn::Path3D || row < 0 || row >= now->path.size())
        return;
    FunctionColumn changed = *now;
    bool const lock = !changed.path[row].timeLocked();
    changed.path[row].frame = lock ? m_frame : -1;
    changed.path[row].locked = lock;
    m_document->setFunction(changed, tr("Lock/Unlock Point"));
}
