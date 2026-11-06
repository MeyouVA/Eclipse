// Eclipse - Pose Controllers.
#include "ui/views/ControllerView.h"
#include "ui/views/MasterControls.h"
#include <QPlainTextEdit>
#include <QColorDialog>
#include <QBoxLayout>
#include <QSpinBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QComboBox>

#include <cmath>

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

static constexpr int kSteps = 100;      // slider steps from one state to the next

ControllerPad::ControllerPad(QWidget* parent) : QWidget(parent)
{
    setToolTip(tr("Drag the point among the states: the layers take poses blended by how near each state is. Shift-drag a state to move it."));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ControllerPad::setStates(QVector<QPointF> const& places, QStringList const& names)
{
    m_places = places;
    m_names = names;
    update();
}

void ControllerPad::setPoint(QPointF where)
{
    if (!m_dragging)
        m_where = where;
    update();
}

QRectF ControllerPad::square() const
{
    double const side = std::max(10.0, std::min(width(), height()) - 16.0);
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

QPointF ControllerPad::toWidget(QPointF place) const
{
    QRectF const r = square();
    return r.topLeft() + QPointF(place.x() * r.width(), place.y() * r.height());
}

QPointF ControllerPad::toPlace(QPointF widgetPos) const
{
    QRectF const r = square();
    return QPointF(std::clamp((widgetPos.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((widgetPos.y() - r.top()) / r.height(), 0.0, 1.0));
}

void ControllerPad::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF const r = square();
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(palette().color(QPalette::Base));
    p.drawRect(r);
    p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));
    p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    for (int i = 0; i < m_places.size(); ++i) {
        QPointF const at = toWidget(m_places[i]);
        p.setPen(palette().color(QPalette::Text));
        p.setBrush(palette().color(QPalette::Button));
        p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8)));
        if (i < m_names.size())
            p.drawText(at + QPointF(6, -4), m_names[i]);
    }
    QPointF const knob = toWidget(m_where);
    p.setPen(QPen(palette().color(QPalette::HighlightedText), 1.5));
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawEllipse(knob, 6, 6);
}

void ControllerPad::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
        return;
    m_dragState = -1;
    if (event->modifiers() & Qt::ShiftModifier) {
        for (int i = int(m_places.size()) - 1; i >= 0; --i)
            if (QLineF(toWidget(m_places[i]), event->position()).length() <= 7.0) {
                m_dragState = i;
                break;
            }
        if (m_dragState < 0)
            return;
    } else {
        m_where = toPlace(event->position());
    }
    m_dragging = true;
    update();
}

void ControllerPad::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    if (m_dragState >= 0 && m_dragState < m_places.size())
        m_places[m_dragState] = toPlace(event->position());
    else
        m_where = toPlace(event->position());
    update();
}

void ControllerPad::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    if (m_dragState >= 0) {
        int const state = std::exchange(m_dragState, -1);
        if (state < m_places.size())
            emit stateMoved(state, toPlace(event->position()));
        return;
    }
    m_where = toPlace(event->position());
    emit pointSet(m_where);
}

