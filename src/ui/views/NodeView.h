// Eclipse - the Node View: the scene as a graph.  Every layer is a node; a cable runs from
// a layer to each layer attached to it, and from every drawing into the composite that
// stacks them into the picture.
//
// Drag a node to arrange the graph, drag from the port under a node onto another node to
// attach that one to it, click a cable and press Delete to detach.
#pragma once

#include "EclipseExport.h"

#include <QHash>
#include <QImage>
#include <QKeySequence>
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QWidget>
#include <QJsonObject>
#include <algorithm>
#include <functional>

#include "model/scene/NodeGraph.h"

class Document;
class QLineEdit;
class QMenu;
class QKeyEvent;

class ECLIPSE_UI_API NodeView : public QWidget {
    Q_OBJECT

public:
    explicit NodeView(Document* document, QWidget* parent = nullptr);

    // Where the node of a layer is in the widget, and the port under it cables leave from.
    QRectF nodeRect(int layer) const;
    QPointF outPort(int layer) const;
    // The middle of the cable into `child` from its parent (for picking it).
    QPointF cableMiddle(int child) const;
    int find(QString const& text);
    QRectF navigatorRect() const;
    int enteredGroup() const;
    void enterGroup(int layer);
    void leaveGroup();
    // Whether the node of a layer is on show at the level the view is at.
    bool nodeShown(int layer) const;
    // The path along the top of the view while inside a group; a click on it goes out one level.
    QRectF pathRect() const;
    QString pathText() const;
    QRectF groupInputRect() const;
    QRectF groupOutputRect() const;
    // The layers the input node is cabled to: those that hang from the group itself.
    QVector<int> groupInputs() const;
    QPointF navigatorPoint(QPointF graph) const;
    // Where a place of the view is in the graph.
    QPointF graphPoint(QPointF widgetPos) const { return toGraph(widgetPos); }
    double zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    void restoreView(double zoom, QPointF pan) { m_zoom = std::clamp(zoom, 0.1, 4.0); m_pan = pan; m_centred = true; update(); }
    QJsonObject viewState() const;
    void restoreViewState(QJsonObject const& state);
    QVector<int> pickedLayers() const;
    void setNavigatorCorner(int corner);
    int navigatorCorner() const { return m_navigatorCorner; }
    // Larger or smaller about the middle of the view.
    void zoomBy(double factor);
    // Preferences: the small map in the corner shown or not; cables drawn straight from port to port, or as curves.
    void setNavigatorShown(bool on);
    bool navigatorShown() const { return m_navigator; }
    void setStraightCables(bool on);
    bool straightCables() const { return m_options.cableType == 1; }
    // Preferences: a double-click on a layer's node opens its properties (editRequested) instead of renaming it.
    void setDoubleClickOpensEditor(bool on) { m_doubleClickEdits = on; }
    bool doubleClickOpensEditor() const { return m_doubleClickEdits; }

    // The Node View preferences.
    struct Options {
        int cableType = 0;              // 0 curved, 1 a line from port to port, 2 straight runs with square corners
        bool wideCables = true;         // cables out of a pass-through composite wide, in two colours
        bool portsRightToLeft = false;  // a composite's first input on its right
        double magnifierWidth = 6.0;    // in tenths of the view's width
        double magnifierAspect = 1.5;   // width over height
        int magnifierOpacity = 80;      // percent
        double magnifierScale = 5.0;    // how much larger
        bool panMiddle = true;          // a middle drag pans; otherwise it moves the nodes picked
        bool readColour = true;         // drawing nodes in their layer's colour, when it has one
        int backdropSelected = 220;     // backdrop opacity (0..255), picked and not
        int backdropUnselected = 170;
        int backdropTitleSize = 14;
        int backdropColours = 17;       // how many of the backdrop colours are offered
        bool compositePassThrough = false;  // new composites pass their inputs through
    };
    void setOptions(Options const& options);
    Options const& options() const { return m_options; }
    // The cable style: 0 curved, 1 line, 2 straight with square corners.
    void setCableType(int type);
    int cableType() const { return m_options.cableType; }
    // The keys of the Node View's own shortcuts (by shortcut id), asked of the window when a key is pressed here.
    void setShortcutLookup(std::function<QKeySequence(QString const&)> lookup) { m_shortcutLookup = std::move(lookup); }
    bool matchesShortcut(QKeyEvent* event, QString const& id) const;
    // The commands of the Node View's own menu, added to the menu shown in a graph wired by hand.
    void setSharedMenu(std::function<QMenu*(QWidget*)> make) { m_sharedMenu = std::move(make); }

