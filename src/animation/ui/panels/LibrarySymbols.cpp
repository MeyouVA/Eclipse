// Eclipse - the Library: the scene's symbols, listed as a folder of their own, with their pictures and frames.
#include "animation/ui/panels/LibraryView.h"

#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSlider>
#include <QLineEdit>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"

void LibraryView::setDocument(Document* document)
{
    if (m_document == document)
        return;
    m_document = document;
    if (document)
        connect(document, &Document::changed, this, [this] {
            // Listed again only when what the symbols are changed (not with every stroke drawn).
            if (showingSymbols() && symbolSignature() != m_symbolSignature)
                refresh();
        });
    showFolders();
}

bool LibraryView::showingSymbols() const
{
    return m_folderList->currentItem() && m_folderList->currentItem()->data(Qt::UserRole).toString() == kSymbolsFolder;
}

int LibraryView::symbolIdOf(QString const& path)
{
    return path.startsWith(QLatin1String("symbol:")) ? path.mid(7).toInt() : 0;
}

void LibraryView::showSceneSymbols(int pick)
{
    if (!m_document)
        return;
    for (int row = 0; row < m_folderList->count(); ++row)
        if (m_folderList->item(row)->data(Qt::UserRole).toString() == kSymbolsFolder)
            m_folderList->setCurrentRow(row);
    m_symbolSignature.clear();
    refresh();
    if (pick > 0)
        setCurrentTemplate(QStringLiteral("symbol:%1").arg(pick));
}

QString LibraryView::symbolSignature() const
{
    QString signature;
    if (!m_document)
        return signature;
    for (Scene::Symbol const& symbol : m_document->scene().symbols)
        signature += QStringLiteral("%1|%2|%3|%4;").arg(symbol.id).arg(symbol.name).arg(symbol.frameCount).arg(symbol.layers.size());
    return signature;
}

void LibraryView::fillSymbols()
{
    QString const chosen = currentTemplate();
    QString const wanted = m_search->text().trimmed();
    m_symbolSignature = symbolSignature();
    {
        QSignalBlocker const block(m_items);
        m_items->clear();
        if (m_document) {
            Scene const& scene = m_document->scene();
            QSize const size(96 * m_thumbnailPercent / 100, 54 * m_thumbnailPercent / 100);
            bool const pictures = !m_suppressThumbnails;
            for (Scene::Symbol const& symbol : scene.symbols) {
                if (!wanted.isEmpty() && !symbol.name.contains(wanted, Qt::CaseInsensitive))
                    continue;
                QIcon icon;
                if (pictures) {
                    Scene const inner = scene.symbolScene(symbol);
                    icon = QIcon(QPixmap::fromImage(picture(inner, firstDrawnFrame(inner), size)));
                }
                QListWidgetItem* const item = new QListWidgetItem(icon, symbol.name, m_items);
                QString const path = QStringLiteral("symbol:%1").arg(symbol.id);
                item->setData(Qt::UserRole, path);
                item->setToolTip(tr("%1 - a symbol of the scene, %n frame(s); shown by %2 layer(s)", nullptr, symbol.frameCount)
                                     .arg(symbol.name).arg(scene.symbolReferences(symbol.id)));
                if (path == chosen)
                    m_items->setCurrentItem(item);
            }
        }
    }
    setItemsView(settings().value(QStringLiteral("library/itemsView"), 0).toInt());
    showTemplate();
}

bool LibraryView::showSymbol(QString const& path)
{
    int const id = symbolIdOf(path);
    if (!id)
        return false;
    Scene::Symbol const* const symbol = m_document ? m_document->scene().findSymbol(id) : nullptr;
    if (!symbol) {
        m_shown = Scene();
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Template not found."));
        m_about->setText(QString());
        m_scrub->setEnabled(false);
        return true;
    }
    m_shown = m_document->scene().symbolScene(*symbol);
    m_shownPath = path;
    {
        QSignalBlocker const block(m_scrub);
        m_scrub->setRange(0, m_shown.frameCount() - 1);
        m_scrub->setValue(firstDrawnFrame(m_shown));
    }
    m_scrub->setEnabled(m_shown.frameCount() > 1);
    m_about->setText(tr("%1   -   a symbol, %n frame(s)", nullptr, m_shown.frameCount()).arg(symbol->name)
                     + tr(", shown by %n layer(s)", nullptr, m_document->scene().symbolReferences(id)));
    showFrame(m_scrub->value());
    return true;
}
