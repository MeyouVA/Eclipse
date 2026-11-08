// Eclipse - a Master Controller's controls: made by its own script, they set the attributes it names (Scene::Controller::master).
//
// The script sees what any script does (layers, node, frame, MessageLog ...) and `controls`:
//   controls.slider({attribute: "Peg:rotation.angle", min: -90, max: 90, label: "Turn"})
//   controls.point({x: "Peg:position.x", y: "Peg:position.y", min: -200, max: 200, label: "Aim"})   (or minX, maxX, minY, maxY)
//   controls.line({x1: ..., y1: ..., x2: ..., y2: ..., min: ..., max: ..., label: ...})   two points joined, each end dragged on its own
//   controls.check({attribute: ..., on: 1, off: 0, label: ...})
//   controls.label({text: ..., attribute: ...})   a name, or an attribute's value shown as it goes
//   controls.data (read and set: kept with the controller), controls.name, controls.frame (from 1)
//   function onChange(label, value) { ... }   run after a control has set its attributes, in the same step of undo
//   function onLoad() { ... }                 run when the scene is opened (the controller's On Load)
// Each of slider, point, line, check and label gives the control's number, or -1 when an attribute it names is not there.
#pragma once

#include "EclipseExport.h"

#include <QJSValue>
#include <QWidget>

#include <memory>

class Document;
class QJSEngine;
class QLabel;
class QVBoxLayout;
class ScriptLayers;
class ScriptOutput;
class Timeline;

// Points on a square that spans a range each way; each dragged on its own, joined by a line when there are two.
class ECLIPSE_UI_API MasterPad : public QWidget {
    Q_OBJECT

public:
    explicit MasterPad(QWidget* parent = nullptr);
    void setRange(QRectF range);
    void setPoints(QVector<QPointF> const& points);
    QVector<QPointF> points() const { return m_points; }
    QPointF toWidget(QPointF value) const;
    QPointF toValue(QPointF widgetPos) const;
    QSize sizeHint() const override { return QSize(120, 120); }

signals:
    // A point let go there (in the range's values).
    void moved(int point, QPointF value);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF square() const;
    QRectF m_range{-100, -100, 200, 200};
    QVector<QPointF> m_points;
    int m_dragging = -1;
};

class ECLIPSE_UI_API MasterControls : public QWidget {
    Q_OBJECT

public:
    MasterControls(Document* document, Timeline* timeline, QWidget* parent = nullptr);
    ~MasterControls() override;

    // The controller shown (its place in the scene's list): its script is run again when it is another one, or its script changed.
    void setController(int controller);
    int controller() const { return m_controller; }
    void rebuild();
    // The controls show the attributes' values on the current frame.
    void refresh();
    // What its script said, and the controls it could not make.
    QStringList messages() const { return m_messages; }
    int count() const { return int(m_controls.size()); }
    QString kindOf(int control) const;
    QString labelOf(int control) const;
    // A control set as if moved there: a slider or check box (0 / not 0) to `value`; a point to (value, y); a line's end `point`.
    bool set(int control, double value, double y = 0.0, int point = 0);
    // A function of its script, called (in a step of undo of its own when it changes the scene). Undefined when there is none.
    QJSValue call(QString const& function, QJSValueList const& arguments = {});
    // On Load: the controller's script run and its onLoad() called. False when it has none.
    static bool runOnLoad(Document* document, Timeline* timeline, int controller, QStringList* said = nullptr);

    // (What the script's `controls` calls.)
    int addControl(QString const& kind, QJSValue const& options);
    QString data() const;
    void setData(QString const& data);
    QString controllerName() const;
    int frame() const;

signals:
    void said(QString const& text);

private:
    friend class MasterScriptControls;
    struct Control {
        QString kind;           // "slider", "point", "line", "check", "label"
        QString label;
        QStringList attributes; // slider, check: 1; point: x, y; line: x1, y1, x2, y2; label: 0 or 1
        QRectF range;           // a slider: from left() to right(); a point or line: its square
        double on = 1.0, off = 0.0;
        QString text;
        QWidget* widget = nullptr;
        QLabel* value = nullptr;
    };
    void makeWidget(int control);
    void apply(int control, QVector<double> const& values, double reported);
    double read(QString const& path) const;
    Document* m_document;
    Timeline* m_timeline;
    int m_controller = -1;
    QString m_script;
    std::unique_ptr<QObject> m_parts;       // (declared before the engine: the engine goes first)
    std::unique_ptr<QJSEngine> m_engine;
    ScriptOutput* m_output = nullptr;
    ScriptLayers* m_layers = nullptr;
    QVector<Control> m_controls;
    QStringList m_messages;
    QVBoxLayout* m_rows = nullptr;
    QLabel* m_said = nullptr;
    bool m_refreshing = false;
    bool m_building = false;
};

// What the script calls `controls`.
class ECLIPSE_UI_API MasterScriptControls : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString data READ data WRITE setData)
    Q_PROPERTY(QString name READ name)
    Q_PROPERTY(int frame READ frame)

public:
    explicit MasterScriptControls(MasterControls* controls, QObject* parent) : QObject(parent), m_controls(controls) {}
    Q_INVOKABLE int slider(QJSValue const& options) { return m_controls->addControl(QStringLiteral("slider"), options); }
    Q_INVOKABLE int point(QJSValue const& options) { return m_controls->addControl(QStringLiteral("point"), options); }
    Q_INVOKABLE int line(QJSValue const& options) { return m_controls->addControl(QStringLiteral("line"), options); }
    Q_INVOKABLE int check(QJSValue const& options) { return m_controls->addControl(QStringLiteral("check"), options); }
    Q_INVOKABLE int label(QJSValue const& options) { return m_controls->addControl(QStringLiteral("label"), options); }
    QString data() const { return m_controls->data(); }
    void setData(QString const& data) { m_controls->setData(data); }
    QString name() const { return m_controls->controllerName(); }
    int frame() const { return m_controls->frame(); }

private:
    MasterControls* m_controls;
};