ControllerView::ControllerView(Document* document, Timeline* timeline, QWidget* parent)
    : QWidget(parent), m_document(document), m_timeline(timeline)
{
    m_controllers = new QListWidget(this);
    m_controllers->setObjectName(QStringLiteral("ControllerList"));
    m_controllers->setToolTip(tr("The controllers of the scene. Double click one to rename it."));
    m_states = new QListWidget(this);
    m_states->setObjectName(QStringLiteral("ControllerStates"));
    m_states->setToolTip(tr("The poses the controller keeps. Double click one to set the controller to it on this frame."));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName(QStringLiteral("ControllerSlider"));
    m_slider->setToolTip(tr("Between the states: the layers take poses blended between the two either side, on the current frame."));
    m_value = new QLabel(this);
    m_value->setObjectName(QStringLiteral("ControllerValue"));
    m_value->setMinimumWidth(110);
    auto button = [this](char const* name, QString const& text, QString const& tip) {
        QPushButton* const b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setToolTip(tip);
        return b;
    };
    QPushButton* const add = button("ControllerNew", tr("New"), tr("A controller for the layers selected in the Timeline (or the current layer)."));
    QPushButton* const remove = button("ControllerDelete", tr("Delete"), tr("Deletes the controller (the keyframes it set stay)."));
    QPushButton* const addState = button("ControllerAddState", tr("Add State"), tr("Keeps the poses and drawings the layers have on this frame, under a name."));
    QPushButton* const replace = button("ControllerReplaceState", tr("Replace"), tr("The state chosen takes the poses the layers have on this frame."));
    QPushButton* const drop = button("ControllerRemoveState", tr("Remove"), tr("Removes the state chosen."));
    QPushButton* const train = button("ControllerTrainNearest", tr("Train Nearest"), tr("The state nearest where the controller stands takes the poses the layers have on this frame."));
    QPushButton* const duplicate = button("ControllerDuplicateState", tr("Duplicate"), tr("Duplicate Position(s): a copy of the state chosen, after it."));
    QPushButton* const rename = button("ControllerRenameState", tr("Rename"), tr("Rename Pose: the state chosen gets another name."));
    QPushButton* const autoCreate = button("ControllerAutoCreate", tr("Auto-Create"), tr("Auto-Create Positions: a state for each frame its layers have a keyframe on."));
    QPushButton* const reset = button("ControllerReset", tr("Default"), tr("Default Value: the controller back where it rests, on this frame."));
    QPushButton* const jump = button("ControllerJump", tr("Jump to Controller"), tr("Chooses the controller the current layer is part of."));
    QPushButton* const options = button("ControllerOptions", tr("Settings..."), tr("Blend Type, Interpolation Type, Grid 2D, Slider Timeline, Default Value and how it shows in the Camera view."));

    QPushButton* const addMaster = button("ControllerNewMaster", tr("New Master"),
                                          tr("New Master Controller: its script makes sliders, points, lines and check boxes in the Camera view that set the attributes it names."));
    QPushButton* const script = button("ControllerScript", tr("Script..."), tr("The Master Controller's script, and whether its onLoad() runs when the scene is opened."));
    QHBoxLayout* const top = new QHBoxLayout;
    top->addWidget(add);
    top->addWidget(addMaster);
    top->addWidget(script);
    top->addWidget(remove);
    top->addStretch(1);
    QHBoxLayout* const stateButtons = new QHBoxLayout;
    stateButtons->addWidget(addState);
    stateButtons->addWidget(replace);
    stateButtons->addWidget(drop);
    stateButtons->addWidget(duplicate);
    stateButtons->addWidget(rename);
    stateButtons->addStretch(1);
    QHBoxLayout* const moreButtons = new QHBoxLayout;
    moreButtons->addWidget(train);
    moreButtons->addWidget(autoCreate);
    moreButtons->addWidget(reset);
    moreButtons->addWidget(jump);
    moreButtons->addWidget(options);
    moreButtons->addStretch(1);
    m_inCamera = new QCheckBox(tr("Show in Camera"), this);
    m_inCamera->setObjectName(QStringLiteral("ControllerInCamera"));
    m_inCamera->setToolTip(tr("The controller's slider in the lower left corner of the Camera view as well."));
    connect(m_inCamera, &QCheckBox::toggled, this, &ControllerView::setShownInCamera);
    m_point = new QCheckBox(tr("2D Point"), this);
    m_point->setObjectName(QStringLiteral("ControllerPoint"));
    m_point->setToolTip(tr("A point dragged on a pad among the states, instead of a slider from one state to the next."));
    connect(m_point, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing && currentController() >= 0)
            m_document->setControllerPoint(currentController(), on);
    });
    m_pad = new ControllerPad(this);
    m_pad->setObjectName(QStringLiteral("ControllerPad"));
    connect(m_pad, &ControllerPad::pointSet, this, &ControllerView::setPoint);
    connect(m_pad, &ControllerPad::stateMoved, this, [this](int state, QPointF at) {
        if (currentController() >= 0)
            m_document->moveControllerState(currentController(), state, at);
    });
    QHBoxLayout* const sliderRow = new QHBoxLayout;
    sliderRow->addWidget(m_slider, 1);
    sliderRow->addWidget(m_value);
    sliderRow->addWidget(m_point);
    sliderRow->addWidget(m_inCamera);
    QHBoxLayout* const lists = new QHBoxLayout;
    lists->addWidget(m_controllers, 1);
    lists->addWidget(m_states, 1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(top);
    layout->addLayout(lists, 1);
    layout->addLayout(stateButtons);
    layout->addLayout(moreButtons);
    layout->addWidget(m_pad, 1);
    layout->addLayout(sliderRow);
    m_master = new MasterControls(document, timeline, this);
    m_master->setObjectName(QStringLiteral("ControllerMasterControls"));
    layout->addWidget(m_master);
    m_stateParts = {m_states, addState, replace, drop, train, duplicate, rename, autoCreate, reset, m_point};
    connect(addMaster, &QPushButton::clicked, this, &ControllerView::newMasterController);
    connect(script, &QPushButton::clicked, this, &ControllerView::editScript);
    // On Load: when a scene is opened, each Master Controller that asks has its onLoad() run.
    connect(m_document, &Document::resetDone, this, [this] {
        for (int c = 0; c < m_document->scene().controllers.size(); ++c)
            if (m_document->scene().controllers[c].master && m_document->scene().controllers[c].onLoad)
                MasterControls::runOnLoad(m_document, m_timeline, c);
    });

    connect(add, &QPushButton::clicked, this, &ControllerView::newController);
    connect(remove, &QPushButton::clicked, this, &ControllerView::deleteController);
    connect(addState, &QPushButton::clicked, this, &ControllerView::addState);
    connect(replace, &QPushButton::clicked, this, &ControllerView::replaceState);
    connect(drop, &QPushButton::clicked, this, &ControllerView::removeState);
    connect(train, &QPushButton::clicked, this, [this] {
        if (currentController() >= 0)
            m_document->trainNearestState(currentController(), m_timeline->currentFrame());
    });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        if (currentController() >= 0 && m_states->currentRow() >= 0)
            m_document->duplicateControllerState(currentController(), m_states->currentRow());
    });
    connect(rename, &QPushButton::clicked, this, [this] {
        int const c = currentController(), state = m_states->currentRow();
        if (c < 0 || state < 0)
            return;
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Pose"), tr("Pose Name :"), QLineEdit::Normal, m_document->scene().controllers[c].states[state].name, &ok);
        if (ok)
            m_document->renameControllerState(c, state, name);
    });
    connect(autoCreate, &QPushButton::clicked, this, [this] {
        if (currentController() >= 0)
            m_document->autoCreatePositions(currentController());
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (currentController() >= 0)
            m_document->resetController(currentController(), m_timeline->currentFrame());
    });
    connect(options, &QPushButton::clicked, this, &ControllerView::editOptions);
    connect(jump, &QPushButton::clicked, this, &ControllerView::jumpToController);
    connect(m_controllers, &QListWidget::currentRowChanged, this, [this] {
        if (!m_refreshing)
            refresh();
    });
    connect(m_controllers, &QListWidget::itemDoubleClicked, this, [this] {
        int const c = currentController();
        if (c < 0)
            return;
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Controller"), tr("Name:"), QLineEdit::Normal,
                                                   m_document->scene().controllers[c].name, &ok);
        if (ok)
            m_document->renameController(c, name);
    });
    connect(m_states, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { setValue(m_states->row(item)); });
    // The slider sets the controller when it is let go (or moved with the keys): one step to undo each time.
    connect(m_slider, &QSlider::sliderReleased, this, [this] { setValue(m_slider->value() / double(kSteps)); });
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_value->setText(tr("%1").arg(v / double(kSteps), 0, 'f', 2));
        if (!m_refreshing && !m_slider->isSliderDown())
            setValue(v / double(kSteps));
    });
    connect(m_document, &Document::changed, this, &ControllerView::refresh);
    // (Another frame: the slider shows where the controller stands there.)
    connect(m_timeline, &Timeline::currentCellChanged, this, [this] {
        if (!m_refreshing && !m_slider->isSliderDown())
            refresh();
    });
    refresh();
}

