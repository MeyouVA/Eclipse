#include "ui/timeline/Xsheet.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"

Xsheet::Xsheet(Document* document, QWidget* parent) : QAbstractScrollArea(parent), m_document(document)
{
    setFocusPolicy(Qt::ClickFocus);
    viewport()->setAutoFillBackground(false);
    connect(m_document, &Document::changed, this, [this] {
        if (m_extent >= m_document->scene().frameCount())
            m_extent = -1;
        updateScrollBars();
        viewport()->update();
    });
    updateScrollBars();
}

int Xsheet::columnOfLayer(int layer) const
{
    return int(m_document->scene().layers.size()) - 1 - layer;
}

int Xsheet::layerOfColumn(int column) const
{
    return int(m_document->scene().layers.size()) - 1 - column;
}

void Xsheet::setCurrentCell(int layer, int frame)
{
    if (layer == m_layer && frame == m_frame)
        return;
    m_layer = layer;
    m_frame = frame;
    m_extent = -1;      // the frames selected are counted from the current cell
    m_extentLayer = -1;

    // Keep the current cell in view.
    int const y = frame * kRowHeight;
    int const viewHeight = viewport()->height() - kHeaderHeight;
    QScrollBar* const v = verticalScrollBar();
    if (y < v->value())
        v->setValue(y);
    else if (y + kRowHeight > v->value() + viewHeight)
        v->setValue(y + kRowHeight - viewHeight);
    int const x = columnOfLayer(layer) * kColumnWidth;
    int const viewWidth = viewport()->width() - kRowHeaderWidth;
    QScrollBar* const h = horizontalScrollBar();
    if (x < h->value())
        h->setValue(x);
    else if (x + kColumnWidth > h->value() + viewWidth)
        h->setValue(x + kColumnWidth - viewWidth);

    viewport()->update();
}

void Xsheet::deleteSelection()
{
    if (!hasSelection())
        m_document->clearCell(m_layer, m_frame);
    else
        m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Clear Cells"));
}

void Xsheet::updateScrollBars()
{
    Scene const& scene = m_document->scene();
    int const contentWidth = int(scene.layers.size()) * kColumnWidth;
    int const contentHeight = scene.frameCount() * kRowHeight;
    int const viewWidth = viewport()->width() - kRowHeaderWidth;
    int const viewHeight = viewport()->height() - kHeaderHeight;
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth - viewWidth));
    horizontalScrollBar()->setPageStep(std::max(1, viewWidth));
    horizontalScrollBar()->setSingleStep(kColumnWidth);
    verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewHeight));
    verticalScrollBar()->setPageStep(std::max(1, viewHeight));
    verticalScrollBar()->setSingleStep(kRowHeight);
}

void Xsheet::resizeEvent(QResizeEvent*)
{
    updateScrollBars();
}

