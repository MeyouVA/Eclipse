// Eclipse - the Function view: the animated values of the current layer as curves over
// the frames of the scene.  At the left the values to show, each with its colour; at the
// right the graph, with a square on a curve wherever the layer has a keyframe.  A square
// dragged up or down changes that value on that keyframe.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include "ui/timeline/Timeline.h"

class Document;
struct Layer;
class QListWidget;

// The graph itself.
class ECLIPSE_UI_API FunctionGraph : public QWidget {
    Q_OBJECT

public:
    explicit FunctionGraph(Document* document, QWidget* parent = nullptr);

    void setCurrentCell(int layer, int frame);
    void setShown(Timeline::Parameter which, bool shown);
    // The other layers selected in the Timeline: their curves are shown too, faint and dashed,
    // to compare with (the squares to drag are the current layer's).
    void setOtherLayers(QVector<int> const& layers);
    QVector<int> otherLayers() const { return m_others; }
    // The keyframe on the current frame has two handles, on the first curve shown: the one after it
    // is pulled along to leave more gently (ease out), the one before it to arrive more gently
    // (ease in).  Where a handle is; a null point when there is none.
    QPointF easeHandle(bool out) const;
    bool isShown(Timeline::Parameter which) const { return m_shown[which]; }
    static QColor colourOf(Timeline::Parameter which);

    // The value a curve has on a frame, as it is drawn (opacity as a percentage).
    double valueAt(Timeline::Parameter which, int frame) const;
    // Where that is in the widget; and the value a height of the widget stands for.
    QPointF pointAt(Timeline::Parameter which, int frame) const;
    double valueOfY(double y) const;
    // The lowest and highest value the graph shows.
    double bottomValue() const { return m_low; }
    double topValue() const { return m_high; }
    // The wheel comes closer along the frames, about the pointer; with Ctrl or Shift, along
    // the values.  The middle button slides the graph; pressed twice, it shows all of it again.
    double firstFrame() const { return m_first; }
    double frameSpan() const;
    bool zoomed() const { return m_zoomed || m_span > 0.0; }
    void resetView();

signals:
    void framePicked(int frame);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    QSize sizeHint() const override { return QSize(420, 220); }

private:
    void fit();
    double xOfFrame(double frame) const;
    double yOfValue(double value) const;
    int frameOfX(double x) const;
    QRectF plot() const;

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    QVector<int> m_others;
    // The position - across, down and in depth - to begin with.
    bool m_shown[Timeline::ParameterCount] = {true, true, false, false, false, false, true, false, false};
    double m_first = 0.0;       // the frame at the left edge
    double m_span = 0.0;        // how many frames across; 0: all of the scene
    bool m_zoomed = false;      // the values shown were chosen by hand: they are not fitted again
    bool m_sliding = false;
    QPointF m_slideLast;
    double m_low = -1.0, m_high = 1.0;
    // The keyframe being dragged: which curve, which of the layer's keyframes, and the
    // value it would have.
    int m_dragParam = -1;
    int m_dragKey = -1;
    double m_dragValue = 0.0;
    int m_dragFrame = 0;       // and the frame it would be on (between its neighbours)
    // The ease handle being dragged (0: none, 1: out, 2: in), of which keyframe, and the ease it would give (0 to 1).
    int m_easeDrag = 0;
    int m_easeKey = -1;
    double m_easeValue = 0.0;
    int firstShown() const;
    QPointF easeHandleOf(Layer const& layer, int key, bool out) const;
    // The keyframe (of the layer's) whose square is under the point; -1 for none.
    int keyAt(QPointF pos, int* param = nullptr) const;
};

class ECLIPSE_UI_API FunctionView : public QWidget {
    Q_OBJECT

public:
    explicit FunctionView(Document* document, QWidget* parent = nullptr);

    FunctionGraph* graph() const { return m_graph; }

public slots:
    void setCurrentCell(int layer, int frame) { m_graph->setCurrentCell(layer, frame); }
    void setOtherLayers(QVector<int> const& layers) { m_graph->setOtherLayers(layers); }

signals:
    void framePicked(int frame);

private:
    FunctionGraph* m_graph;
    QListWidget* m_list;
};