int ControllerView::currentController() const
{
    int const row = m_controllers->currentRow();
    return row >= 0 && row < m_document->scene().controllers.size() ? row : -1;
}

void ControllerView::refresh()
{
    Scene const& scene = m_document->scene();
    m_refreshing = true;
    int const keep = std::max(0, m_controllers->currentRow());
    m_controllers->clear();
    for (Scene::Controller const& c : scene.controllers)
        m_controllers->addItem(tr("%1  (%n layer(s))", nullptr, int(c.layers.size())).arg(c.name));
    m_controllers->setCurrentRow(std::min(keep, m_controllers->count() - 1));
    int const c = currentController();
    int const keepState = m_states->currentRow();
    m_states->clear();
    if (c >= 0)
        for (Scene::Controller::State const& s : scene.controllers[c].states)
            m_states->addItem(s.name);
    m_states->setCurrentRow(std::min(std::max(0, keepState), m_states->count() - 1));
    bool const timeline = c >= 0 && scene.controllers[c].timeline;
    int const states = c >= 0 ? (timeline ? scene.controllers[c].timelineLength : int(scene.controllers[c].states.size())) : 0;
    m_slider->setEnabled(states >= 2);
    m_slider->setRange(0, std::max(0, states - 1) * kSteps);
    // (Where it stands on this frame: its keyframes say.)
    int const frame = m_timeline->currentFrame();
    double const value = c >= 0 ? scene.controllers[c].valueAt(frame) : 0.0;
    m_slider->setValue(int(std::lround(value * kSteps)));
    m_value->setText(c >= 0 ? tr("%1").arg(value, 0, 'f', 2) : QString());
    bool const point = c >= 0 && scene.controllers[c].point;
    m_point->setEnabled(c >= 0);
    m_point->setChecked(point);
    QVector<QPointF> places;
    QStringList names;
    if (c >= 0)
        for (Scene::Controller::State const& s : scene.controllers[c].states) {
            places.push_back(s.at);
            names.push_back(s.name);
        }
    QPointF const where = c >= 0 ? scene.controllers[c].whereAt(frame) : QPointF(0.5, 0.5);
    for (ControllerPad* pad : {m_pad, m_overlayPad})
        if (pad) {
            pad->setStates(places, names);
            pad->setPoint(where);
            pad->setEnabled(states >= 1);
        }
    bool const master = c >= 0 && scene.controllers[c].master;
    m_pad->setVisible(point && !master);
    m_slider->setVisible(!point && !master);
    m_value->setVisible(!point && !master);
    for (QWidget* w : std::as_const(m_stateParts))
        w->setVisible(!master);
    m_master->setVisible(master);
    m_master->setController(master ? c : -1);
    if (m_overlay) {
        m_overlaySlider->setRange(m_slider->minimum(), m_slider->maximum());
        m_overlaySlider->setValue(m_slider->value());
        m_overlaySlider->setVisible(!point && !master);
        m_overlayPad->setVisible(point && !master);
        m_overlayMaster->setVisible(master);
        m_overlayMaster->setController(master ? c : -1);
        m_overlayName->setText(c >= 0 ? scene.controllers[c].name : QString());
        bool shownHere = true;
        if (c >= 0) {
            Scene::Controller const& one = scene.controllers[c];
            // Text Colour, Text Background Colour, Text Size; Background Box; the pad's size (Constant Size: as set).
            m_overlayName->setStyleSheet(QStringLiteral("color: %1; background-color: %2; font-size: %3pt; padding: 1px 3px;")
                                             .arg(one.textColour.name(), one.textBackground.name(QColor::HexArgb))
                                             .arg(one.textSize));
            m_overlay->setAutoFillBackground(one.backgroundBox);
            m_overlay->setFrameShape(one.backgroundBox ? QFrame::StyledPanel : QFrame::NoFrame);
            m_overlayPad->setFixedSize(one.padSize, one.padSize);
            // Text Position: beside the control or above it.
            if (QBoxLayout* const row = qobject_cast<QBoxLayout*>(m_overlay->layout()))
                row->setDirection(one.textAbove ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
            // Visibility Context: Based on Selection - shown only while one of its layers (or the Selection Override layer) is
            // the current one.
            if (one.visibility == Scene::Controller::BasedOnSelection) {
                int const current = m_timeline->currentLayer();
                int const id = current >= 0 && current < scene.layers.size() ? scene.layers[current].id : 0;
                shownHere = one.selectionOverride != 0 ? id == one.selectionOverride : one.layers.contains(id);
            }
            // Show Controls Mode: Never.
            if (one.visibility == Scene::Controller::Never)
                shownHere = false;
        }
        m_overlay->setVisible(m_inCamera->isChecked() && shownHere && (master ? m_overlayMaster->count() > 0 : states >= (point ? 1 : 2)));
        placeOverlay();
    }
    m_refreshing = false;
}

void ControllerView::setValue(double value)
{
    int const c = currentController();
    if (c >= 0)
        m_document->applyController(c, m_timeline->currentFrame(), value);
}

void ControllerView::setPoint(QPointF where)
{
    int const c = currentController();
    if (c >= 0)
        m_document->applyControllerPoint(c, m_timeline->currentFrame(), where);
}

void ControllerView::newController()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty() && m_timeline->currentLayer() >= 0)
        layers = {m_timeline->currentLayer()};
    int const at = m_document->addController(QString(), layers);
    if (at >= 0) {
        refresh();
        m_controllers->setCurrentRow(at);
    }
}

