// Eclipse - the Xsheet: the exposure sheet as a table, a column per layer (front layer on the left) and per animated value, a row per frame.
#pragma once

#include "EclipseExport.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QPointF>
#include <QSet>

#include <algorithm>
#include <functional>

class Document;
class QLineEdit;
class QPainter;
class QPlainTextEdit;
class Scene;
struct Layer;

class ECLIPSE_UI_API Xsheet : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit Xsheet(Document* document, QWidget* parent = nullptr);

    // A column: a layer's own (function -1), or one of the values of its pose (a Timeline::Parameter) when they are animated.
    struct Column {
        int layer = -1;
        int function = -1;
        int x = 0;              // from the left of the first column
        int width = 0;
    };
    QVector<Column> const& columns() const { return m_columns; }
    // The column a layer's own column is (left to right), and the layer of a column; -1: none (hidden, or not there).
    int columnOfLayer(int layer) const;
    int layerOfColumn(int column) const;
    int functionOfColumn(int column) const { return column >= 0 && column < m_columns.size() ? m_columns[column].function : -1; }
    int currentColumn() const { return m_column; }
    void setCurrentColumn(int column);

    // Preferences: how wide a column is by default (pixels).
    void setColumnWidth(int pixels);
    int columnWidth() const { return m_columnWidth; }
    void setCentreOnFrame(bool on) { m_centreOnFrame = on; }
    bool centreOnFrame() const { return m_centreOnFrame; }
    void setDefaultHold(int frames) { m_defaultHold = std::clamp(frames, 1, 100); }
    int defaultHold() const { return m_defaultHold; }
    // How far the rows can be zoomed (a level is two pixels of row height).
    void setZoomRange(int lowest, int highest);
    int zoomLevel() const { return m_zoom; }
    void setZoomLevel(int level);
    int rowHeight() const { return 2 * m_zoom; }

    // What is shown.
    enum Show { ShowDrawings = 1, ShowFunctions = 2, ShowPaths = 4, ShowRotations = 8, ShowAnnotations = 16, ShowSounds = 32, ShowOthers = 64,
                ShowEverything = 127 };
    int shown() const { return m_show; }
    void setShown(int what);
    enum HeldLook { HeldLine, HeldValue };
    int heldLook() const { return m_heldLook; }
    void setHeldLook(int look);
    // A held exposure this many frames long or shorter has no line (0: every one has).
    void setLineThreshold(int frames);
    int lineThreshold() const { return m_lineThreshold; }
    bool feetAndFrames() const { return m_feet; }
    void setFeetAndFrames(bool on);
    bool thumbnails() const { return m_thumbnails; }
    void setThumbnails(bool on);
    bool minimalHeaders() const { return m_minimal; }
    void setMinimalHeaders(bool on);
    bool grouping() const { return m_grouping; }
    void setGrouping(bool on);
    bool gridShown() const { return m_grid; }
    void setGridShown(bool on);
    // Show Selection: only the columns of these layers (the ones picked in the other views when it was turned on).
    bool selectionOnly() const { return m_selectionOnly; }
    void setSelectionOnly(bool on, QVector<int> const& layers = {});
    void setSelectionLayers(QVector<int> const& layers);
    bool gesturalDrag() const { return m_gestural; }
    void setGesturalDrag(bool on) { m_gestural = on; }
    // Typing a drawing into a cell puts it in before what is there (pushing the rest down) rather than over it.
    bool insertMode() const { return m_insert; }
    void setInsertMode(bool on) { m_insert = on; viewport()->update(); }
    void setOnion(bool on, int previous, int next);
    void setLightTable(bool on);
    void setAnnotationFrameMarks(bool on);
    void setAnnotationSmoothing(double hardness);

    // Annotation columns: the pen.
    bool annotationDrawing() const { return m_annotationDrawing; }
    void setAnnotationDrawing(bool on) { m_annotationDrawing = on; }
    QColor penColour() const { return m_penColour; }
    void setPenColour(QColor const& colour) { m_penColour = colour; }
    double penWidth() const { return m_penWidth; }
    void setPenWidth(double width) { m_penWidth = std::clamp(width, 0.5, 20.0); }
    // Erases in the current annotation column: everything, or the pen marks / the notes of the frames selected.
    bool eraseAnnotation(bool ink, bool notes, bool everything);
    // Puts a picture into the current annotation column from the current frame over the frames selected (at least four).
    bool addAnnotationPicture(QImage const& image);
    // Draws a line in the current annotation column (x across 0..1, y in frames) as the pen would.
    void drawAnnotation(QVector<QPointF> const& points);
    // Opens the box of the current annotation cell's note for typing (Ctrl+Enter or a click elsewhere keeps it).
    void beginNote();
    void endNote(bool keep);

    // The Timesheet markers: one over the frames selected in the current column; the one under a point; their colours.
    int addSheetMarker();
    int sheetMarkerAt(QPoint pos) const;
    void setSheetMarkerColour(int marker, int colour);
    void removeSheetMarker(int marker);
    void moveSheetMarker(int marker, int frame, int length);
    int menuMarker() const { return m_menuMarker; }

    // View.
    void zoomIn() { setZoomLevel(m_zoom + 1); }
    void zoomOut() { setZoomLevel(m_zoom - 1); }
    void resetZoom() { setZoomLevel(kDefaultZoom); }
    void resetPan();
    void resetView() { resetZoom(); resetPan(); }
    // As small as it goes or as big as needed for every frame to fit the height.
    void zoomExtents();
    // Folds the columns selected (or all) to narrow strips, and opens them out again.
    void collapseColumns(bool collapse, bool all);

    // Printing: the sheet over as many pages as it needs, the frame numbers on the left or right.
    struct PrintOptions {
        bool numbersOnRight = false;
        int framesPerPage = 0;        // 0: as many as fit at a readable size
        QString title;
    };
    static int printPages(QPainter& painter, QRectF const& page, Scene const& scene, PrintOptions const& options, std::function<bool()> const& newPage);

    // What the status strip under the sheet says.
    QString statusText() const;

