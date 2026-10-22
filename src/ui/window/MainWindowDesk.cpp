// Eclipse - the main window: the Desk - drawings kept at hand to trace from with Shift and Trace.
#include "ui/window/MainWindow.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

namespace {

// The drawing alone, on white, as large as it fits.
QIcon deskPicture(Scene const& scene, Drawing const& drawing, QSize size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QRectF const bounds = drawing.bounds();
    if (!bounds.isEmpty()) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        double const scale = std::min((size.width() - 6) / bounds.width(), (size.height() - 6) / bounds.height());
        painter.translate(size.width() / 2.0, size.height() / 2.0);
        painter.scale(scale, scale);
        painter.translate(-bounds.center());
        Canvas::paintDrawing(painter, scene, drawing, 1.0);
    }
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}

QSize deskSize(int which)
{
    return which == 1 ? QSize(48, 36) : which == 2 ? QSize(80, 60) : which == 3 ? QSize(128, 96) : QSize();
}

}  // namespace

QWidget* MainWindow::createDeskView()
{
    QWidget* const view = new QWidget(this);
    view->setObjectName(QStringLiteral("DeskView"));
    m_desk = new QListWidget(view);
    m_desk->setObjectName(QStringLiteral("DeskList"));
    m_desk->setToolTip(tr("Drawings kept at hand to trace from: ticked, they show in the Camera view with Shift and Trace on. A click picks the one the Shift and Trace tool moves."));
    QPushButton* const up = new QPushButton(tr("Up"), view);
    QPushButton* const down = new QPushButton(tr("Down"), view);
    QPushButton* const reset = new QPushButton(tr("Reset"), view);
    reset->setToolTip(tr("Back where the drawing is (Shift and Trace Drawing Reset Position)."));
    QPushButton* const remove = new QPushButton(tr("Remove"), view);
    remove->setObjectName(QStringLiteral("DeskRemove"));
    QHBoxLayout* const row = new QHBoxLayout;
    for (QPushButton* const b : {up, down, reset, remove})
        row->addWidget(b);
    row->addStretch(1);
    QVBoxLayout* const layout = new QVBoxLayout(view);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_desk, 1);
    layout->addLayout(row);
    connect(up, &QPushButton::clicked, m_canvas, [this] { m_canvas->onActionShiftTraceMove(QStringLiteral("up")); });
    connect(down, &QPushButton::clicked, m_canvas, [this] { m_canvas->onActionShiftTraceMove(QStringLiteral("down")); });
    connect(reset, &QPushButton::clicked, m_canvas, &Canvas::onActionShiftTraceReset);
    connect(remove, &QPushButton::clicked, this, [this] {
        int const row = m_desk->currentRow();
        QVector<Scene::DeskItem> desk = m_document->scene().desk;
        if (row < 0 || row >= desk.size())
            return;
        desk.removeAt(row);
        m_document->setDesk(desk, tr("Remove from Desk"));
    });
    connect(m_desk, &QListWidget::currentRowChanged, this, [this](int row) {
        Scene const& scene = m_document->scene();
        if (row >= 0 && row < scene.desk.size())
            m_canvas->setTraceTarget(scene.layerIndex(scene.desk[row].layer), scene.desk[row].drawing);
    });
    connect(m_desk, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        int const row = m_desk->row(item);
        QVector<Scene::DeskItem> desk = m_document->scene().desk;
        if (row < 0 || row >= desk.size() || desk[row].shown == (item->checkState() == Qt::Checked))
            return;
        desk[row].shown = item->checkState() == Qt::Checked;
        m_document->setDesk(desk, tr("Shift and Trace Visibility"));
    });
    connect(m_document, &Document::changed, this, &MainWindow::syncDesk);
    m_deskThumbnails = std::clamp(settings().value(QStringLiteral("desk/thumbnails"), 2).toInt(), 0, 3);
    syncDesk();
    return view;
}

void MainWindow::syncDesk()
{
    if (!m_desk)
        return;
    Scene const& scene = m_document->scene();
    QSignalBlocker const quiet(m_desk);
    int const was = m_desk->currentRow();
    m_desk->clear();
    QSize const size = deskSize(m_deskThumbnails);
    m_desk->setIconSize(size);
    for (Scene::DeskItem const& item : scene.desk) {
        int const l = scene.layerIndex(item.layer);
        bool const there = l >= 0 && item.drawing >= 0 && item.drawing < scene.layers[l].drawings.size();
        QString const text = there ? QStringLiteral("%1 - %2").arg(scene.layers[l].name, scene.layers[l].drawingLabel(item.drawing)) : tr("(gone)");
        QListWidgetItem* const row = size.isValid() && there ? new QListWidgetItem(deskPicture(scene, scene.layers[l].drawings[item.drawing], size), text, m_desk)
                                                            : new QListWidgetItem(text, m_desk);
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(item.shown ? Qt::Checked : Qt::Unchecked);
    }
    if (m_desk->count() > 0)
        m_desk->setCurrentRow(std::clamp(was, 0, m_desk->count() - 1));
    for (int which = 0; which < 4; ++which)
        m_actions->setChecked(QStringLiteral("DeskThumbnails%1").arg(which), which == m_deskThumbnails);
}

void MainWindow::onActionDeskThumbnails(QString const& which)
{
    m_deskThumbnails = std::clamp(which.toInt(), 0, 3);
    settings().setValue(QStringLiteral("desk/thumbnails"), m_deskThumbnails);
    syncDesk();
}

void MainWindow::onActionDesk(QString const& how)
{
    // The drawings of the cells selected (of the current cell, when none are): onto the Desk in their place (send) or after those there (add).
    Scene const& scene = m_document->scene();
    QVector<int> layers = m_timeline->selectedLayers();
    if (layers.isEmpty() && layer() >= 0)
        layers << layer();
    int const first = m_timeline->hasRange() ? m_timeline->selectionFirstFrame() : frame();
    int const last = m_timeline->hasRange() ? m_timeline->selectionLastFrame() : frame();
    QVector<Scene::DeskItem> desk = how == QLatin1String("add") ? scene.desk : QVector<Scene::DeskItem>();
    int const before = int(desk.size());
    for (int l : std::as_const(layers)) {
        if (l < 0 || l >= scene.layers.size() || !scene.layers[l].holdsDrawings())
            continue;
        for (int f = first; f <= last; ++f) {
            int const drawing = scene.layers[l].drawingAt(f) == Layer::kNoDrawing ? -1 : scene.layers[l].cells[f];
            Scene::DeskItem const item{scene.layers[l].id, drawing, true};
            bool known = drawing < 0;
            for (Scene::DeskItem const& on : std::as_const(desk))
                known = known || (on.layer == item.layer && on.drawing == item.drawing);
            if (!known)
                desk.push_back(item);
        }
    }
    if (desk.size() == before && how == QLatin1String("add")) {
        statusBar()->showMessage(tr("No drawing there that is not on the Desk already."), 4000);
        return;
    }
    m_document->setDesk(desk, how == QLatin1String("add") ? tr("Add Drawings to Desk") : tr("Send Drawings to Desk"));
    m_canvas->onActionShiftTrace(true);
    m_actions->setChecked(QStringLiteral("ShiftTraceToggle"), true);
    showView(dockOf("DeskDock"));
}
