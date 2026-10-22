// Eclipse - the Reference view: pictures to look at while drawing.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"

namespace {
// A picture shown as large as the view allows, in its proportions.
class ReferencePicture : public QWidget {
public:
    using QWidget::QWidget;
    void show(QImage const& image)
    {
        m_image = image;
        fit();
    }
    QImage const& image() const { return m_image; }
    // With Alt, a press takes the colour of the picture there: this is told which.
    std::function<void(QColor const&)> picked;
    // Back to the whole picture, as large as the view lets it be.
    void fit()
    {
        m_zoom = 1.0;
        m_offset = QPointF();
        update();
    }
    void flip()
    {
        m_flipped = !m_flipped;
        update();
    }
    // 1 is the whole picture fitted to the view.
    double zoom() const { return m_zoom; }
    bool flipped() const { return m_flipped; }
    void setZoom(double zoom)
    {
        m_zoom = std::clamp(zoom, 0.25, 16.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(46, 46, 46));
        if (m_image.isNull()) {
            p.setPen(QColor(150, 150, 150));
            p.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                       QCoreApplication::translate("MainWindow", "File > Import > To Reference View... brings pictures here."));
            return;
        }
        QSizeF const fitted = QSizeF(m_image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
        QRectF box(QPointF(0, 0), fitted);
        box.moveCenter(QRectF(rect()).center() + m_offset);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (m_flipped) {
            // Left to right, about the middle of the picture.
            p.translate(box.center().x(), 0);
            p.scale(-1, 1);
            p.translate(-box.center().x(), 0);
        }
        p.drawImage(box, m_image);
    }
    // The wheel zooms, a drag moves the picture about, a double click fits it again.
    void wheelEvent(QWheelEvent* event) override
    {
        setZoom(m_zoom * std::pow(1.0015, event->angleDelta().y()));
    }
    QRectF pictureBox() const
    {
        QSizeF const fitted = QSizeF(m_image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
        QRectF box(QPointF(0, 0), fitted);
        box.moveCenter(QRectF(rect()).center() + m_offset);
        return box;
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        m_last = event->position();
        if (!(event->modifiers() & Qt::AltModifier) || m_image.isNull() || !picked)
            return;
        QRectF const box = pictureBox();
        if (!box.contains(event->position()) || box.width() < 1 || box.height() < 1)
            return;
        double u = (event->position().x() - box.left()) / box.width();
        double const v = (event->position().y() - box.top()) / box.height();
        if (m_flipped)
            u = 1.0 - u;
        QColor colour = m_image.pixelColor(std::clamp(int(u * m_image.width()), 0, m_image.width() - 1), std::clamp(int(v * m_image.height()), 0, m_image.height() - 1));
        colour.setAlpha(255);
        picked(colour);
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) && !(event->modifiers() & Qt::AltModifier)) {
            m_offset += event->position() - m_last;
            m_last = event->position();
            update();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent*) override { fit(); }

private:
    QImage m_image;
    double m_zoom = 1.0;
    QPointF m_offset, m_last;
    bool m_flipped = false;
};
}  // namespace

