#include "ui/panels/LibraryView.h"
#include <algorithm>

#include <functional>

#include <cmath>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QInputDialog>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/canvas/Canvas.h"

QString const LibraryView::kSuffix = QStringLiteral("ecltpl");
QString const LibraryView::kMimeType = QStringLiteral("application/x-eclipse-template");

namespace {
// The list of templates: one dragged out of it carries the path of its file.
class TemplateList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem* const item = currentItem();
        if (!item)
            return;
        QMimeData* const mime = new QMimeData;
        mime->setData(LibraryView::kMimeType, item->data(Qt::UserRole).toString().toUtf8());
        QDrag* const drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(item->icon().pixmap(iconSize()));
        drag->exec(Qt::CopyAction);
    }
};
}  // namespace

QString LibraryView::defaultFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("Eclipse Library"));
}

int LibraryView::firstDrawnFrame(Scene const& scene)
{
    for (int f = 0; f < scene.frameCount(); ++f)
        for (Layer const& layer : scene.layers)
            if (layer.holdsDrawings() && layer.drawingAt(f) != Layer::kNoDrawing)
                return f;
    return 0;
}

bool LibraryView::isTemplate(QString const& path)
{
    return QFileInfo(path).suffix().compare(kSuffix, Qt::CaseInsensitive) == 0;
}

// The picture of a thing that is not a template: a sound as a run of bars, a palette as its colours.
static QImage otherPicture(QString const& path, QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(46, 46, 46));
    if (size.isEmpty())
        return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (QFileInfo(path).suffix().compare(QLatin1String("eclpal"), Qt::CaseInsensitive) == 0) {
        QFile file(path);
        QJsonArray const colours = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()[QStringLiteral("colours")].toArray() : QJsonArray();
        int const across = 4, down = 3;
        double const w = size.width() / double(across), h = size.height() / double(down);
        for (int i = 0; i < std::min<int>(int(colours.size()), across * down); ++i) {
            QColor const colour(colours[i].toObject()[QStringLiteral("color")].toString());
            painter.fillRect(QRectF((i % across) * w + 1.0, (i / across) * h + 1.0, w - 2.0, h - 2.0), colour.isValid() ? colour : QColor(Qt::gray));
        }
    } else {
        // (Bars as of a sound - the same for every sound: it is not read to make them.)
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(110, 190, 230));
        int const bars = 14;
        double const step = size.width() / double(bars + 1);
        for (int i = 0; i < bars; ++i) {
            double const tall = size.height() * (0.2 + 0.6 * std::abs(std::sin(i * 1.7 + 0.6)));
            painter.drawRoundedRect(QRectF(step * (i + 0.6), (size.height() - tall) / 2.0, step * 0.6, tall), 1.5, 1.5);
        }
    }
    return image;
}

QImage LibraryView::picture(Scene const& scene, int frame, QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(46, 46, 46));
    if (scene.width < 1 || scene.height < 1 || size.isEmpty())
        return image;
    // The frame of the scene, as large as fits, in the middle.
    double const scale = std::min(double(size.width()) / scene.width, double(size.height()) / scene.height);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate((size.width() - scene.width * scale) / 2.0, (size.height() - scene.height * scale) / 2.0);
    painter.scale(scale, scale);
    painter.setClipRect(QRectF(0, 0, scene.width, scene.height));
    Canvas::paintFrame(painter, scene, std::clamp(frame, 0, scene.frameCount() - 1));
    return image;
}

