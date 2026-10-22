// Eclipse - the Xsheet printed: the columns of the drawings, the sound and the notes, a page for every so many frames.
#include "ui/timeline/Xsheet.h"

#include <QPainter>

#include "model/scene/Scene.h"

int Xsheet::printPages(QPainter& painter, QRectF const& page, Scene const& scene, PrintOptions const& options, std::function<bool()> const& newPage)
{
    // What goes on paper: the columns shown, front layer first.
    QVector<int> columns;
    for (int l = int(scene.layers.size()) - 1; l >= 0; --l) {
        Layer const& layer = scene.layers[l];
        if (!layer.columnHidden && (layer.holdsDrawings() || layer.kind == Layer::Sound || layer.kind == Layer::Annotation))
            columns.push_back(l);
    }
    int const frames = scene.frameCount();
    int const perPage = options.framesPerPage > 0 ? options.framesPerPage : 80;
    double const titleHeight = page.height() * 0.05, headerHeight = page.height() * 0.035;
    double const rowHeight = (page.height() - titleHeight - headerHeight) / perPage;
    double const numbersWidth = page.width() * 0.07;
    double const narrowest = page.width() / 12.0;
    int const perSheet = std::max(1, int((page.width() - numbersWidth) / narrowest));
    int const across = std::max(1, (int(columns.size()) + perSheet - 1) / perSheet);
    int const down = std::max(1, (frames + perPage - 1) / perPage);
    int const total = across * down;
    QFont font = painter.font();
    font.setPixelSize(std::max(4, int(rowHeight * 0.62)));
    QFont title = font;
    title.setPixelSize(std::max(6, int(titleHeight * 0.45)));
    title.setBold(true);
    QPen const thin(QColor(150, 150, 150), std::max(1.0, rowHeight / 30.0));
    QPen const ink(Qt::black, std::max(1.0, rowHeight / 14.0));
    int printed = 0;
    for (int a = 0; a < across; ++a) {
        QVector<int> const here = columns.mid(a * perSheet, perSheet);
        double const columnWidth = here.isEmpty() ? page.width() - numbersWidth : (page.width() - numbersWidth) / here.size();
        double const left = options.numbersOnRight ? page.left() : page.left() + numbersWidth;
        double const numbersLeft = options.numbersOnRight ? page.left() + columnWidth * here.size() : page.left();
        for (int d = 0; d < down; ++d) {
            if (printed > 0 && newPage && !newPage())
                return printed;
            ++printed;
            int const first = d * perPage, last = std::min(frames, first + perPage) - 1;
            double const top = page.top() + titleHeight + headerHeight;
            painter.setFont(title);
            painter.setPen(Qt::black);
            painter.drawText(QRectF(page.left(), page.top(), page.width(), titleHeight), Qt::AlignVCenter | Qt::AlignLeft,
                             options.title.isEmpty() ? tr("Xsheet") : options.title);
            painter.drawText(QRectF(page.left(), page.top(), page.width(), titleHeight), Qt::AlignVCenter | Qt::AlignRight,
                             tr("Page %1 of %2").arg(printed).arg(total));
            painter.setFont(font);
            // The names over the columns.
            for (int c = 0; c < here.size(); ++c) {
                QRectF const box(left + c * columnWidth, page.top() + titleHeight, columnWidth, headerHeight);
                painter.setPen(ink);
                painter.drawRect(box);
                painter.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter,
                                 painter.fontMetrics().elidedText(scene.layers[here[c]].name, Qt::ElideRight, int(box.width() - 8)));
            }
            for (int f = first; f <= last; ++f) {
                double const y = top + (f - first) * rowHeight;
                painter.setPen((f + 1) % 8 == 0 ? ink : thin);
                painter.drawLine(QPointF(page.left(), y + rowHeight), QPointF(page.left() + numbersWidth + columnWidth * here.size(), y + rowHeight));
                painter.setPen(Qt::black);
                painter.drawText(QRectF(numbersLeft, y, numbersWidth - 4, rowHeight), Qt::AlignVCenter | Qt::AlignRight, QString::number(f + 1));
                for (int c = 0; c < here.size(); ++c) {
                    Layer const& layer = scene.layers[here[c]];
                    QRectF const cell(left + c * columnWidth, y, columnWidth, rowHeight);
                    if (layer.holdsDrawings()) {
                        int const drawing = layer.drawingAt(f);
                        if (drawing == Layer::kNoDrawing)
                            continue;
                        if (layer.isKey(f) || f == first)
                            painter.drawText(cell.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, layer.drawingLabel(drawing));
                        else {
                            painter.setPen(ink);
                            painter.drawLine(QPointF(cell.left() + columnWidth * 0.3, cell.top()), QPointF(cell.left() + columnWidth * 0.3, cell.bottom()));
                            painter.setPen(Qt::black);
                        }
                    } else if (layer.kind == Layer::Sound && !layer.mouthAt(f).isNull()) {
                        painter.drawText(cell, Qt::AlignCenter, QString(layer.mouthAt(f)));
                    } else if (layer.kind == Layer::Annotation && layer.notes.contains(f)) {
                        auto const next = layer.notes.upperBound(f);
                        int const end = std::min(last + 1, next == layer.notes.constEnd() ? frames : next.key());
                        painter.drawText(QRectF(cell.left() + 3, cell.top(), columnWidth - 6, (end - f) * rowHeight), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                                         layer.notes.value(f));
                    }
                }
            }
            // The lines down the sheet.
            painter.setPen(ink);
            double const bottom = top + (last - first + 1) * rowHeight;
            for (int c = 0; c <= here.size(); ++c)
                painter.drawLine(QPointF(left + c * columnWidth, top), QPointF(left + c * columnWidth, bottom));
            painter.drawLine(QPointF(numbersLeft, top), QPointF(numbersLeft, bottom));
            painter.drawLine(QPointF(numbersLeft + numbersWidth, top), QPointF(numbersLeft + numbersWidth, bottom));
        }
    }
    return printed;
}
