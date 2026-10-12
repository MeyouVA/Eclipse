#include "ui/panels/SubstitutionPanel.h"

#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"

static constexpr int kThumbWidth = 80;
static constexpr int kThumbHeight = 60;

// The drawing alone, on white, as large as it fits.
static QIcon thumbnail(Scene const& scene, Drawing const& drawing)
{
    QPixmap pixmap(kThumbWidth, kThumbHeight);
    pixmap.fill(Qt::white);
    QRectF const bounds = drawing.bounds();
    if (!bounds.isEmpty()) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        double const scale = std::min((kThumbWidth - 8) / bounds.width(), (kThumbHeight - 8) / bounds.height());
        painter.translate(kThumbWidth / 2.0, kThumbHeight / 2.0);
        painter.scale(scale, scale);
        painter.translate(-bounds.center());
        Canvas::paintDrawing(painter, scene, drawing, 1.0);
    }
    // The same picture when selected: the default would tint it.
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}

SubstitutionPanel::SubstitutionPanel(Document* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(4);
    m_list->setToolTip(tr("Click a drawing to show it on the current frame."));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!m_rebuilding)
            m_document->substituteDrawing(m_layer, m_frame, item->data(Qt::UserRole).toInt());
    });
    connect(m_document, &Document::changed, this, &SubstitutionPanel::rebuild);
    rebuild();
}

int SubstitutionPanel::count() const
{
    return m_list->count();
}

void SubstitutionPanel::setCurrentCell(int layer, int frame)
{
    bool const layerChanged = layer != m_layer;
    m_layer = layer;
    m_frame = frame;
    if (layerChanged) {
        rebuild();
        return;
    }
    // Same drawings, another frame: only which one is shown changes.
    Scene const& scene = m_document->scene();
    int const shown = m_layer >= 0 && m_layer < scene.layers.size() ? scene.layers[m_layer].drawingAt(m_frame) : Layer::kNoDrawing;
    m_rebuilding = true;
    m_list->setCurrentRow(shown);  // -1 clears
    m_rebuilding = false;
}

void SubstitutionPanel::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    m_list->clear();
    if (m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        for (int i = 0; i < layer.drawings.size(); ++i) {
            QListWidgetItem* const item = new QListWidgetItem(thumbnail(scene, layer.drawings[i]), QString::number(i + 1), m_list);
            item->setData(Qt::UserRole, i);
        }
        m_list->setCurrentRow(layer.drawingAt(m_frame));
    }
    m_rebuilding = false;
}