public slots:
    // Follows the timeline; does not signal back.
    void setCurrentCell(int layer, int frame);
    void deleteSelection();
    void cut();
    void copy();
    void paste();
    void selectAll();
    void deselectAll() { extendSelection(m_frame); }
    void onActionZoomIn() { zoomIn(); }
    void onActionZoomOut() { zoomOut(); }
    void onActionResetZoom() { resetZoom(); }
    void onActionResetPan() { resetPan(); }
    void onActionResetView() { resetView(); }

public:
    // The frames selected: from the current cell down or up its column.
    int selectionFirstFrame() const { return m_extent < 0 ? m_frame : std::min(m_frame, m_extent); }
    int selectionLastFrame() const { return m_extent < 0 ? m_frame : std::max(m_frame, m_extent); }
    int selectionExtent() const { return m_extent; }
    void extendSelection(int frame, int layer = -1);
    // ... to a column given by its place (-1: the current one only).
    void extendSelectionToColumn(int frame, int column);
    // The layers of the columns selected, left to right (each once).
    QVector<int> selectedLayers() const;
    // The columns selected (places in columns()).
    QVector<int> selectedColumns() const;
    bool hasSelection() const { return m_extent >= 0 || m_extentColumn >= 0; }
    // Where a cell is, in the coordinates of viewport().
    QRect cellRect(int layer, int frame) const;
    QRect columnCellRect(int column, int frame) const;
    QRect headerRect(int column) const;
    int headerHeight() const;
    int rowHeaderWidth() const { return kRowHeaderWidth; }
    void beginTyping(QString const& start = QString());
    void endTyping(bool keep);
    // What a frame is called in the frame column: its number, or feet and frames (16 frames to a foot).
    QString frameLabel(int frame) const;