    // ---- switched on and off ----
    bool nodeEnabled(int id) const;
    // ---- going round the graph ----
    // The node a node takes its picture or motion from, and the one it gives to (graph ids; layer mode: layer indexes).
    int parentOf(int node, bool skipEffects) const;
    int childOf(int node, bool skipEffects) const;
    // A node's path from the top, groups and all ("Top/Group/Node").
    QString qualifiedName(int node) const;
    // ---- waypoints ----
    int addWaypoint(QPointF graphPos, int onCable = -1);
    // ---- ports picked (their names and filters) ----
    struct PortRef {
        int node = 0;
        int port = 0;
        bool out = false;
        bool operator==(PortRef const&) const = default;
    };
    QVector<PortRef> pickedPorts() const { return m_portsPicked; }
    void pickPort(PortRef port, bool add);
    // Names given to outputs, filters given to inputs: to the ports picked, else to those of the nodes picked.
    int namePorts(QString const& name, bool filters);
    bool portFilteringShown() const { return m_showFilters; }
    // ---- the cable cutter ----
    bool cableCutter() const { return m_cutter || m_cutterHeld; }
    // Every cable at this level crossed by the line between two places of the view is cut, as one step.
    int cutCables(QPointF from, QPointF to);
    // ---- thumbnails ----
    bool thumbnailShown(int node) const;
    QRectF thumbnailRect(int node) const;
    QImage thumbnailImage(int node, QSize size) const;
    bool thumbnailPattern() const { return m_thumbPattern; }
    // ---- the magnifier ----
    bool magnifier() const { return m_magnifier; }
    QRectF magnifierRect() const;
    // ---- backdrops ----
    QVector<Backdrop> const& backdrops() const;
    QRectF backdropRect(int index) const;
    int backdropAt(QPointF widgetPos) const;
    QVector<int> pickedBackdrops() const { return m_backdropPicked; }
    void pickBackdrop(int index, bool add);
    int addBackdrop(QString const& title = QString());
    void setBackdrops(QVector<Backdrop> const& backdrops, QString const& text);
    bool editBackdrop(int index);
    // The colours offered for a backdrop (the Colours preferences, as many as the Node View preferences say).
    QVector<QColor> backdropColours() const;
    // ---- the whole view as a picture (to print) ----
    QImage picture(QSize size);
    // ---- what the library offers to add, found by name ----
    QStringList insertableNames() const;
    bool insertByName(QString const& name, QPointF graphPos);
    // The kind a node is, as Search For Nodes lists it; and the nodes found by name (and kind), picked.
    QString nodeTypeName(int key) const;
    QStringList nodeTypeNames() const;
    int selectMatching(QString const& text, bool caseSensitive, QStringList const& types);