LibraryView::LibraryView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("LibraryView"));

    // The template chosen, larger, with its frames to look through.
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("LibraryPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(120);
    m_preview->setStyleSheet(QStringLiteral("background-color: #2e2e2e;"));
    m_scrub = new QSlider(Qt::Horizontal, this);
    m_scrub->setObjectName(QStringLiteral("LibraryScrub"));
    m_scrub->setEnabled(false);
    m_about = new QLabel(this);
    m_about->setObjectName(QStringLiteral("LibraryAbout"));

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("LibrarySearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);

    m_folderList = new QListWidget(this);
    m_folderList->setObjectName(QStringLiteral("LibraryFolders"));
    TemplateList* const items = new TemplateList(this);
    m_items = items;
    m_items->setObjectName(QStringLiteral("LibraryItems"));
    m_items->setViewMode(QListView::IconMode);
    m_items->setResizeMode(QListView::Adjust);
    m_items->setMovement(QListView::Static);
    m_items->setIconSize(QSize(96, 54));
    m_items->setGridSize(QSize(112, 82));
    m_items->setWordWrap(true);
    m_items->setDragEnabled(true);
    m_items->setToolTip(tr("Double-click a template to put it in the scene, or drag it onto the Timeline or the Camera view."));

    auto small = [this](QString const& text, QString const& tip, char const* name) {
        QToolButton* const b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setObjectName(QLatin1String(name));
        return b;
    };
    QToolButton* const addFolderButton = small(QStringLiteral("+"), tr("Add a folder to the library"), "LibraryAddFolder");
    QToolButton* const newFolderButton = small(QString(QChar(0x2295)), tr("Make a new folder inside the selected one"), "LibraryNewFolder");
    QToolButton* const removeFolderButton = small(QString(QChar(0x2212)), tr("Take the selected folder out of the library (its files stay)"), "LibraryRemoveFolder");
    QPushButton* const create = new QPushButton(tr("New Template from Selection..."), this);
    create->setObjectName(QStringLiteral("LibraryCreate"));
    QPushButton* const insert = new QPushButton(tr("Insert"), this);
    insert->setObjectName(QStringLiteral("LibraryInsert"));
    QPushButton* const asSymbol = new QPushButton(tr("As Symbol"), this);
    asSymbol->setObjectName(QStringLiteral("LibraryInsertSymbol"));
    asSymbol->setToolTip(tr("Puts the template in as a symbol: its layers kept together, shown by one layer."));
    QPushButton* const special = new QPushButton(tr("Paste Special..."), this);
    special->setObjectName(QStringLiteral("LibraryPasteSpecial"));
    special->setToolTip(tr("Puts the template in as new layers or onto the layers of the same names, with or without its drawings and keyframes."));
    QPushButton* const rename = new QPushButton(tr("Rename..."), this);
    rename->setObjectName(QStringLiteral("LibraryRename"));
    QPushButton* const remove = new QPushButton(tr("Delete"), this);
    remove->setObjectName(QStringLiteral("LibraryDelete"));

    QWidget* const left = new QWidget(this);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);
    QHBoxLayout* const folderBar = new QHBoxLayout;
    folderBar->addWidget(new QLabel(tr("Folders"), left));
    folderBar->addStretch(1);
    folderBar->addWidget(addFolderButton);
    folderBar->addWidget(newFolderButton);
    folderBar->addWidget(removeFolderButton);
    leftLayout->addLayout(folderBar);
    leftLayout->addWidget(m_folderList, 1);
    QSplitter* const lists = new QSplitter(Qt::Horizontal, this);
    lists->addWidget(left);
    lists->addWidget(m_items);
    lists->setStretchFactor(0, 0);
    lists->setStretchFactor(1, 1);
    lists->setSizes({130, 300});

    QHBoxLayout* const buttons = new QHBoxLayout;
    buttons->addWidget(create);
    buttons->addStretch(1);
    buttons->addWidget(insert);
    buttons->addWidget(asSymbol);
    buttons->addWidget(special);
    buttons->addWidget(rename);
    buttons->addWidget(remove);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);
    layout->addWidget(m_preview);
    layout->addWidget(m_scrub);
    layout->addWidget(m_about);
    layout->addWidget(m_search);
    layout->addWidget(lists, 1);
    layout->addLayout(buttons);

    connect(m_folderList, &QListWidget::currentRowChanged, this, [this] { refresh(); });
    connect(m_items, &QListWidget::currentItemChanged, this, [this] { showTemplate(); });
    connect(m_items, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { emit insertRequested(item->data(Qt::UserRole).toString()); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_scrub, &QSlider::valueChanged, this, &LibraryView::showFrame);
    connect(create, &QPushButton::clicked, this, &LibraryView::createRequested);
    connect(insert, &QPushButton::clicked, this, [this] {
        if (!currentTemplate().isEmpty())
            emit insertRequested(currentTemplate());
    });
    connect(asSymbol, &QPushButton::clicked, this, [this] {
        if (currentTemplate().endsWith(QLatin1Char('.') + kSuffix))
            emit insertAsSymbolRequested(currentTemplate());
    });
    connect(special, &QPushButton::clicked, this, [this] {
        if (currentTemplate().endsWith(QLatin1Char('.') + kSuffix))
            emit pasteSpecialRequested(currentTemplate());
    });
    connect(remove, &QPushButton::clicked, this, [this] { deleteCurrent(true); });
    connect(rename, &QPushButton::clicked, this, [this] {
        QString const path = currentTemplate();
        if (path.isEmpty())
            return;
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("Rename Template"), tr("Name of the template:"), QLineEdit::Normal,
                                                   QFileInfo(path).completeBaseName(), &ok);
        if (ok && name.trimmed() != QFileInfo(path).completeBaseName() && !renameCurrent(name))
            QMessageBox::warning(this, tr("Eclipse"), tr("The template could not be given that name: it is taken, or it is not a name a file can have."));
    });
    connect(addFolderButton, &QToolButton::clicked, this, [this] {
        QString const folder = QFileDialog::getExistingDirectory(this, tr("Add a Folder to the Library"), currentFolder());
        if (!folder.isEmpty() && addFolder(folder))
            setCurrentFolder(int(m_folders.size()) - 1);
    });
    connect(removeFolderButton, &QToolButton::clicked, this, [this] {
        // Only a folder of the library itself is taken out of it; one inside it is part of that folder.
        if (m_folderList->currentItem())
            removeFolder(m_folderList->currentItem()->data(Qt::UserRole + 1).toInt());
    });
    // The right button on a folder: the same, and Refresh for what was changed outside the program.
    m_folderList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderList, &QListWidget::customContextMenuRequested, this, [this, newFolderButton, removeFolderButton](QPoint const& pos) {
        if (QListWidgetItem* const item = m_folderList->itemAt(pos))
            m_folderList->setCurrentItem(item);
        QMenu menu(this);
        connect(menu.addAction(tr("New Folder...")), &QAction::triggered, newFolderButton, &QToolButton::click);
        QAction* const refreshAll = menu.addAction(tr("Refresh"));
        refreshAll->setShortcut(QKeySequence(Qt::Key_F5));
        connect(refreshAll, &QAction::triggered, this, [this] { showFolders(); });
        menu.addSeparator();
        QAction* const take = menu.addAction(tr("Remove from the Library"));
        take->setEnabled(m_folderList->currentItem() && m_folderList->currentItem()->data(Qt::UserRole + 1).toInt() >= 1);
        connect(take, &QAction::triggered, removeFolderButton, &QToolButton::click);
        menu.exec(m_folderList->viewport()->mapToGlobal(pos));
    });
    connect(newFolderButton, &QToolButton::clicked, this, [this] {
        bool ok = false;
        QString const name = QInputDialog::getText(this, tr("New Folder"), tr("Name of the folder to make in \"%1\":").arg(QDir(currentFolder()).dirName()),
                                                   QLineEdit::Normal, tr("New Folder"), &ok);
        if (ok && !addSubFolder(name))
            QMessageBox::warning(this, tr("Eclipse"), tr("The folder could not be made: the name is taken, or it is not a name a folder can have."));
    });

    // The folders kept from last time, after the one that is always there.
    QStringList kept = QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("library/folders")).toStringList();
    kept.removeAll(defaultFolder());
    kept.prepend(defaultFolder());
    m_folders = kept;
    showFolders();
}