void ControllerView::deleteController()
{
    if (int const c = currentController(); c >= 0)
        m_document->removeController(c);
}

void ControllerView::addState()
{
    int const c = currentController();
    if (c < 0)
        return;
    int const at = m_document->addControllerState(c, m_timeline->currentFrame(), QString());
    refresh();
    m_states->setCurrentRow(at);
}

void ControllerView::replaceState()
{
    int const c = currentController();
    if (c >= 0 && m_states->currentRow() >= 0)
        m_document->replaceControllerState(c, m_states->currentRow(), m_timeline->currentFrame());
}

void ControllerView::removeState()
{
    int const c = currentController();
    if (c >= 0 && m_states->currentRow() >= 0)
        m_document->removeControllerState(c, m_states->currentRow());
}

void ControllerView::setCameraHost(QWidget* camera)
{
    m_camera = camera;
    m_overlay = new QFrame(camera);
    m_overlay->setObjectName(QStringLiteral("CameraControllerOverlay"));
    m_overlay->setFrameShape(QFrame::StyledPanel);
    m_overlay->setAutoFillBackground(true);
    m_overlayName = new QLabel(m_overlay);
    m_overlaySlider = new QSlider(Qt::Horizontal, m_overlay);
    m_overlaySlider->setObjectName(QStringLiteral("CameraControllerSlider"));
    m_overlaySlider->setMinimumWidth(160);
    m_overlayPad = new ControllerPad(m_overlay);
    m_overlayPad->setObjectName(QStringLiteral("CameraControllerPad"));
    m_overlayPad->setFixedSize(130, 130);
    m_overlayPad->hide();
    connect(m_overlayPad, &ControllerPad::pointSet, this, &ControllerView::setPoint);
    connect(m_overlayPad, &ControllerPad::stateMoved, this, [this](int state, QPointF at) {
        if (currentController() >= 0)
            m_document->moveControllerState(currentController(), state, at);
    });
    QHBoxLayout* const row = new QHBoxLayout(m_overlay);
    row->setContentsMargins(6, 3, 6, 3);
    row->addWidget(m_overlayName);
    row->addWidget(m_overlaySlider, 1);
    row->addWidget(m_overlayPad);
    m_overlayMaster = new MasterControls(m_document, m_timeline, m_overlay);
    m_overlayMaster->setObjectName(QStringLiteral("CameraMasterControls"));
    m_overlayMaster->setMinimumWidth(200);
    m_overlayMaster->hide();
    row->addWidget(m_overlayMaster, 1);
    // As the slider in the view: set when it is let go, or moved with the keys.
    connect(m_overlaySlider, &QSlider::sliderReleased, this, [this] { setValue(m_overlaySlider->value() / double(kSteps)); });
    connect(m_overlaySlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_refreshing && !m_overlaySlider->isSliderDown())
            setValue(v / double(kSteps));
    });
    m_overlay->hide();
    camera->installEventFilter(this);
    refresh();
}