    // ---- the node graph, wired by hand (see NodeGraph) ----
    // Whether the scene's graph is wired by hand: the view then shows it and edits it (otherwise it shows the layers as
    // they stack).
    bool graphMode() const;
    // Where a node of the graph is (by its id) and its ports, in the widget; and the node under a place (0: none).
    QRectF graphNodeRect(int id) const;
    QPointF graphInPort(int id, int port) const;
    QPointF graphOutPort(int id, int port) const;
    int graphNodeAt(QPointF widgetPos) const;
    // The group of the graph the view is inside (its id; 0 at the top), gone into and out of.
    int graphGroup() const { return m_graphGroup; }
    void enterGraphGroup(int id);
    // The node and the cable picked (an id; a place among the cables), or 0 and -1.
    int pickedGraphNode() const { return m_graphNode; }
    int pickedGraphCable() const { return m_graphCable; }
    void pickGraphNode(int id);
    void pickGraphCable(int index);
    int addGraphNode(GraphNode node, QPointF graphPos);
    bool connectGraph(int from, int fromPort, int to, int toPort);
    void changeGraphPorts(int id, bool outputs, int by);
    QPointF graphTransformIn(int id) const;
    QPointF graphTransformOut(int id) const;
    // The nodes picked together (Ctrl+click adds one or takes it away).
    QVector<int> pickedGraphNodes() const;
    // Glue Selection: the two nodes picked joined by a Glue node (the first glued onto the second, which it replaces where that went).
    Q_INVOKABLE void onActionGlueSelection();
    void addPickedGraphNode(int id);
    int groupGraphNodes(QVector<int> const& ids);
    // The nodes grouped, with a composite inside taking whatever left them, so the group gives one picture (its id; 0: none).
    int groupGraphNodesWithComposite(QVector<int> const& ids);
    // The picked nodes cabled into new inputs of a composite (how many were).
    int connectToComposite(int composite, QVector<int> const& ids);
    // What a port of a node takes or gives (shown when the pointer rests on it): "Input image", "Matte" ...
    QString graphPortName(GraphNode const& node, int port, bool input) const;

public slots:
    // Nodes switched on and off: picked, all, all but those picked.
    void onActionEnableNodes();
    void onActionDisableNodes();
    void onActionEnableAll();
    void onActionDisableAllOthers();
    // Going round the graph from the node picked.
    void onActionSelectParent();
    void onActionSelectParentSkippingEffects();
    void onActionSelectChild();
    void onActionSelectChildSkippingEffects();
    void onActionSelectChildren();
    void onActionSelectPreviousSibling();
    void onActionSelectNextSibling();
    void onActionCentreOnParent();
    void onActionCentreOnChild();
    // Groups.
    void onActionGroupSelection();
    void onActionCreateGroup();
    void onActionMoveToParentGroup();
    void onActionExplodeGroups();
    void onActionSelectGroupContent();
    // Waypoints and port names.
    void onActionCreateWaypoint();
    void onActionRenameWaypoint();
    void onActionAddPortName();
    void onActionAddPortFilter();
    void onActionClearPortNames();
    void onActionCopyPortNames();
    void onActionPastePortNames();
    void onActionShowPortFiltering(bool on);
    void onActionCableCutter(bool on);
    // Thumbnails, the navigator, the magnifier.
    void onActionShowSelectedThumbnails();
    void onActionHideSelectedThumbnails();
    void onActionToggleSelectedThumbnails();
    void onActionShowAllThumbnails();
    void onActionHideAllThumbnails();
    void onActionThumbnailPattern(bool on);
    void onActionShowNavigator(bool on);
    void onActionMagnifier(bool on);
    // Arranging.
    void onActionOrderUp();
    void onActionOrderDown();
    void onActionAlignHorizontally();
    void onActionCopyQualifiedName();
    // `how`: "timing" (a clone keeping the timing), "drawings" (a clone with timing of its own) or "duplicate".
    void onActionCloneNodes(QString const& how);
    void onActionEditor();
    void onActionCreateDisplay();
    void onActionCreateBackdrop();
    // Composites: a new one; the picked nodes grouped with one inside; the picked nodes cabled into the picked composite.
    void onActionCreateComposite();
    void onActionGroupWithComposite();
    void onActionConnectToComposite();
    void onActionRecalculateSelected();
    void onActionRecalculateAll();
    void onActionFindAndInsert();
    void onActionSearchNodes();
    void cut();
    void copy();
    void paste();
    void onActionNavigator(QString const& position);
    void onActionCableStyle(QString const& style);
    void selectAll();
    void deselectAll();
    void onActionInvertSelection();
    void onActionCentreOnSelection();
    void setCurrentCell(int layer, int frame);
    void deleteSelection();
    void onActionResetView();
    // The group that is the current layer is gone into; and out again, one level.
    void onActionEnterGroup();
    void onActionExitGroup() { leaveGroup(); }
    // View > Zoom In and the rest, with this view in use.
    void onActionZoomIn() { zoomBy(1.25); }
    void onActionZoomOut() { zoomBy(0.8); }
    void onActionResetZoom() { zoomBy(1.0 / m_zoom); }
    void onActionResetPan();
    // The graph made from the layers as they stack, to wire by hand from; and let go again (the layers stack as they are).
    void onActionWireGraph();
    void onActionStackLayers();

signals:
    void layerPicked(int layer);
    void renameRequested(int layer);
    // A double-click on a layer's node, when it is to open the layer's properties (setDoubleClickOpensEditor).
    void editRequested(int layer);
    void contextMenuRequested(QPoint const& globalPos);
    void nodeDropped(QString const& what, QPointF graphPos);
    // The layers picked here (front to back), for the Timeline to show picked too.
    void layersPicked(QVector<int> const& layers);
    void statusMessage(QString const& text);
    void recalculateRequested(bool all);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void leaveEvent(QEvent*) override;
    QSize sizeHint() const override { return QSize(360, 300); }

private:
    static constexpr double kNodeWidth = 124.0;
    static constexpr double kNodeHeight = 30.0;

