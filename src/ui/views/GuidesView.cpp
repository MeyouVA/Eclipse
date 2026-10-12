// Eclipse - the Guides view: straight lines across the picture to draw and snap to.
#include "ui/views/GuidesView.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"
#include "ui/canvas/Canvas.h"

GuidesView::GuidesView(Document* document, Canvas* canvas, QWidget* parent) : QWidget(parent), m_document(document), m_canvas(canvas)
{
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("GuidesList"));
    m_position = new QDoubleSpinBox(this);
    m_position->setObjectName(QStringLiteral("GuidePosition"));
    m_position->setRange(-100000, 100000);
    m_position->setDecimals(1);
    m_position->setKeyboardTracking(false);
    m_position->setToolTip(tr("Where the guide chosen lies: from the left edge of the frame for a guide that runs down, from the top for one that runs across."));
    m_show = new QCheckBox(tr("Show guides"), this);
    m_show->setObjectName(QStringLiteral("GuidesShow"));
    m_show->setChecked(m_canvas->showsGuides());
    m_show->setToolTip(tr("Guides are seen in the Camera view; the shape tools and the points of the Contour Editor go to them when they come near."));
    QPushButton* const across = new QPushButton(tr("Add Across"), this);
    across->setObjectName(QStringLiteral("GuideAddHorizontal"));
    QPushButton* const down = new QPushButton(tr("Add Down"), this);
    down->setObjectName(QStringLiteral("GuideAddVertical"));
    QPushButton* const remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("GuideRemove"));
    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(across);
    buttons->addWidget(down);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    QHBoxLayout* const place = new QHBoxLayout;
    place->addWidget(new QLabel(tr("Position"), this));
    place->addWidget(m_position, 1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_show);
    layout->addWidget(m_list, 1);
    layout->addLayout(place);
    layout->addLayout(buttons);
    connect(m_show, &QCheckBox::toggled, m_canvas, &Canvas::setShowGuides);
    // A new guide lies through the middle of the frame, to be put where it is wanted.
    connect(across, &QPushButton::clicked, this, [this] { addGuide(false, m_document->scene().height / 2.0); });
    connect(down, &QPushButton::clicked, this, [this] { addGuide(true, m_document->scene().width / 2.0); });
    connect(remove, &QPushButton::clicked, this, [this] {
        QVector<Scene::Guide> guides = m_document->scene().guides;
        int const row = m_list->currentRow();
        if (row < 0 || row >= guides.size())
            return;
        guides.removeAt(row);
        m_document->setGuides(guides, tr("Remove Guide"));
    });
    connect(m_position, &QDoubleSpinBox::valueChanged, this, [this](double at) {
        QVector<Scene::Guide> guides = m_document->scene().guides;
        int const row = m_list->currentRow();
        if (m_rebuilding || row < 0 || row >= guides.size() || guides[row].at == at)
            return;
        guides[row].at = at;
        m_document->setGuides(guides, tr("Move Guide"));
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        QVector<Scene::Guide> const& guides = m_document->scene().guides;
        QSignalBlocker const block(m_position);
        m_position->setEnabled(row >= 0 && row < guides.size());
        if (row >= 0 && row < guides.size())
            m_position->setValue(guides[row].at);
    });
    connect(m_document, &Document::changed, this, &GuidesView::rebuild);
    rebuild();
}

int GuidesView::addGuide(bool vertical, double at)
{
    QVector<Scene::Guide> guides = m_document->scene().guides;
    guides.push_back({vertical, at});
    m_document->setGuides(guides, tr("Add Guide"));
    m_list->setCurrentRow(int(guides.size()) - 1);
    return int(guides.size()) - 1;
}

void GuidesView::rebuild()
{
    QVector<Scene::Guide> const& guides = m_document->scene().guides;
    int const row = m_list->currentRow();
    m_rebuilding = true;
    {
        QSignalBlocker const block(m_list);
        m_list->clear();
        for (Scene::Guide const& guide : guides)
            m_list->addItem(guide.vertical ? tr("Down, %1 from the left").arg(guide.at, 0, 'f', 1) : tr("Across, %1 from the top").arg(guide.at, 0, 'f', 1));
        m_list->setCurrentRow(guides.isEmpty() ? -1 : std::clamp(row, 0, int(guides.size()) - 1));
    }
    int const now = m_list->currentRow();
    QSignalBlocker const block(m_position);
    m_position->setEnabled(now >= 0);
    if (now >= 0)
        m_position->setValue(guides[now].at);
    m_rebuilding = false;
}
