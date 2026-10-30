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
#include "model/scene/PsdRead.h"
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QDockWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"

namespace {
// A picture shown as large as the view allows, in its proportions: zoomed, moved, turned and mirrored as asked.
class ReferencePicture : public QWidget {
public:
    using QWidget::QWidget;
    // What a click does: moves the picture (Select), zooms (Zoom), or takes its colour (Colour Eyedropper).
    enum Mode { Select, Zoom, Eyedropper };
    void show(QImage const& image)
    {
        m_image = image;
        fit();
    }
    QImage const& image() const { return m_image; }
    // With Alt (or in Eyedropper mode), a press takes the colour of the picture there: this is told which.
    std::function<void(QColor const&)> picked;
    // Back to the whole picture, as large as the view lets it be, upright.
    void fit()
    {
        m_zoom = 1.0;
        m_offset = QPointF();
        m_angle = 0.0;
        update();
    }
    void flip()
    {
        m_flipped = !m_flipped;
        update();
    }
    // 1 is the whole picture fitted to the view.
    double zoom() const { return m_zoom; }
    double angle() const { return m_angle; }
    bool flipped() const { return m_flipped; }
    QPointF offset() const { return m_offset; }
    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; setCursor(mode == Zoom ? Qt::SizeAllCursor : mode == Eyedropper ? Qt::CrossCursor : Qt::ArrowCursor); }
    void setZoom(double zoom)
    {
        m_zoom = std::clamp(zoom, 0.25, 16.0);
        update();
    }
    void resetZoom() { setZoom(1.0); }
    void resetPan()
    {
        m_offset = QPointF();
        update();
    }
    void turn(double degrees)
    {
        m_angle = std::fmod(m_angle + degrees, 360.0);
        update();
    }
    void resetRotation()
    {
        m_angle = 0.0;
        update();
    }

protected:
    QRectF pictureBox() const
    {
        QSizeF const fitted = QSizeF(m_image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
        QRectF box(QPointF(0, 0), fitted);
        box.moveCenter(QRectF(rect()).center() + m_offset);
        return box;
    }
    // From the picture's box (as if upright and not mirrored) to the view.
    QTransform turned() const
    {
        QPointF const c = pictureBox().center();
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(m_angle);
        if (m_flipped)
            t.scale(-1, 1);
        t.translate(-c.x(), -c.y());
        return t;
    }
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
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setTransform(turned());
        p.drawImage(pictureBox(), m_image);
    }
    // The wheel zooms, a drag moves the picture about, a double click fits it again.
    void wheelEvent(QWheelEvent* event) override
    {
        setZoom(m_zoom * std::pow(1.0015, event->angleDelta().y()));
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        m_last = event->position();
        bool const alt = event->modifiers() & Qt::AltModifier;
        if (m_mode == Zoom && !m_image.isNull()) {
            // About the place clicked: it stays under the pointer.
            QPointF const middle = QRectF(rect()).center() + m_offset;
            double const was = m_zoom;
            setZoom(m_zoom * (alt ? 1.0 / 1.5 : 1.5));
            m_offset += (middle - event->position()) * (m_zoom / was - 1.0);
            update();
            return;
        }
        if ((!alt && m_mode != Eyedropper) || m_image.isNull() || !picked)
            return;
        QRectF const box = pictureBox();
        QPointF const at = turned().inverted().map(event->position());
        if (!box.contains(at) || box.width() < 1 || box.height() < 1)
            return;
        double const u = (at.x() - box.left()) / box.width();
        double const v = (at.y() - box.top()) / box.height();
        QColor colour = m_image.pixelColor(std::clamp(int(u * m_image.width()), 0, m_image.width() - 1), std::clamp(int(v * m_image.height()), 0, m_image.height() - 1));
        colour.setAlpha(255);
        picked(colour);
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) && !(event->modifiers() & Qt::AltModifier) && m_mode == Select) {
            m_offset += event->position() - m_last;
            m_last = event->position();
            update();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        if (m_mode == Select)
            fit();
    }

