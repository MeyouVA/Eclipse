// Eclipse - reading the YAML that OpenColorIO configurations are written in (the parts they use).
#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Cms::Yaml {

struct Node {
    enum Kind { Null, Scalar, Sequence, Map };
    Kind kind = Null;
    QString tag;                            // "ColorSpace" of !<ColorSpace>
    QString text;                           // a scalar
    int line = 0;
    QVector<Node> items;                    // a sequence
    QVector<QPair<QString, Node>> entries;  // a map, in order

    Node const* get(QString const& key) const;
    bool has(QString const& key) const { return get(key) != nullptr; }
    QString str(QString const& key, QString const& fallback = {}) const;
    double num(QString const& key, double fallback) const;
    bool flag(QString const& key, bool fallback) const;
    // A list of strings (a sequence, or one scalar).
    QStringList list(QString const& key) const;
    // Numbers: a sequence, or one number given for each of `count`.
    QVector<double> numbers(QString const& key, int count, double fallback) const;
};

// Reads the text; false with the line and the reason when it cannot.
bool parse(QString const& text, Node* out, QString* error);

}  // namespace Cms::Yaml