QWidget* MainWindow::makeReferenceView()
{
    QWidget* const view = new QWidget(this);
    view->setObjectName(QStringLiteral("ReferenceView"));
    ReferencePicture* const picture = new ReferencePicture(view);
    picture->setObjectName(QStringLiteral("ReferencePicture"));
    picture->setMinimumSize(120, 90);
    // Along the bottom: the pictures brought in, small; a click shows one.
    m_references = new QListWidget(view);
    m_references->setObjectName(QStringLiteral("ReferenceList"));
    m_references->setViewMode(QListView::IconMode);
    m_references->setFlow(QListView::LeftToRight);
    m_references->setWrapping(false);
    m_references->setMovement(QListView::Static);
    m_references->setIconSize(QSize(64, 48));
    m_references->setFixedHeight(84);
    QPushButton* const add = new QPushButton(tr("Add..."), view);
    QPushButton* const remove = new QPushButton(tr("Remove"), view);
    remove->setObjectName(QStringLiteral("ReferenceRemove"));
    QHBoxLayout* const row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    QPushButton* const flip = new QPushButton(tr("Flip"), view);
    flip->setObjectName(QStringLiteral("ReferenceFlip"));
    flip->setToolTip(tr("Turn the picture over, left to right"));
    QPushButton* const fit = new QPushButton(tr("Fit"), view);
    fit->setObjectName(QStringLiteral("ReferenceFit"));
    fit->setToolTip(tr("The whole picture again. The wheel zooms, a drag moves the picture, a double click fits it.\nAlt+click takes the colour of the picture there."));
    // A colour taken from the picture is the colour in hand, as with the Dropper on the drawings.
    picture->picked = [this](QColor const& colour) { emit m_canvas->colourPicked(Scene::kNoSwatch, colour); };
    row->addWidget(flip);
    row->addWidget(fit);
    connect(flip, &QPushButton::clicked, picture, [picture] { picture->flip(); });
    connect(fit, &QPushButton::clicked, picture, [picture] { picture->fit(); });
    QVBoxLayout* const layout = new QVBoxLayout(view);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(picture, 1);
    layout->addWidget(m_references);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, &MainWindow::onActionImportReference);
    connect(remove, &QPushButton::clicked, this, [this] {
        int const row = m_references->currentRow();
        QStringList list = m_document->scene().references;
        if (row < 0 || row >= list.size())
            return;
        list.removeAt(row);
        m_document->setReferences(list, tr("Remove Reference Picture"));
        syncReferences();
    });
    connect(m_references, &QListWidget::currentItemChanged, this, [picture](QListWidgetItem* item) {
        picture->show(item ? item->data(Qt::UserRole).value<QImage>() : QImage());
    });
    return view;
}

int MainWindow::addReferences(QStringList const& files)
{
    // The pictures belong to the scene: it keeps the files they are read from.
    QStringList list = m_document->scene().references;
    int read = 0;
    for (QString const& file : files) {
        if (QImageReader(file).canRead()) {
            list << file;
            ++read;
        }
    }
    if (read == 0)
        return 0;
    m_document->setReferences(list, tr("Add Reference Picture"));
    syncReferences();
    m_references->setCurrentRow(m_references->count() - 1);
    showView(dockOf("ReferenceDock"));
    return read;
}

void MainWindow::syncReferences()
{
    QStringList const files = m_document->scene().references;
    if (!m_references || files == m_referenceFiles)
        return;
    m_referenceFiles = files;
    m_references->clear();
    for (QString const& file : files) {
        QImageReader reader(file);
        reader.setAutoTransform(true);
        QImage const image = reader.read();
        // A picture whose file has gone keeps its place, so that the list still says what was there.
        QListWidgetItem* const item = image.isNull()
            ? new QListWidgetItem(tr("%1 (missing)").arg(QFileInfo(file).completeBaseName()), m_references)
            : new QListWidgetItem(QIcon(QPixmap::fromImage(image.scaled(64, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
                                  QFileInfo(file).completeBaseName(), m_references);
        item->setData(Qt::UserRole, image);
        item->setToolTip(QDir::toNativeSeparators(file));
    }
    if (m_references->count() > 0)
        m_references->setCurrentRow(0);
}

void MainWindow::onActionImportReference()
{
    QStringList const files = QFileDialog::getOpenFileNames(this, tr("Import to Reference View"), m_path.isEmpty() ? QString() : QFileInfo(m_path).path(), kImageFilter);
    if (files.isEmpty())
        return;
    int const read = addReferences(files);
    if (read < files.size())
        QMessageBox::warning(this, tr("Eclipse"), tr("%1 of the %2 files could not be read as pictures.").arg(files.size() - read).arg(files.size()));
}