void ControllerView::setShownInCamera(bool on)
{
    if (m_inCamera->isChecked() != on)
        m_inCamera->setChecked(on);     // (comes back here)
    else
        refresh();
}

void ControllerView::placeOverlay()
{
    if (!m_overlay || !m_camera)
        return;
    m_overlay->adjustSize();
    m_overlay->move(10, m_camera->height() - m_overlay->height() - 10);
    m_overlay->raise();
}

bool ControllerView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_camera && event->type() == QEvent::Resize)
        placeOverlay();
    return QWidget::eventFilter(watched, event);
}

void ControllerView::editOptions()
{
    int const c = currentController();
    if (c < 0)
        return;
    Scene const& scene = m_document->scene();
    Scene::Controller options = scene.controllers[c];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ControllerOptionsDialog"));
    dialog.setWindowTitle(tr("Controller Settings: %1").arg(options.name));
    QFormLayout* const form = new QFormLayout(&dialog);
    QComboBox* const blend = new QComboBox(&dialog);
    blend->setObjectName(QStringLiteral("ControllerBlend"));
    blend->addItems({tr("Normalize Weights"), tr("Highest Weight")});
    blend->setCurrentIndex(options.blend);
    QComboBox* const interpolation = new QComboBox(&dialog);
    interpolation->setObjectName(QStringLiteral("ControllerInterpolation"));
    interpolation->addItems({tr("Linear"), tr("Smooth")});
    interpolation->setCurrentIndex(options.interpolation);
    QCheckBox* const grid = new QCheckBox(tr("Grid 2D"), &dialog);
    grid->setObjectName(QStringLiteral("ControllerGrid"));
    grid->setChecked(options.grid);
    QSpinBox* const gridX = new QSpinBox(&dialog);
    gridX->setObjectName(QStringLiteral("ControllerGridX"));
    gridX->setRange(2, 20);
    gridX->setValue(options.gridX);
    QSpinBox* const gridY = new QSpinBox(&dialog);
    gridY->setObjectName(QStringLiteral("ControllerGridY"));
    gridY->setRange(2, 20);
    gridY->setValue(options.gridY);
    QCheckBox* const timeline = new QCheckBox(tr("Slider Timeline"), &dialog);
    timeline->setObjectName(QStringLiteral("ControllerTimeline"));
    timeline->setChecked(options.timeline);
    QSpinBox* const start = new QSpinBox(&dialog);
    start->setObjectName(QStringLiteral("ControllerTimelineStart"));
    start->setRange(1, 100000);
    start->setValue(options.timelineStart + 1);
    QSpinBox* const length = new QSpinBox(&dialog);
    length->setObjectName(QStringLiteral("ControllerTimelineLength"));
    length->setRange(1, 100000);
    length->setValue(options.timelineLength);
    QDoubleSpinBox* const defaultValue = new QDoubleSpinBox(&dialog);
    defaultValue->setObjectName(QStringLiteral("ControllerDefault"));
    defaultValue->setRange(0, 100000);
    defaultValue->setDecimals(2);
    defaultValue->setValue(options.defaultValue);
    QSpinBox* const textSize = new QSpinBox(&dialog);
    textSize->setObjectName(QStringLiteral("ControllerTextSize"));
    textSize->setRange(5, 72);
    textSize->setValue(options.textSize);
    // Text Colour and Text Background Colour: buttons showing the colour, a click to choose another.
    auto colourButton = [&](char const* name, QColor const& now) {
        QPushButton* const b = new QPushButton(&dialog);
        b->setObjectName(QLatin1String(name));
        auto show = [b](QColor const& c) {
            b->setProperty("colour", c);
            b->setStyleSheet(QStringLiteral("background-color: %1; min-width: 60px;").arg(c.name(QColor::HexArgb)));
        };
        show(now);
        connect(b, &QPushButton::clicked, &dialog, [b, show, &dialog] {
            QColor const chosen = QColorDialog::getColor(b->property("colour").value<QColor>(), &dialog, QString(), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid())
                show(chosen);
        });
        return b;
    };
    QPushButton* const textColour = colourButton("ControllerTextColour", options.textColour);
    QPushButton* const textBack = colourButton("ControllerTextBackground", options.textBackground);
    QCheckBox* const above = new QCheckBox(tr("Text Position: above the control"), &dialog);
    above->setChecked(options.textAbove);
    QCheckBox* const box = new QCheckBox(tr("Background Box"), &dialog);
    box->setChecked(options.backgroundBox);
    QSpinBox* const padSize = new QSpinBox(&dialog);
    padSize->setObjectName(QStringLiteral("ControllerPadSize"));
    padSize->setRange(40, 600);
    padSize->setValue(options.padSize);
    QComboBox* const visibility = new QComboBox(&dialog);
    visibility->setObjectName(QStringLiteral("ControllerVisibility"));
    visibility->addItems({tr("Always"), tr("Based on Selection"), tr("Never")});
    visibility->setToolTip(tr("Show Controls Mode: when the controller shows in the Camera view."));
    // Base Pose: the state the controller rests on (where Default puts it).
    QComboBox* const basePose = new QComboBox(&dialog);
    basePose->setObjectName(QStringLiteral("ControllerBasePose"));
    basePose->addItem(tr("(Default Value)"), -1);
    for (int s = 0; s < options.states.size(); ++s)
        basePose->addItem(options.states[s].name, s);
    for (int s = 0; s < options.states.size(); ++s)
        if (options.point ? options.states[s].at == options.defaultWhere : std::abs(options.defaultValue - s) < 1e-9)
            basePose->setCurrentIndex(s + 1);
    visibility->setCurrentIndex(options.visibility);
    QComboBox* const override = new QComboBox(&dialog);
    override->setObjectName(QStringLiteral("ControllerSelectionOverride"));
    override->addItem(tr("Its own layers"), 0);
    for (Layer const& l : scene.layers)
        override->addItem(l.name, l.id);
    override->setCurrentIndex(std::max(0, override->findData(options.selectionOverride)));
    form->addRow(tr("Blend Type"), blend);
    form->addRow(tr("Interpolation Type"), interpolation);
    form->addRow(grid);
    form->addRow(tr("Grid Count X"), gridX);
    form->addRow(tr("Grid Count Y"), gridY);
    form->addRow(timeline);
    form->addRow(tr("Frame 1 / Custom Frame"), start);
    form->addRow(tr("Timeline Length"), length);
    form->addRow(tr("Default Value"), defaultValue);
    form->addRow(tr("Base Pose:"), basePose);
    form->addRow(tr("Text Size"), textSize);
    form->addRow(tr("Text Colour"), textColour);
    form->addRow(tr("Text Background Colour"), textBack);
    form->addRow(above);
    form->addRow(box);
    form->addRow(tr("Pad size"), padSize);
    form->addRow(tr("Show Controls Mode"), visibility);
    form->addRow(tr("Selection Override"), override);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    options.blend = blend->currentIndex();
    options.interpolation = interpolation->currentIndex();
    options.grid = grid->isChecked();
    options.gridX = gridX->value();
    options.gridY = gridY->value();
    options.timeline = timeline->isChecked();
    options.timelineStart = start->value() - 1;
    options.timelineLength = length->value();
    options.defaultValue = defaultValue->value();
    if (int const base = basePose->currentData().toInt(); base >= 0 && base < options.states.size()) {
        if (options.point)
            options.defaultWhere = options.states[base].at;
        else
            options.defaultValue = base;
    }
    options.textSize = textSize->value();
    options.textColour = textColour->property("colour").value<QColor>();
    options.textBackground = textBack->property("colour").value<QColor>();
    options.textAbove = above->isChecked();
    options.backgroundBox = box->isChecked();
    options.padSize = padSize->value();
    options.visibility = visibility->currentIndex();
    options.selectionOverride = override->currentData().toInt();
    m_document->setControllerOptions(c, options, tr("Controller Settings"));
}