    QVector<QPointF> positions() const;
    QPointF compositePos(QVector<QPointF> const& positions) const;
    QPointF toWidget(QPointF graph) const { return graph * m_zoom + m_pan; }
    QPointF toGraph(QPointF widget) const { return (widget - m_pan) / m_zoom; }
    int nodeAt(QPointF widgetPos) const;
    QRectF graphBounds() const;
    int cableAt(QPointF widgetPos) const;
    QVector<QPointF> cablePoints(QPointF from, QPointF to) const;
    static bool cableContains(QVector<QPointF> const& points, QPointF position, double reach);
    static bool crosses(QVector<QPointF> const& points, QPointF from, QPointF to);
    void paintView(QPainter& p);
    void paintLayerMode(QPainter& p);
    void paintBackdrops(QPainter& p);
    void paintThumbnails(QPainter& p);
    void paintMagnifier(QPainter& p);
    void paintEditorButton(QPainter& p, QRectF const& node);
    QRectF editorButton(QRectF const& node) const;
    QColor layerNodeColour(int layer) const;
    QColor nodeBorder(GraphNode const* node, int layer) const;
    bool showsEditor(int layer, GraphNode const* node) const;
    // A group node holding effects and nothing else (shown in the group-effect colours).
    bool effectGroup(int id) const;
    // The node keys the picked thumbnails and the like go by: a graph node's id, or (layer mode) a layer's id.
    QVector<int> pickedKeys() const;
    void setThumbs(QVector<int> const& keys, int height, QString const& text);
    void switchNodes(QVector<int> const& keys, bool on, QString const& text);
    // The mouse on what both kinds of view have: the cutter, thumbnails, editor buttons, backdrops.
    bool commonPress(QMouseEvent* event);
    bool backdropPress(QMouseEvent* event);
    bool commonMove(QMouseEvent* event);
    bool commonRelease(QMouseEvent* event);
    bool backdropMenu(QPoint globalPos, QPointF widgetPos);
    int thumbGripAt(QPointF widgetPos) const;
    int editorAt(QPointF widgetPos) const;
    void openEditor(int key);
    QRectF nodeRectOf(int key) const;
    QVector<int> shownKeys() const;
    void emitPicked();
    bool handleOwnKeys(QKeyEvent* event, bool dryRun);
    void selectLayers(QVector<int> const& layers);
    void selectRelative(int which, bool skipEffects, bool centre);
    void moveOutOfGroup(QVector<int> const& ids, bool dissolve);
    QString askName(QString const& title, QString const& label, QString const& text, bool waypoint);
    bool m_printing = false;
    bool m_inLens = false;
    Options m_options;
    std::function<QKeySequence(QString const&)> m_shortcutLookup;
    std::function<QMenu*(QWidget*)> m_sharedMenu;
    QVector<PortRef> m_portsPicked;
    PortRef m_portPress;
    bool m_showFilters = false;
    bool m_cutter = false;
    bool m_cutterHeld = false;
    bool m_thumbPattern = false;
    bool m_magnifier = false;
    QPointF m_hover{-1, -1};
    QVector<int> m_backdropPicked;
    int m_backdropDrag = -1;
    bool m_backdropResize = false;
    QVector<int> m_backdropCarry;               // the nodes a backdrop being moved takes with it (keys)
    int m_thumbDrag = 0;                        // the node whose thumbnail is being made larger or smaller (key)
    int m_thumbStart = 0;
    mutable QHash<QString, QImage> m_thumbCache;
    bool m_navigator = true;
    bool m_doubleClickEdits = false;
    int m_navigatorCorner = 2;
    bool m_spacePan = false;
    QVector<int> m_layerPicked;
    QVector<int> m_boxBefore;
    Qt::KeyboardModifiers m_boxModifiers = Qt::NoModifier;
    void startBoxSelection(QMouseEvent* event);
    void updateBoxSelection();
    void paintBoxSelection(QPainter& painter);
    void movePickedNodes(QPointF delta);
    QPointF nodeDragDelta(QPointF widgetPos) const;

