// Eclipse - the node graph: nodes, cables, how they are put together.
#pragma once

#include "model/scene/Layer.h"

struct GraphNode {
    enum Kind { LayerNode, Composite, EffectNode, Group, GroupIn, GroupOut, Display, Note, KindCount };
    int id = 0;
    Kind kind = Composite;
    QString name;
    int layer = 0;              // a layer node: the layer's id
    int sheets = 0xf;
    Effect effect;              // an effect node: what it does (its input 0 the picture; input 1 the matte, for those that take one)
    // An effect node's amount, keyframed: so much on these frames, straight from one to the next (none: `effect.amount`).
    QMap<int, double> amountKeys;
    double amountAt(int frame) const;
    // How many inputs it has (a composite, a group, a group's output node), and outputs (a group, a group's input node).
    int ports = 1;
    int outPorts = 1;
    int group = 0;              // the group node it is in (0: none, at the top)
    QPointF at;                 // where it stands in the Node View
    // The ports it has, in and out.
    int inputs() const;
    int outputs() const;
    // An effect that takes a matte at its second input.
    static bool takesMatte(Effect::Type type);
    bool operator==(GraphNode const&) const = default;
};

struct GraphCable {
    int from = 0, fromPort = 0;     // the node (its id) and its output the cable leaves
    int to = 0, toPort = 0;         // the node and its input it goes into
    bool operator==(GraphCable const&) const = default;
};

struct NodeGraph {
    QVector<GraphNode> nodes;
    QVector<GraphCable> cables;
    bool isEmpty() const { return nodes.isEmpty(); }
    int nodeIndex(int id) const;
    GraphNode const* node(int id) const;
    // The cable into an input (an input takes one), or null.
    GraphCable const* into(int id, int port) const;
    int nextId() const;
    // The Display at the top that the frame shows (the first), its id; 0: none.
    int display() const;
    // The node of a layer (its id), 0: none.
    int layerNode(int layerId) const;
    // Whether a cable out of `from` into `to` would close a ring (`to` already feeds `from`).
    bool feeds(int to, int from) const;
    // A group's node for what comes into it, and for what goes out (ids), 0: none.
    int groupIn(int group) const;
    int groupOut(int group) const;
    bool operator==(NodeGraph const&) const = default;
};