void Xsheet::paintEvent(QPaintEvent*)
{
    Scene const& scene = m_document->scene();
    QPainter p(viewport());
    int const w = viewport()->width();
    int const h = viewport()->height();
    int const sx = horizontalScrollBar()->value();
    int const sy = verticalScrollBar()->value();

    QColor const bg(72, 72, 72), header(58, 58, 58), line(48, 48, 48), text(210, 210, 210);
    QColor const paper(88, 88, 88), paperAlt(83, 83, 83), exposed(122, 122, 122), pegColumn(74, 74, 74);
    QColor const currentRow(100, 100, 100), keyColor(255, 190, 70), accent(224, 36, 36);
    p.fillRect(0, 0, w, h, bg);

    int const firstRow = sy / kRowHeight;
    int const lastRow = std::min(scene.frameCount() - 1, (sy + h - kHeaderHeight) / kRowHeight);

    // ---- cells ----
    p.save();
    p.setClipRect(kRowHeaderWidth, kHeaderHeight, w - kRowHeaderWidth, h - kHeaderHeight);
    for (int column = 0; column < scene.layers.size(); ++column) {
        int const x = kRowHeaderWidth + column * kColumnWidth - sx;
        if (x + kColumnWidth < kRowHeaderWidth || x > w)
            continue;
        int const layerIndex = layerOfColumn(column);
        Layer const& layer = scene.layers[layerIndex];
        for (int f = firstRow; f <= lastRow; ++f) {
            int const y = kHeaderHeight + f * kRowHeight - sy;
            QRect const cell(x, y, kColumnWidth, kRowHeight);
            int const drawing = layer.drawingAt(f);
            QColor fill = !layer.holdsDrawings() ? pegColumn : (f % 2 ? paperAlt : paper);
            if (f == m_frame)
                fill = currentRow;
            if (drawing != Layer::kNoDrawing)
                fill = f == m_frame ? exposed.lighter(125) : exposed;
            p.fillRect(cell, fill);
            if (drawing != Layer::kNoDrawing) {
                p.setPen(text);
                if (layer.isKey(f)) {
                    // The number of the drawing among those of its layer.
                    p.drawText(cell.adjusted(6, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, layer.drawingLabel(drawing));
                    // Its mark: K for a key drawing, B for a breakdown, I for an in-between.
                    int const mark = layer.drawings[drawing].mark;
                    if (mark != Drawing::NoMark) {
                        p.setPen(Drawing::markColour(mark));
                        p.drawText(cell.adjusted(6, 0, -22, 0), Qt::AlignVCenter | Qt::AlignRight,
                                   mark == Drawing::KeyMark ? QStringLiteral("K") : mark == Drawing::BreakdownMark ? QStringLiteral("B") : QStringLiteral("I"));
                        p.setPen(text);
                    }
                } else if (layer.morphing(f)) {
                    // In a morphing: the drawing here is made from the two either side of it.
                    p.setPen(QPen(QColor(170, 215, 120), 2));
                    p.drawLine(x + 12, y, x + 12, y + kRowHeight - 1);
                    p.setPen(QPen(QColor(170, 215, 120), 1));
                    p.drawLine(x + 8, y + kRowHeight / 2 - 2, x + 12, y + kRowHeight / 2 + 2);
                    p.drawLine(x + 16, y + kRowHeight / 2 - 2, x + 12, y + kRowHeight / 2 + 2);
                } else {
                    p.drawLine(x + 12, y, x + 12, y + kRowHeight - 1);  // held
                }
            }
            if (layer.keyIndex(f) >= 0) {
                double const kx = x + kColumnWidth - 9.0, ky = y + kRowHeight / 2.0;
                QPolygonF diamond;
                diamond << QPointF(kx, ky - 4) << QPointF(kx + 4, ky) << QPointF(kx, ky + 4) << QPointF(kx - 4, ky);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(keyColor);
                p.drawPolygon(diamond);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            // A sound column: the shape of the mouth on the frame, when it has lip-sync.
            if (layer.kind == Layer::Sound && !layer.mouthAt(f).isNull()) {
                QChar const shape = layer.mouthAt(f);
                p.setPen(shape == QLatin1Char('X') ? QColor(140, 140, 140) : (f == 0 || layer.mouthAt(f - 1) != shape) ? keyColor : text);
                p.drawText(cell, Qt::AlignCenter, QString(shape));
            }
            p.setPen(line);
            p.drawLine(x, y + kRowHeight - 1, x + kColumnWidth - 1, y + kRowHeight - 1);
        }
        p.setPen(line);
        p.drawLine(x + kColumnWidth - 1, kHeaderHeight, x + kColumnWidth - 1, h);
    }
    // the frames selected
    if (hasSelection()) {
        QVector<int> const across = selectedLayers();
        int const left = std::min(columnOfLayer(across.first()), columnOfLayer(across.last()));
        QRect const block(kRowHeaderWidth + left * kColumnWidth - sx, kHeaderHeight + selectionFirstFrame() * kRowHeight - sy,
                          int(across.size()) * kColumnWidth, (selectionLastFrame() - selectionFirstFrame() + 1) * kRowHeight);
        p.fillRect(block, QColor(42, 155, 176, 95));
        p.setPen(QPen(QColor(120, 220, 235), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(block.adjusted(0, 0, -2, -1));
    }
    // the current cell
    {
        int const x = kRowHeaderWidth + columnOfLayer(m_layer) * kColumnWidth - sx;
        int const y = kHeaderHeight + m_frame * kRowHeight - sy;
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x, y, kColumnWidth - 2, kRowHeight - 2);
    }
    p.restore();

    // ---- frame numbers ----
    p.fillRect(0, 0, kRowHeaderWidth, h, header);
    p.save();
    p.setClipRect(0, kHeaderHeight, kRowHeaderWidth, h - kHeaderHeight);
    for (int f = firstRow; f <= lastRow; ++f) {
        int const y = kHeaderHeight + f * kRowHeight - sy;
        if (f == m_frame)
            p.fillRect(0, y, kRowHeaderWidth, kRowHeight, QColor(accent.red(), accent.green(), accent.blue(), 150));
        p.setPen(text);
        p.drawText(QRect(0, y, kRowHeaderWidth - 6, kRowHeight), Qt::AlignVCenter | Qt::AlignRight, QString::number(f + 1));
        p.setPen(line);
        p.drawLine(0, y + kRowHeight - 1, kRowHeaderWidth, y + kRowHeight - 1);
    }
    p.restore();

    // ---- column headers ----
    p.fillRect(kRowHeaderWidth, 0, w - kRowHeaderWidth, kHeaderHeight, header);
    p.save();
    p.setClipRect(kRowHeaderWidth, 0, w - kRowHeaderWidth, kHeaderHeight);
    for (int column = 0; column < scene.layers.size(); ++column) {
        int const x = kRowHeaderWidth + column * kColumnWidth - sx;
        int const layerIndex = layerOfColumn(column);
        Layer const& layer = scene.layers[layerIndex];
        if (layerIndex == m_layer)
            p.fillRect(x, 0, kColumnWidth, kHeaderHeight, currentRow);
        p.setPen(layer.kind == Layer::Peg ? keyColor : layer.kind == Layer::Camera ? QColor(111, 168, 232)
                                                 : (layer.visible ? text : QColor(130, 130, 130)));
        p.drawText(QRect(x + 4, 0, kColumnWidth - 8, kHeaderHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(layer.name, Qt::ElideRight, kColumnWidth - 8));
        p.setPen(line);
        p.drawLine(x + kColumnWidth - 1, 0, x + kColumnWidth - 1, kHeaderHeight);
    }
    p.restore();
    p.fillRect(0, 0, kRowHeaderWidth, kHeaderHeight, header);
    p.setPen(line);
    p.drawLine(kRowHeaderWidth - 1, 0, kRowHeaderWidth - 1, h);
    p.drawLine(0, kHeaderHeight - 1, w, kHeaderHeight - 1);
}

void Xsheet::pick(QPoint pos)
{
    Scene const& scene = m_document->scene();
    int const column = (pos.x() - kRowHeaderWidth + horizontalScrollBar()->value()) / kColumnWidth;
    int const row = (pos.y() - kHeaderHeight + verticalScrollBar()->value()) / kRowHeight;
    bool const inColumns = pos.x() >= kRowHeaderWidth && column >= 0 && column < scene.layers.size();
    bool const inRows = pos.y() >= kHeaderHeight && row >= 0 && row < scene.frameCount();
    // A click on a header changes only the layer, on a frame number only the frame.
    int const layer = inColumns ? layerOfColumn(column) : m_layer;
    int const frame = inRows ? row : m_frame;
    if (layer != m_layer || frame != m_frame)
        emit cellPicked(layer, frame);
}

QRect Xsheet::cellRect(int layer, int frame) const
{
    return QRect(kRowHeaderWidth + columnOfLayer(layer) * kColumnWidth - horizontalScrollBar()->value(),
                 kHeaderHeight + frame * kRowHeight - verticalScrollBar()->value(), kColumnWidth, kRowHeight);
}

void Xsheet::extendSelection(int frame, int layer)
{
    frame = std::clamp(frame, 0, m_document->scene().frameCount() - 1);
    int const extent = frame == m_frame ? -1 : frame;
    int const extentLayer = layer < 0 || layer >= m_document->scene().layers.size() || layer == m_layer ? -1 : layer;
    if (extent == m_extent && extentLayer == m_extentLayer)
        return;
    m_extent = extent;
    m_extentLayer = extentLayer;
    viewport()->update();
}

QVector<int> Xsheet::selectedLayers() const
{
    int const here = columnOfLayer(m_layer);
    int const there = m_extentLayer < 0 || m_extentLayer >= m_document->scene().layers.size() ? here : columnOfLayer(m_extentLayer);
    QVector<int> layers;
    for (int column = std::min(here, there); column <= std::max(here, there); ++column)
        layers.push_back(layerOfColumn(column));
    return layers;
}

void Xsheet::mousePressEvent(QMouseEvent* event)
{
    int const row = (event->pos().y() - kHeaderHeight + verticalScrollBar()->value()) / kRowHeight;
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier) && event->pos().y() >= kHeaderHeight) {
        int const column = (event->pos().x() - kRowHeaderWidth + horizontalScrollBar()->value()) / kColumnWidth;
        bool const inColumns = event->pos().x() >= kRowHeaderWidth && column >= 0 && column < m_document->scene().layers.size();
        extendSelection(row, inColumns ? layerOfColumn(column) : -1);       // Shift: from the current cell to this one
        return;
    }
    if (event->button() == Qt::RightButton && hasSelection() && row >= selectionFirstFrame() && row <= selectionLastFrame())
        return;                     // the menu, for the frames selected
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        extendSelection(m_frame);
        pick(event->pos());
        m_selecting = event->button() == Qt::LeftButton && event->pos().x() >= kRowHeaderWidth && event->pos().y() >= kHeaderHeight;
    }
}

void Xsheet::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if (m_selecting) {              // down the column, and across to other columns: a block of cells
        int const count = int(m_document->scene().layers.size());
        int const column = std::clamp((std::max(event->pos().x(), int(kRowHeaderWidth)) - kRowHeaderWidth + horizontalScrollBar()->value()) / kColumnWidth, 0, count - 1);
        extendSelection((std::max(event->pos().y(), int(kHeaderHeight)) - kHeaderHeight + verticalScrollBar()->value()) / kRowHeight, layerOfColumn(column));
    }
    else
        pick(event->pos());
}