private:
    QImage m_image;
    double m_zoom = 1.0;
    double m_angle = 0.0;
    QPointF m_offset, m_last;
    bool m_flipped = false;
    Mode m_mode = Select;
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
        if (QImageReader(file).canRead() || Psd::isPsd(file)) {
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
        QImage const image = Psd::isPsd(file) ? Psd::read(file) : reader.read();
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

QString MainWindow::referenceOfDrawing()
{
    // The drawing of the layer worked on, as the frame shows it, written beside the scene (or with Eclipse's own files for a scene not saved yet).
    Scene const& scene = m_document->scene();
    int const layer = m_canvas->currentLayer(), frame = m_canvas->currentFrame();
    if (layer < 0 || layer >= scene.layers.size() || scene.layers[layer].drawingAt(frame) == Layer::kNoDrawing)
        return QString();
    Layer const& l = scene.layers[layer];
    QImage image(QSize(int(std::ceil(scene.width)), int(std::ceil(scene.height))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setTransform(scene.worldTransform(layer, frame));
        Canvas::paintDrawing(p, scene, l.drawings[l.cells[frame]], 1.0);
    }
    QString const folder = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/references")
                                            : QFileInfo(m_path).path() + QStringLiteral("/references");
    QDir().mkpath(folder);
    QString const name = QStringLiteral("%1-%2.png").arg(l.name.isEmpty() ? QStringLiteral("drawing") : l.name).arg(frame + 1);
    QString file = folder + QLatin1Char('/') + name;
    for (int n = 2; QFileInfo::exists(file); ++n)
        file = folder + QLatin1Char('/') + QFileInfo(name).completeBaseName() + QStringLiteral(" %1.png").arg(n);
    return image.save(file) ? file : QString();
}

void MainWindow::onActionShowReferenceView()
{
    // Shift+K: the Reference view (the colour model) shown, or put away.
    QDockWidget* const dock = dockOf("ReferenceDock");
    if (!dock)
        return;
    if (dock->isVisible())
        dock->hide();
    else
        showView(dock);
    m_actions->setChecked(QStringLiteral("ShowReferenceView"), dock->isVisible());
}

void MainWindow::onActionReference(QString const& what)
{
    ReferencePicture* const picture = dynamic_cast<ReferencePicture*>(findChild<QWidget*>(QStringLiteral("ReferencePicture")));
    if (!picture || !m_references)
        return;
    int const row = m_references->currentRow(), count = m_references->count();
    if (what == QLatin1String("select") || what == QLatin1String("zoom") || what == QLatin1String("eyedropper")) {
        picture->setMode(what == QLatin1String("zoom") ? ReferencePicture::Zoom : what == QLatin1String("eyedropper") ? ReferencePicture::Eyedropper : ReferencePicture::Select);
        m_actions->setChecked(QStringLiteral("RefSelect"), picture->mode() == ReferencePicture::Select);
        m_actions->setChecked(QStringLiteral("RefZoomTool"), picture->mode() == ReferencePicture::Zoom);
        m_actions->setChecked(QStringLiteral("RefEyedropper"), picture->mode() == ReferencePicture::Eyedropper);
    } else if (what == QLatin1String("previous") || what == QLatin1String("next")) {
        if (count > 0)
            m_references->setCurrentRow((std::max(row, 0) + (what == QLatin1String("next") ? 1 : count - 1)) % count);
    } else if (what == QLatin1String("unload") || what == QLatin1String("delete")) {
        // Unload: out of the scene's list. Delete: its file goes too.
        QStringList list = m_document->scene().references;
        if (row < 0 || row >= list.size())
            return;
        QString const file = list[row];
        if (what == QLatin1String("delete")
            && QMessageBox::question(this, tr("Delete Current"), tr("Delete the picture's file?\n%1").arg(QDir::toNativeSeparators(file))) != QMessageBox::Yes)
            return;
        list.removeAt(row);
        m_document->setReferences(list, what == QLatin1String("delete") ? tr("Delete Reference Picture") : tr("Remove Reference Picture"));
        if (what == QLatin1String("delete"))
            QFile::remove(file);
        syncReferences();
        if (m_references->count() > 0)
            m_references->setCurrentRow(std::min(row, m_references->count() - 1));
    } else if (what == QLatin1String("useDrawing")) {
        QString const file = referenceOfDrawing();
        if (file.isEmpty())
            statusBar()->showMessage(tr("There is no drawing on this frame to use as a reference."), 4000);
        else
            addReferences({file});
    } else if (what == QLatin1String("loadDefaults")) {
        // The pictures kept in Eclipse's own references folder, for every scene.
        QDir const folder(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/references/default"));
        QStringList files;
        for (QFileInfo const& one : folder.entryInfoList(QDir::Files, QDir::Name))
            if (!m_document->scene().references.contains(one.filePath()) && (QImageReader(one.filePath()).canRead() || Psd::isPsd(one.filePath())))
                files << one.filePath();
        if (files.isEmpty())
            statusBar()->showMessage(tr("No default references: put pictures in %1").arg(QDir::toNativeSeparators(folder.path())), 6000);
        else
            addReferences(files);
    } else if (what == QLatin1String("zoomIn") || what == QLatin1String("zoomOut")) {
        picture->setZoom(picture->zoom() * (what == QLatin1String("zoomIn") ? 1.25 : 0.8));
    } else if (what == QLatin1String("resetZoom")) {
        picture->resetZoom();
    } else if (what == QLatin1String("resetPan")) {
        picture->resetPan();
    } else if (what == QLatin1String("resetView")) {
        picture->fit();
    } else if (what == QLatin1String("rotateCW") || what == QLatin1String("rotateCCW")) {
        picture->turn(what == QLatin1String("rotateCW") ? 30.0 : -30.0);
    } else if (what == QLatin1String("resetRotation")) {
        picture->resetRotation();
    } else if (what == QLatin1String("mirror")) {
        picture->flip();
        m_actions->setChecked(QStringLiteral("RefMirror"), picture->flipped());
    }
}
