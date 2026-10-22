// Eclipse - the Xsheet: the exposure sheet as a table, one column per layer (front layer on
// the left) and one row per frame.  A cell shows the number of its drawing where an
// exposure starts and a line while it is held.
#pragma once

#include "EclipseExport.h"

#include <QAbstractScrollArea>

#include <algorithm>

class Document;
class QLineEdit;

class ECLIPSE_UI_API Xsheet : public QAbstractScrollArea {
    Q_OBJECT

public:
    // The column a layer is shown in (left to right), and the layer of a column; -1: none.
    int columnOfLayer(int layer) const;
    int layerOfColumn(int column) const;
    // Preferences: how wide a column is (pixels).
    void setColumnWidth(int pixels);
    void setCentreOnFrame(bool on) { m_centreOnFrame = on; }
    bool centreOnFrame() const { return m_centreOnFrame; }
    void setDefaultHold(int frames) { m_defaultHold = std::clamp(frames, 1, 100); }
    int defaultHold() const { return m_defaultHold; }
    int columnWidth() const { return m_columnWidth; }
    explicit Xsheet(Document* document, QWidget* parent = nullptr);

public slots:
    // Follows the timeline; does not signal back.
    void setCurrentCell(int layer, int frame);
    void deleteSelection();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void deselectAll() { extendSelection(m_frame); }

public:
    // The frames selected: from the current cell down or up its column.
    int selectionFirstFrame() const { return m_extent < 0 ? m_frame : std::min(m_frame, m_extent); }
    int selectionLastFrame() const { return m_extent < 0 ? m_frame : std::max(m_frame, m_extent); }
    void extendSelection(int frame, int layer = -1);
    // The layers of the columns selected, left to right.
    QVector<int> selectedLayers() const;
    bool hasSelection() const { return m_extent >= 0 || m_extentLayer >= 0; }
    // Where a cell is, in the coordinates of viewport().
    QRect cellRect(int layer, int frame) const;
    void beginTyping();
    void endTyping(bool keep);

signals:
    void cellPicked(int layer, int frame);
    // Paste, with artwork copied last (see Canvas::artworkCopiedLast).
    void pasteArtwork();
    void contextMenuRequested(QPoint const& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QSize sizeHint() const override { return QSize(320, 400); }

private:
    void updateScrollBars();
    void pick(QPoint pos);

    static constexpr int kRowHeaderWidth = 44;
    static constexpr int kHeaderHeight = 26;
    static constexpr int kColumnWidth = 78;
    int m_columnWidth = kColumnWidth;
    bool m_centreOnFrame = false;
    int m_defaultHold = 1;
    static constexpr int kRowHeight = 18;

    Document* m_document;
    QLineEdit* m_entry = nullptr;
    int m_layer = 0;
    int m_frame = 0;
    int m_extent = -1;         // the far end of the frames selected; -1: only the current cell
    int m_extentLayer = -1;    // the layer of the far column selected; -1: only the current column
    bool m_selecting = false;
};
