// Eclipse - the main window: Easy Flipping - the drawings either side of the current one flipped through, as paper is flipped.
#include "ui/window/MainWindow.h"

#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include "actions/manager/ActionManager.h"
#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

void MainWindow::createFlipToolbar()
{
    QToolBar* const bar = new QToolBar(tr("Easy Flipping"), this);
    bar->setObjectName(QStringLiteral("EasyFlippingToolbar"));
    if (QAction* const flip = m_actions->action(QStringLiteral("EasyFlip")))
        bar->addAction(flip);
    auto spin = [&](char const* name, QString const& tip, int low, int high, int usual, QString const& suffix) {
        QSpinBox* const box = new QSpinBox(bar);
        box->setObjectName(QLatin1String(name));
        box->setRange(low, high);
        box->setValue(std::clamp(settings().value(QStringLiteral("flip/") + QLatin1String(name), usual).toInt(), low, high));
        box->setSuffix(suffix);
        box->setToolTip(tip);
        connect(box, &QSpinBox::valueChanged, this, [name](int v) { settings().setValue(QStringLiteral("flip/") + QLatin1String(name), v); });
        bar->addWidget(box);
        return box;
    };
    m_flipSpeed = spin("FlipSpeed", tr("Set flipping speed - drawings per second"), 1, 60, 8, tr(" /s"));
    m_flipBefore = spin("FlipBefore", tr("Set number of drawings to flip before the current drawing."), 0, 20, 3, QString());
    m_flipAfter = spin("FlipAfter", tr("Set number of drawings to flip after the current drawing."), 0, 20, 3, QString());
    connect(m_flipSpeed, &QSpinBox::valueChanged, this, [this](int perSecond) { m_flipTimer->setInterval(1000 / std::max(1, perSecond)); });
    auto mark = [&](char const* name, QString const& text, QString const& tip) {
        QCheckBox* const box = new QCheckBox(text, bar);
        box->setObjectName(QLatin1String(name));
        box->setToolTip(tip);
        box->setChecked(settings().value(QStringLiteral("flip/") + QLatin1String(name), false).toBool());
        connect(box, &QCheckBox::toggled, this, [name](bool on) { settings().setValue(QStringLiteral("flip/") + QLatin1String(name), on); });
        bar->addWidget(box);
        return box;
    };
    m_flipKeys = mark("FlipKey", tr("K"), tr("Show Key drawings when flipping"));
    m_flipBreakdowns = mark("FlipBreakdown", tr("B"), tr("Show Breakdown drawings when flipping"));
    m_flipInbetweens = mark("FlipInbetween", tr("I"), tr("Show In-between drawings when flipping"));
    m_flipTimer = new QTimer(this);
    m_flipTimer->setInterval(1000 / m_flipSpeed->value());
    connect(m_flipTimer, &QTimer::timeout, this, [this] {
        // To the end, back to the start, and round again.
        if (m_flipFrames.size() < 2)
            return;
        m_flipAt += m_flipWay;
        if (m_flipAt <= 0 || m_flipAt >= m_flipFrames.size() - 1)
            m_flipWay = -m_flipWay;
        m_flipAt = std::clamp(m_flipAt, 0, int(m_flipFrames.size()) - 1);
        m_timeline->setCurrentCell(m_flipLayer, m_flipFrames[m_flipAt]);
    });
    addToolBar(Qt::TopToolBarArea, bar);
    bar->hide();
    m_toolbars << bar;
}

QVector<int> MainWindow::flipFrames() const
{
    // The first frame of each drawing either side of the current one, so many before and after, of the marks ticked (none: all).
    Scene const& scene = m_document->scene();
    QVector<int> frames;
    if (layer() < 0 || layer() >= scene.layers.size() || !scene.layers[layer()].holdsDrawings())
        return frames;
    Layer const& l = scene.layers[layer()];
    bool const any = !m_flipKeys->isChecked() && !m_flipBreakdowns->isChecked() && !m_flipInbetweens->isChecked();
    auto wanted = [&](int f) {
        int const mark = l.drawings[l.cells[f]].mark;
        return any || (mark == Drawing::KeyMark && m_flipKeys->isChecked()) || (mark == Drawing::BreakdownMark && m_flipBreakdowns->isChecked())
               || (mark == Drawing::InBetweenMark && m_flipInbetweens->isChecked());
    };
    QVector<int> before;
    for (int f = l.previousDrawingFrame(frame()); f >= 0 && before.size() < m_flipBefore->value(); f = l.previousDrawingFrame(f))
        if (l.drawingAt(f) != Layer::kNoDrawing && wanted(f))
            before.prepend(f);
    frames = before;
    int here = frame();
    while (here > 0 && l.drawingAt(here) != Layer::kNoDrawing && l.drawingAt(here - 1) == l.drawingAt(here))
        --here;
    frames << here;
    int after = 0;
    for (int f = l.nextDrawingFrame(frame()); f >= 0 && f < scene.frameCount() && after < m_flipAfter->value(); f = l.nextDrawingFrame(f))
        if (l.drawingAt(f) != Layer::kNoDrawing && wanted(f)) {
            frames << f;
            ++after;
        }
    return frames;
}

void MainWindow::onActionEasyFlip(bool on)
{
    if (on) {
        m_flipFrames = flipFrames();
        if (m_flipFrames.size() < 2) {
            statusBar()->showMessage(tr("No drawings either side of this one to flip through."), 4000);
            m_actions->setChecked(QStringLiteral("EasyFlip"), false);
            return;
        }
        m_flipLayer = layer();
        m_flipHome = frame();
        m_flipAt = int(m_flipFrames.indexOf(m_flipFrames.contains(frame()) ? frame() : m_flipFrames.first()));
        m_flipAt = std::max(0, m_flipAt);
        m_flipWay = -1;
        m_flipTimer->start();
    } else if (m_flipTimer->isActive()) {
        m_flipTimer->stop();
        m_timeline->setCurrentCell(m_flipLayer, m_flipHome);     // back to the drawing worked on
    }
    m_actions->setChecked(QStringLiteral("EasyFlip"), on);
}
