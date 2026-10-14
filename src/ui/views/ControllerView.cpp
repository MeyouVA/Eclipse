// Eclipse - Pose Controllers.
#include "ui/views/ControllerView.h"

#include <cmath>

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

static constexpr int kSteps = 100;      // slider steps from one state to the next

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

    QHBoxLayout* const top = new QHBoxLayout;
    top->addWidget(add);
    top->addWidget(remove);
    top->addStretch(1);
    QHBoxLayout* const stateButtons = new QHBoxLayout;
    stateButtons->addWidget(addState);
    stateButtons->addWidget(replace);
    stateButtons->addWidget(drop);
    stateButtons->addStretch(1);
    m_inCamera = new QCheckBox(tr("Show in Camera"), this);
    m_inCamera->setObjectName(QStringLiteral("ControllerInCamera"));
    m_inCamera->setToolTip(tr("The controller's slider in the lower left corner of the Camera view as well."));
    connect(m_inCamera, &QCheckBox::toggled, this, &ControllerView::setShownInCamera);
    QHBoxLayout* const sliderRow = new QHBoxLayout;
    sliderRow->addWidget(m_slider, 1);
    sliderRow->addWidget(m_value);
    sliderRow->addWidget(m_inCamera);
    QHBoxLayout* const lists = new QHBoxLayout;
    lists->addWidget(m_controllers, 1);
    lists->addWidget(m_states, 1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(top);
    layout->addLayout(lists, 1);
    layout->addLayout(stateButtons);
    layout->addLayout(sliderRow);

    connect(add, &QPushButton::clicked, this, &ControllerView::newController);
    connect(remove, &QPushButton::clicked, this, &ControllerView::deleteController);
    connect(addState, &QPushButton::clicked, this, &ControllerView::addState);
    connect(replace, &QPushButton::clicked, this, &ControllerView::replaceState);
    connect(drop, &QPushButton::clicked, this, &ControllerView::removeState);
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
    int const states = c >= 0 ? int(scene.controllers[c].states.size()) : 0;
    m_slider->setEnabled(states >= 2);
    m_slider->setRange(0, std::max(0, states - 1) * kSteps);
    m_slider->setValue(c >= 0 ? int(std::lround(scene.controllers[c].value * kSteps)) : 0);
    m_value->setText(c >= 0 ? tr("%1").arg(scene.controllers[c].value, 0, 'f', 2) : QString());
    if (m_overlay) {
        m_overlaySlider->setRange(m_slider->minimum(), m_slider->maximum());
        m_overlaySlider->setValue(m_slider->value());
        m_overlayName->setText(c >= 0 ? scene.controllers[c].name : QString());
        m_overlay->setVisible(m_inCamera->isChecked() && states >= 2);
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
    QHBoxLayout* const row = new QHBoxLayout(m_overlay);
    row->setContentsMargins(6, 3, 6, 3);
    row->addWidget(m_overlayName);
    row->addWidget(m_overlaySlider, 1);
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