signals:
    void cellPicked(int layer, int frame);
    // Paste, with artwork copied last (see Canvas::artworkCopiedLast).
    void pasteArtwork();
    void contextMenuRequested(QPoint const& globalPos);
    void headerMenuRequested(QPoint const& globalPos);
    void sheetMarkerMenuRequested(QPoint const& globalPos);
    // The block of cells selected changed (in this view).
    void selectionChanged();
    void columnsChanged();
    void renameRequested(int layer);
    void statusChanged();
    // Remove Selected Frames asked for, from Delete with every column selected.
    void removeFramesRequested(int first, int last);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    QSize sizeHint() const override { return QSize(320, 400); }

private:
    void rebuildColumns();
    void updateScrollBars();
    void keepInView();
    void pick(QPoint pos);
    int columnAt(int x) const;          // viewport x; -1 off the columns
    int frameAt(int y) const;           // viewport y; may be out of the scene
    bool columnShown(Scene const& scene, int layer) const;
    int widthOf(Layer const& layer) const;
    void notifySelection();
    // Painting (XsheetPaint).
    void paintCells(QPainter& p, Column const& column, int x, int firstRow, int lastRow);
    void paintFunctionCells(QPainter& p, Column const& column, int x, int firstRow, int lastRow);
    void paintAnnotation(QPainter& p, Column const& column, int x, int firstRow, int lastRow);
    void paintHeaders(QPainter& p);
    void paintSheetMarkers(QPainter& p);
    void paintTempo(QPainter& p, int firstRow, int lastRow);
    QColor columnColour(Layer const& layer, int function) const;
    QString functionText(int layer, int function, int frame) const;
    // Typing into a function column: a keyframe of that value here.
    void setFunctionValue(int layer, int function, int frame, double value);
    // Gestural drag.
    bool beginGesture(QPoint pos);
    void moveGesture(QPoint pos);
    void endGesture(bool keep);

    static constexpr int kRowHeaderWidth = 44;
    static constexpr int kDefaultZoom = 9;
    static constexpr int kCollapsedWidth = 12;
    int m_columnWidth = 78;
    bool m_centreOnFrame = false;
    int m_defaultHold = 1;
    int m_zoom = kDefaultZoom;
    int m_zoomMin = 5, m_zoomMax = 16;
    int m_show = ShowEverything;
    int m_heldLook = HeldLine;
    int m_lineThreshold = 0;
    bool m_feet = false;
    bool m_thumbnails = false;
    bool m_minimal = false;
    bool m_grouping = false;
    bool m_grid = true;
    bool m_selectionOnly = false;
    QSet<int> m_selectionIds;           // layer ids
    bool m_gestural = true;
    bool m_insert = false;
    bool m_onion = false;
    int m_onionPrevious = 0, m_onionNext = 0;
    bool m_lightTable = false;
    bool m_annotationMarks = false;
    double m_annotationHardness = 10.0;
    bool m_annotationDrawing = false;
    QColor m_penColour = Qt::black;
    double m_penWidth = 2.0;

    Document* m_document;
    QVector<Column> m_columns;
    int m_contentWidth = 0;
    QLineEdit* m_entry = nullptr;
    QPlainTextEdit* m_note = nullptr;
    int m_noteLayer = -1, m_noteFrame = -1;
    int m_layer = 0;
    int m_column = -1;
    int m_frame = 0;
    int m_extent = -1;         // the far end of the frames selected; -1: only the current cell
    int m_extentColumn = -1;   // the far column selected; -1: only the current column
    bool m_selecting = false;
    // A drag going on.
    enum Drag { NoDrag, DragResize, DragColumn, DragBlock, DragExposure, DragInk, DragErase, DragMarker, DragMarkerEnd };
    Drag m_drag = NoDrag;
    QPoint m_dragFrom;
    int m_dragColumn = -1, m_dragTarget = -1, m_dragWidth = 0;
    int m_dragRows = 0, m_dragFrames = 0;
    int m_exposureLayer = -1, m_exposureStart = 0, m_exposureEnd = 0;
    QVector<int> m_exposureCells;
    bool m_exposureBegun = false;
    QVector<QPointF> m_ink;
    int m_dragMarker = -1, m_markerFrame = 0, m_markerLength = 1, m_markerGrab = 0;
    int m_menuMarker = -1;
};
