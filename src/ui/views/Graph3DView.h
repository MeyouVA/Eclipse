// Eclipse - 3D Graph: the layers as they hang together, with where each stands in space, and the parts (subnodes) of
// the 3D model the current layer shows: each can be disabled, have its shape hidden, or be given a move, turn and
// scale of its own (Make Properties Available).
#pragma once

#include "EclipseExport.h"

#include <QVector>
#include <QWidget>

#include "model/geometry/Model3D.h"

class Document;
class QAction;
class QDoubleSpinBox;
class QGroupBox;
class QTreeWidget;
class QTreeWidgetItem;

class ECLIPSE_UI_API Graph3DView : public QWidget {
    Q_OBJECT

public:
    explicit Graph3DView(Document* document, QWidget* parent = nullptr);

    // The parts of the model shown by the current layer on this frame (none: it shows no model).
    std::shared_ptr<Model3D::Mesh const> mesh() const;
    // The names of the parts picked in the tree.
    QStringList selectedSubnodes() const;
    // The part picked (and made the current one); false if there is no such part.
    bool selectSubnode(QString const& name);
    QString currentSubnode() const;
    // The drawing's look of the model, as it is now.
    Model3D::Look look() const;
    // The values of the picked parts' properties set (Set Subnode Position / Rotation / Scale / Pivot).
    void setSubnodeValues(QVector3D const& move, QVector3D const& turn, QVector3D const& scale, QVector3D const& pivot);
    QTreeWidget* tree() const { return m_tree; }

public slots:
    void setCurrentCell(int layer, int frame);
    void onActionEnableSubnodes() { setSubnodesHidden(false); }
    void onActionDisableSubnodes() { setSubnodesHidden(true); }
    void onActionShowShapes() { setShapesHidden(false); }
    void onActionHideShapes() { setShapesHidden(true); }
    void onActionSelectParentSubnode();
    void onActionSelectChildSubnodes();
    void onActionSelectNextSibling() { selectSibling(1); }
    void onActionSelectPreviousSibling() { selectSibling(-1); }
    void onActionCopySubnodeName();
    void onActionMakePropertiesAvailable();
    void onActionResetSubnode();
    void onActionExpandAll();
    void onActionCollapseAll();
    void onActionExpandSelection() { expandSelection(true); }
    void onActionCollapseSelection() { expandSelection(false); }
    void setExtendedMode(bool on);

signals:
    void layerPicked(int layer);
    void message(QString const& text);

private:
    void rebuild();
    void showProperties();
    QTreeWidgetItem* subnodeItem(int node) const;
    int nodeOf(QTreeWidgetItem const* item) const;
    // The parts picked; when none is, a message says so.
    QVector<int> pickedNodes(QString const& what);
    void setSubnodesHidden(bool hidden);
    void setShapesHidden(bool hidden);
    void selectSibling(int by);
    void expandSelection(bool expand);
    // The drawing's look of its model changed, as one step to undo.
    void changeLook(QString const& text, std::function<void(Model3D::Look&)> const& change);

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    QTreeWidget* m_tree;
    QVector<int> m_shape;       // the layers and what each hangs under, as the tree was last built
    QString m_modelShape;       // and the model's parts
    bool m_rebuilding = false;
    bool m_extended = true;
    QVector<QTreeWidgetItem*> m_subnodes;
    QGroupBox* m_properties;
    QDoubleSpinBox* m_values[12];
    QAction* m_extendedAction;
};