void LibraryView::remember()
{
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("library/folders"), m_folders.mid(1));
}

void LibraryView::showFolders()
{
    QString const was = m_folderList->currentItem() ? m_folderList->currentItem()->data(Qt::UserRole).toString() : QString();
    {
        QSignalBlocker const block(m_folderList);
        m_folderList->clear();
        // Each folder of the library, and under it - set in - the folders inside it.
        std::function<void(QString const&, int, int)> list = [&](QString const& folder, int root, int depth) {
            QListWidgetItem* const item = new QListWidgetItem(QString(depth * 4, QChar(0x20)) + QDir(folder).dirName(), m_folderList);
            item->setToolTip(QDir::toNativeSeparators(folder));
            item->setData(Qt::UserRole, folder);
            item->setData(Qt::UserRole + 1, depth == 0 ? root : -1);
            if (depth >= 6)
                return;     // deep enough for a library
            for (QString const& inner : QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
                list(QDir(folder).filePath(inner), root, depth + 1);
        };
        for (int i = 0; i < m_folders.size(); ++i)
            list(m_folders[i], i, 0);
        int row = 0;
        for (int i = 0; i < m_folderList->count(); ++i)
            if (m_folderList->item(i)->data(Qt::UserRole).toString() == was)
                row = i;
        m_folderList->setCurrentRow(row);
    }
    refresh();
}

void LibraryView::setFolders(QStringList const& folders)
{
    m_folders = folders;
    if (m_folders.isEmpty())
        m_folders << defaultFolder();
    showFolders();
}

bool LibraryView::addFolder(QString const& folder)
{
    QString const clean = QDir::cleanPath(folder);
    if (clean.isEmpty() || m_folders.contains(clean))
        return false;
    m_folders << clean;
    remember();
    showFolders();
    return true;
}

void LibraryView::removeFolder(int index)
{
    if (index < 1 || index >= m_folders.size())
        return;     // the first folder is the library itself
    m_folders.removeAt(index);
    remember();
    showFolders();
}

QString LibraryView::currentFolder() const
{
    return m_folderList->currentItem() ? m_folderList->currentItem()->data(Qt::UserRole).toString() : m_folders.value(0);
}

void LibraryView::setCurrentFolder(int index)
{
    for (int row = 0; row < m_folderList->count(); ++row)
        if (m_folderList->item(row)->data(Qt::UserRole + 1).toInt() == index)
            m_folderList->setCurrentRow(row);
}

bool LibraryView::setCurrentPath(QString const& folder)
{
    for (int row = 0; row < m_folderList->count(); ++row) {
        if (QDir(m_folderList->item(row)->data(Qt::UserRole).toString()) == QDir(folder)) {
            m_folderList->setCurrentRow(row);
            return true;
        }
    }
    return false;
}

QStringList LibraryView::shownFolders() const
{
    QStringList shown;
    for (int row = 0; row < m_folderList->count(); ++row)
        shown << m_folderList->item(row)->data(Qt::UserRole).toString();
    return shown;
}

bool LibraryView::addSubFolder(QString const& name)
{
    QString const clean = name.trimmed();
    bool sound = !clean.isEmpty() && !clean.endsWith(QChar(0x2e));
    for (QChar const c : clean)
        sound = sound && !QStringLiteral("\\/:*?\"<>|").contains(c);
    QDir const here(currentFolder());
    if (!sound || here.exists(clean) || !here.mkdir(clean))
        return false;
    showFolders();
    setCurrentPath(here.filePath(clean));
    return true;
}

void LibraryView::setSearch(QString const& text)
{
    m_search->setText(text);
}

void LibraryView::refresh()
{
    QString const chosen = currentTemplate();
    QString const wanted = m_search->text().trimmed();
    {
        QSignalBlocker const block(m_items);
        m_items->clear();
        QDir const dir(currentFolder());
        for (QFileInfo const& file : dir.entryInfoList({QStringLiteral("*.") + kSuffix, QStringLiteral("*.wav"), QStringLiteral("*.eclpal")}, QDir::Files,
                                                       QDir::Name | QDir::IgnoreCase)) {
            if (!wanted.isEmpty() && !file.completeBaseName().contains(wanted, Qt::CaseInsensitive))
                continue;
            if (!isTemplate(file.absoluteFilePath())) {
                // A sound or a palette: told apart by its picture, and named with what it is.
                QListWidgetItem* const item = new QListWidgetItem(QIcon(QPixmap::fromImage(otherPicture(file.absoluteFilePath(), m_items->iconSize()))),
                                                                  file.fileName(), m_items);
                item->setData(Qt::UserRole, file.absoluteFilePath());
                item->setToolTip(QDir::toNativeSeparators(file.absoluteFilePath()));
                if (file.absoluteFilePath() == chosen)
                    m_items->setCurrentItem(item);
                continue;
            }
            // Its picture is made once for each version of the file.
            QString const key = file.absoluteFilePath() + QLatin1Char('|') + QString::number(file.lastModified().toMSecsSinceEpoch());
            auto thumbnail = m_thumbnails.find(key);
            if (thumbnail == m_thumbnails.end()) {
                Scene scene;
                QString error;
                QImage image(m_items->iconSize(), QImage::Format_ARGB32_Premultiplied);
                image.fill(QColor(70, 40, 40));     // a file that cannot be read
                if (Scene::load(file.absoluteFilePath(), scene, &error))
                    image = picture(scene, firstDrawnFrame(scene), m_items->iconSize());
                thumbnail = m_thumbnails.insert(key, image);
            }
            QListWidgetItem* const item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail.value())), file.completeBaseName(), m_items);
            item->setData(Qt::UserRole, file.absoluteFilePath());
            item->setToolTip(QDir::toNativeSeparators(file.absoluteFilePath()));
            if (file.absoluteFilePath() == chosen)
                m_items->setCurrentItem(item);
        }
    }
    showTemplate();
}

