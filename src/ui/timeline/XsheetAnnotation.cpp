// Eclipse - the Xsheet: annotation columns (notes, pen marks and pictures) and the Timesheet markers.
#include "ui/timeline/Xsheet.h"

#include <QBuffer>
#include <QImage>
#include <QPlainTextEdit>
#include <QScrollBar>

#include "model/document/Commands.h"

static bool isAnnotation(Scene const& scene, int layer)
{
    return layer >= 0 && layer < scene.layers.size() && scene.layers[layer].kind == Layer::Annotation;
}

void Xsheet::drawAnnotation(QVector<QPointF> const& points)
{
    if (points.isEmpty() || !isAnnotation(m_document->scene(), m_layer))
        return;
    AnnotationInk mark;
    mark.points = points;
    mark.colour = m_penColour;
    mark.width = m_penWidth;
    m_document->editLayers({m_layer}, tr("Draw Annotation"), [mark](Layer& l) { l.ink.push_back(mark); });
}

bool Xsheet::eraseAnnotation(bool ink, bool notes, bool everything)
{
    Scene const& scene = m_document->scene();
    if (!isAnnotation(scene, m_layer))
        return false;
    Layer const& layer = scene.layers[m_layer];
    if (everything) {
        if (layer.ink.isEmpty() && layer.notes.isEmpty() && layer.pictures.isEmpty())
            return false;
        m_document->editLayers({m_layer}, tr("Erase All"), [](Layer& l) {
            l.ink.clear();
            l.notes.clear();
            l.pictures.clear();
        });
        return true;
    }
    int const first = selectionFirstFrame(), last = selectionLastFrame();
    bool changed = false;
    Layer after = layer;
    if (ink) {
        after.ink.erase(std::remove_if(after.ink.begin(), after.ink.end(), [first, last](AnnotationInk const& mark) {
            for (QPointF const& p : mark.points)
                if (p.y() >= first && p.y() < last + 1)
                    return true;
            return false;
        }), after.ink.end());
        after.pictures.erase(std::remove_if(after.pictures.begin(), after.pictures.end(), [first, last](AnnotationPicture const& picture) {
            return picture.frame <= last && picture.frame + picture.frames > first;
        }), after.pictures.end());
        changed = after.ink.size() != layer.ink.size() || after.pictures.size() != layer.pictures.size();
    }
    if (notes) {
        for (auto it = after.notes.begin(); it != after.notes.end();)
            it = it.key() >= first && it.key() <= last ? after.notes.erase(it) : std::next(it);
        changed = changed || after.notes.size() != layer.notes.size();
    }
    if (!changed)
        return false;
    QVector<AnnotationInk> const keptInk = after.ink;
    QVector<AnnotationPicture> const keptPictures = after.pictures;
    QMap<int, QString> const keptNotes = after.notes;
    m_document->editLayers({m_layer}, ink ? tr("Remove Annotation Images") : tr("Remove Annotation Texts"), [=](Layer& l) {
        l.ink = keptInk;
        l.pictures = keptPictures;
        l.notes = keptNotes;
    });
    return true;
}

bool Xsheet::addAnnotationPicture(QImage const& image)
{
    if (image.isNull() || !isAnnotation(m_document->scene(), m_layer))
        return false;
    // Kept no bigger than it needs to be to fill a column.
    QImage const kept = image.width() > 600 ? image.scaledToWidth(600, Qt::SmoothTransformation) : image;
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    kept.save(&buffer, "PNG");
    AnnotationPicture picture;
    picture.frame = selectionFirstFrame();
    picture.frames = std::max(4, selectionLastFrame() - selectionFirstFrame() + 1);
    picture.png = png;
    m_document->editLayers({m_layer}, tr("Add Annotation Picture"), [picture](Layer& l) { l.pictures.push_back(picture); });
    return true;
}

