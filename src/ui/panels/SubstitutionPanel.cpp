#include "ui/panels/SubstitutionPanel.h"
#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
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
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("SubstitutionSearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("SubstitutionLayer"));
    m_size = new QSlider(Qt::Horizontal, this);
    m_size->setObjectName(QStringLiteral("SubstitutionSize"));
    m_size->setRange(50, 250);
    m_size->setValue(100);
    m_size->setFixedWidth(110);
    m_size->setToolTip(tr("How big the pictures of the drawings are."));
    QHBoxLayout* const foot = new QHBoxLayout;
    foot->addWidget(m_name, 1);
    foot->addWidget(m_size);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_search);
    layout->addWidget(m_list, 1);
    layout->addLayout(foot);
    connect(m_search, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(m_size, &QSlider::valueChanged, this, [this](int percent) {
        m_list->setIconSize(QSize(kThumbWidth * percent / 100, kThumbHeight * percent / 100));
    });

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (m_rebuilding)
            return;
        int const drawing = item->data(Qt::UserRole).toInt();
        if (drawing >= 0)
            m_document->substituteDrawing(m_layer, m_frame, drawing);
        else
            m_document->clearCell(m_layer, m_frame);    // No Exposure: nothing is shown on the frame
    });
    connect(m_document, &Document::changed, this, &SubstitutionPanel::rebuild);
    rebuild();
}

int SubstitutionPanel::count() const
{
    return std::max(0, m_list->count() - 1);    // the first tile is No Exposure
}

// A dark tile with a cross: the frame shows no drawing.
static QIcon noExposureIcon()
{
    QPixmap pixmap(kThumbWidth, kThumbHeight);
    pixmap.fill(QColor(14, 14, 14));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(150, 150, 150), 1.5));
    double const m = std::min(kThumbWidth, kThumbHeight) * 0.22;
    p.drawLine(QPointF(kThumbWidth / 2.0 - m, kThumbHeight / 2.0 - m), QPointF(kThumbWidth / 2.0 + m, kThumbHeight / 2.0 + m));
    p.drawLine(QPointF(kThumbWidth / 2.0 + m, kThumbHeight / 2.0 - m), QPointF(kThumbWidth / 2.0 - m, kThumbHeight / 2.0 + m));
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
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
    m_list->setCurrentRow(shown + 1);  // the first tile is No Exposure: an empty frame picks it
    m_rebuilding = false;
}

void SubstitutionPanel::rebuild()
{
    Scene const& scene = m_document->scene();
    m_rebuilding = true;
    m_list->clear();
    if (m_layer >= 0 && m_layer < scene.layers.size()) {
        Layer const& layer = scene.layers[m_layer];
        QListWidgetItem* const none = new QListWidgetItem(noExposureIcon(), tr("No Exposure"), m_list);
        none->setData(Qt::UserRole, -1);
        for (int i = 0; i < layer.drawings.size(); ++i) {
            QListWidgetItem* const item = new QListWidgetItem(thumbnail(scene, layer.drawings[i]), layer.drawingLabel(i), m_list);
            item->setData(Qt::UserRole, i);
        }
        m_list->setCurrentRow(layer.drawingAt(m_frame) + 1);
        m_name->setText(layer.name);
    } else {
        m_name->clear();
    }
    m_rebuilding = false;
    filter();
}

// The tiles of the drawings whose name has what is typed in the search box; No Exposure stays.
void SubstitutionPanel::filter()
{
    QString const wanted = m_search->text().trimmed();
    for (int row = 1; row < m_list->count(); ++row)
        m_list->item(row)->setHidden(!wanted.isEmpty() && !m_list->item(row)->text().contains(wanted, Qt::CaseInsensitive));
}

void SubstitutionPanel::setThumbnailScale(int percent)
{
    m_size->setValue(std::clamp(percent, 50, 250));
}

int SubstitutionPanel::thumbnailScale() const
{
    return m_size->value();
}