QStringList LibraryView::templates() const
{
    QStringList paths;
    for (int i = 0; i < m_items->count(); ++i)
        if (isTemplate(m_items->item(i)->data(Qt::UserRole).toString()))
            paths << m_items->item(i)->data(Qt::UserRole).toString();
    return paths;
}

QStringList LibraryView::otherItems() const
{
    QStringList paths;
    for (int i = 0; i < m_items->count(); ++i)
        if (!isTemplate(m_items->item(i)->data(Qt::UserRole).toString()))
            paths << m_items->item(i)->data(Qt::UserRole).toString();
    return paths;
}

QString LibraryView::currentTemplate() const
{
    QListWidgetItem const* const item = m_items->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void LibraryView::setCurrentTemplate(QString const& path)
{
    for (int i = 0; i < m_items->count(); ++i)
        if (QFileInfo(m_items->item(i)->data(Qt::UserRole).toString()) == QFileInfo(path))
            m_items->setCurrentRow(i);
}

void LibraryView::showTemplate()
{
    QString const path = currentTemplate();
    QString error;
    m_shownPath.clear();
    if (!path.isEmpty() && !isTemplate(path)) {
        // A sound or a palette: its picture, and what it is.
        m_shown = Scene();
        m_preview->setPixmap(QPixmap::fromImage(otherPicture(path, QSize(std::max(160, m_preview->width()), std::max(120, m_preview->height())))));
        bool const palette = QFileInfo(path).suffix().compare(QLatin1String("eclpal"), Qt::CaseInsensitive) == 0;
        m_about->setText(palette ? tr("%1   -   a palette: Insert adds it to the scene's palettes").arg(QFileInfo(path).completeBaseName())
                                 : tr("%1   -   a sound: Insert puts it on a sound layer from the current frame").arg(QFileInfo(path).completeBaseName()));
        m_scrub->setEnabled(false);
        return;
    }
    if (path.isEmpty() || !Scene::load(path, m_shown, &error)) {
        m_shown = Scene();
        m_preview->setPixmap(QPixmap());
        m_preview->setText(path.isEmpty() ? tr("No template selected") : tr("Cannot read this template:\n%1").arg(error));
        m_about->setText(QString());
        m_scrub->setEnabled(false);
        return;
    }
    m_shownPath = path;
    {
        QSignalBlocker const block(m_scrub);
        m_scrub->setRange(0, m_shown.frameCount() - 1);
        m_scrub->setValue(firstDrawnFrame(m_shown));
    }
    m_scrub->setEnabled(m_shown.frameCount() > 1);
    m_about->setText(tr("%1   -   %n layer(s)", nullptr, int(m_shown.layers.size())).arg(QFileInfo(path).completeBaseName())
                     + tr(", %n frame(s)", nullptr, m_shown.frameCount()));
    showFrame(m_scrub->value());
}

void LibraryView::showFrame(int frame)
{
    if (m_shownPath.isEmpty())
        return;
    QSize const size(std::max(160, m_preview->width()), std::max(120, m_preview->height()));
    m_preview->setPixmap(QPixmap::fromImage(picture(m_shown, frame, size)));
}

bool LibraryView::renameCurrent(QString const& name)
{
    QString const path = currentTemplate();
    QString const clean = name.trimmed();
    bool sound = !path.isEmpty() && !clean.isEmpty() && !clean.endsWith(QLatin1Char('.'));
    for (QChar const c : clean)
        sound = sound && !QStringLiteral("\\/:*?\"<>|").contains(c);
    if (!sound)
        return false;
    QFileInfo const info(path);
    QString const to = info.absolutePath() + QLatin1Char('/') + clean + QLatin1Char('.') + info.suffix();
    if (QFileInfo::exists(to) || !QFile::rename(path, to))
        return false;
    refresh();
    setCurrentTemplate(to);
    return true;
}

bool LibraryView::deleteCurrent(bool ask)
{
    QString const path = currentTemplate();
    if (path.isEmpty())
        return false;
    if (ask && QMessageBox::question(this, tr("Eclipse"), tr("Delete the template \"%1\"?\nIts file is removed from the library folder.").arg(QFileInfo(path).completeBaseName()))
                   != QMessageBox::Yes)
        return false;
    bool const gone = QFile::remove(path);
    refresh();
    return gone;
}

void LibraryView::setThumbnailScale(int percent)
{
    percent = std::clamp(percent, 50, 200);
    QSize const icon(96 * percent / 100, 54 * percent / 100);
    if (icon == m_items->iconSize())
        return;
    m_items->setIconSize(icon);
    m_items->setGridSize(QSize(icon.width() + 16, icon.height() + 28));
    m_thumbnails.clear();
    refresh();
}

QSize LibraryView::thumbnailSize() const
{
    return m_items->iconSize();
}