void Xsheet::beginNote()
{
    endNote(true);
    Scene const& scene = m_document->scene();
    if (!isAnnotation(scene, m_layer) || m_column < 0)
        return;
    m_noteLayer = m_layer;
    m_noteFrame = m_frame;
    m_note = new QPlainTextEdit(viewport());
    m_note->setObjectName(QStringLiteral("XsheetNote"));
    m_note->setPlainText(scene.layers[m_layer].notes.value(m_frame));
    QRect const cell = columnCellRect(m_column, m_frame);
    m_note->setGeometry(cell.x(), cell.y(), std::max(cell.width(), 120), std::max(4 * rowHeight(), 60));
    m_note->installEventFilter(this);
    m_note->show();
    m_note->setFocus();
    m_note->moveCursor(QTextCursor::End);
}

void Xsheet::endNote(bool keep)
{
    if (!m_note)
        return;
    QPlainTextEdit* const note = m_note;
    m_note = nullptr;
    QString const text = note->toPlainText().trimmed();
    note->removeEventFilter(this);
    note->hide();
    note->setObjectName(QString());
    note->deleteLater();
    int const layer = m_noteLayer, frame = m_noteFrame;
    m_noteLayer = m_noteFrame = -1;
    Scene const& scene = m_document->scene();
    if (!keep || !isAnnotation(scene, layer) || scene.layers[layer].notes.value(frame) == text)
        return;
    m_document->editLayers({layer}, tr("Set Annotation Text"), [frame, text](Layer& l) {
        if (text.isEmpty())
            l.notes.remove(frame);
        else
            l.notes.insert(frame, text);
    });
}

// ---- Timesheet markers ----

int Xsheet::addSheetMarker()
{
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size())
        return -1;
    Scene::SheetMarker m;
    m.layer = scene.layers[m_layer].id;
    m.frame = selectionFirstFrame();
    m.length = selectionLastFrame() - selectionFirstFrame() + 1;
    m_document->editScene(tr("Add Timesheet Marker"), [m](Scene& s) { s.sheetMarkers.push_back(m); });
    return int(m_document->scene().sheetMarkers.size()) - 1;
}

int Xsheet::sheetMarkerAt(QPoint pos) const
{
    Scene const& scene = m_document->scene();
    int const row = frameAt(pos.y());
    for (int i = int(scene.sheetMarkers.size()) - 1; i >= 0; --i) {
        Scene::SheetMarker const& m = scene.sheetMarkers[i];
        int const column = columnOfLayer(scene.layerIndex(m.layer));
        if (column < 0 || row < m.frame || row >= m.frame + m.length)
            continue;
        QRect const cell = columnCellRect(column, row);
        if (pos.x() >= cell.x() && pos.x() < cell.x() + 10)
            return i;
    }
    return -1;
}

void Xsheet::setSheetMarkerColour(int marker, int colour)
{
    Scene const& scene = m_document->scene();
    if (marker < 0 || marker >= scene.sheetMarkers.size() || colour < 0 || colour > 2 || scene.sheetMarkers[marker].colour == colour)
        return;
    m_document->editScene(tr("Change Marker Colour"), [marker, colour](Scene& s) { s.sheetMarkers[marker].colour = colour; });
}

void Xsheet::removeSheetMarker(int marker)
{
    if (marker < 0 || marker >= m_document->scene().sheetMarkers.size())
        return;
    m_document->editScene(tr("Delete Timesheet Marker"), [marker](Scene& s) { s.sheetMarkers.removeAt(marker); });
}

void Xsheet::moveSheetMarker(int marker, int frame, int length)
{
    Scene const& scene = m_document->scene();
    if (marker < 0 || marker >= scene.sheetMarkers.size())
        return;
    Scene::SheetMarker const& was = scene.sheetMarkers[marker];
    frame = std::clamp(frame, 0, scene.frameCount() - 1);
    length = std::clamp(length, 1, scene.frameCount() - frame);
    if (frame == was.frame && length == was.length)
        return;
    m_document->editScene(length != was.length ? tr("Resize Timesheet Marker") : tr("Move Timesheet Marker"), [marker, frame, length](Scene& s) {
        s.sheetMarkers[marker].frame = frame;
        s.sheetMarkers[marker].length = length;
    });
}