    Document* m_document;
    QLineEdit* m_search = nullptr;     // along the bottom: type a name, Enter goes to the next node of it
    int m_layer = 0;
    int m_frame = 0;
    int m_groupId = 0;      // the id of the group the view is inside; 0 at the top
    double m_zoom = 1.0;
    QPointF m_pan = QPointF(40, 150);
    bool m_centred = false;   // the graph is brought into view the first time it is shown

    enum Drag { DragNone, DragNode, DragCable, DragPan, DragWire, DragGraphNode, DragPeg, DragBox, DragDisconnect, DragCut, DragBackdrop, DragThumb } m_drag = DragNone;

    // The graph, wired by hand: drawn and worked with in the widget.
    int m_graphGroup = 0;
    int m_graphNode = 0;
    int m_graphCable = -1;
    int m_wireFrom = 0, m_wireFromPort = 0;     // a cable being pulled out of this output
    bool m_wireReverse = false;
    int m_unplugged = -1;                       // a cable taken off an input to be plugged in elsewhere (its place)
    QVector<int> m_graphPicked;                 // the nodes picked together
    int m_pegFrom = 0, m_pegChild = 0;          // a parenting cable pulled out of this node (taken off this child)
    QRectF graphLevelBounds() const;
    struct GraphPort {
        int node = 0;
        int port = -1;
        bool out = false;
    };
    GraphPort graphPortAt(QPointF widgetPos) const;
    int graphCableAt(QPointF widgetPos) const;
    double graphNodeWidth(GraphNode const& node) const;
    bool pictureless(GraphNode const& node) const;
    QString graphNodeName(GraphNode const& node) const;
    QRectF graphPathRect() const;
    void paintGraphMode(QPainter& p);
    void graphPress(QMouseEvent* event);
    void graphMove(QMouseEvent* event);
    void graphRelease(QMouseEvent* event);
    void graphDoubleClick(QMouseEvent* event);
    void graphContextMenu(QPoint globalPos, QPointF widgetPos);
    void deleteGraphSelection();
    void editGraphNode(int id);
    // A composite's settings: its name, mode (bitmap, vector, pass through) and how deep what it gives stands.
    void editComposite(int id);
    void resetGraphView();
    void addNavigationMenu(QMenu& menu);
    int m_dragLayer = -1;
    QPointF m_pressPos;       // widget
    QPointF m_dragNow;        // widget
    QPointF m_nodeStart;      // graph position of the node when its drag began
    bool m_moved = false;
    int m_cable = -1;         // the picked cable: the layer it goes into
};