void Xsheet::mouseReleaseEvent(QMouseEvent*)
{
    m_selecting = false;
}

void Xsheet::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->pos().x() >= kRowHeaderWidth && event->pos().y() >= kHeaderHeight) {
        pick(event->pos());
        beginTyping();
    }
}

void Xsheet::beginTyping()
{
    endTyping(false);
    Scene const& scene = m_document->scene();
    if (m_layer < 0 || m_layer >= scene.layers.size() || m_frame < 0 || m_frame >= scene.frameCount() || !scene.layers[m_layer].holdsDrawings())
        return;
    m_entry = new QLineEdit(viewport());
    m_entry->setObjectName(QStringLiteral("XsheetEntry"));
    m_entry->setFrame(false);
    m_entry->setText(scene.layers[m_layer].drawingLabel(scene.layers[m_layer].drawingAt(m_frame)));
    m_entry->setGeometry(cellRect(m_layer, m_frame).adjusted(0, 0, -1, -1));
    m_entry->selectAll();
    m_entry->installEventFilter(this);
    connect(m_entry, &QLineEdit::returnPressed, this, [this] { endTyping(true); });
    m_entry->show();
    m_entry->setFocus();
}

void Xsheet::endTyping(bool keep)
{
    if (!m_entry)
        return;
    QLineEdit* const entry = m_entry;
    m_entry = nullptr;
    QString const typed = entry->text();
    entry->removeEventFilter(this);
    entry->hide();
    entry->setObjectName(QString());     // on its way out: not the entry to look for any more
    entry->deleteLater();
    if (!keep) {
        setFocus();
        return;
    }
    int const layer = m_layer, frame = m_frame;
    m_document->exposeDrawing(layer, frame, typed);
    // On to the frame below, to type the next.
    if (frame + 1 < m_document->scene().frameCount()) {
        setCurrentCell(layer, frame + 1);
        emit cellPicked(layer, frame + 1);
        beginTyping();
    } else {
        setFocus();
    }
}

bool Xsheet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_entry && m_entry) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            endTyping(false);
            return true;
        }
        if (event->type() == QEvent::FocusOut) {
            endTyping(false);
            return false;
        }
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void Xsheet::copy()
{
    Timeline::clipboard() = m_document->copyCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame());
}

void Xsheet::cut()
{
    copy();
    m_document->clearCells(selectedLayers(), selectionFirstFrame(), selectionLastFrame(), tr("Cut Cells"));
}

void Xsheet::paste()
{
    // From the left-most column selected, onto as many columns to the right as there are.
    QVector<int> layers;
    for (int column = columnOfLayer(selectedLayers().first()); column < m_document->scene().layers.size(); ++column)
        layers.push_back(layerOfColumn(column));
    m_document->pasteCells(layers, selectionFirstFrame(), Timeline::clipboard());
}

void Xsheet::selectAll()
{
    emit cellPicked(m_layer, 0);
    extendSelection(m_document->scene().frameCount() - 1);
}

void Xsheet::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
}