void ControllerView::newMasterController()
{
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty() && m_timeline->currentLayer() >= 0)
        layers = {m_timeline->currentLayer()};
    Scene const& scene = m_document->scene();
    // A script to start from: a turn and a place for the first layer chosen.
    QString script = tr("// A Master Controller: the controls this script makes set the attributes they name.\n"
                        "// controls.slider / point / line / check / label - see Help > Scripting Interface Documentation.\n");
    if (!layers.isEmpty() && layers.first() >= 0 && layers.first() < scene.layers.size()) {
        QString const name = scene.layers[layers.first()].name;
        script += QStringLiteral("controls.slider({attribute: \"%1:rotation.angle\", min: -180, max: 180, label: \"%2\"});\n"
                                 "controls.point({x: \"%1:position.x\", y: \"%1:position.y\", min: -300, max: 300, label: \"%3\"});\n")
                      .arg(name, tr("Angle"), tr("Position"));
    }
    int const at = m_document->addMasterController(QString(), layers, script);
    if (at >= 0) {
        refresh();
        m_controllers->setCurrentRow(at);
    }
}

void ControllerView::editScript()
{
    int const c = currentController();
    if (c < 0)
        return;
    Scene::Controller options = m_document->scene().controllers[c];
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("MasterControllerScriptDialog"));
    dialog.setWindowTitle(tr("Master Controller Script: %1").arg(options.name));
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    QPlainTextEdit* const text = new QPlainTextEdit(options.script, &dialog);
    text->setObjectName(QStringLiteral("MasterControllerScript"));
    text->setMinimumSize(460, 260);
    QCheckBox* const onLoad = new QCheckBox(tr("On Load: run its onLoad() when the scene is opened"), &dialog);
    onLoad->setObjectName(QStringLiteral("MasterControllerOnLoad"));
    onLoad->setChecked(options.onLoad);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(text, 1);
    layout->addWidget(onLoad);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_document->beginMacro(tr("Master Controller Script"));
    m_document->setControllerScript(c, text->toPlainText());
    if (options.onLoad != onLoad->isChecked()) {
        options = m_document->scene().controllers[c];
        options.onLoad = onLoad->isChecked();
        m_document->setControllerOptions(c, options, tr("Master Controller - On Load"));
    }
    m_document->endMacro();
}

bool ControllerView::jumpToController()
{
    Scene const& scene = m_document->scene();
    int const current = m_timeline->currentLayer();
    int const id = current >= 0 && current < scene.layers.size() ? scene.layers[current].id : 0;
    for (int c = 0; c < scene.controllers.size(); ++c)
        if (scene.controllers[c].layers.contains(id)) {
            m_controllers->setCurrentRow(c);
            return true;
        }
    return false;
}
