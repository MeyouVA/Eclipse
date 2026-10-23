// Eclipse - the ease editors: an ease curve with its handles, Set Ease for Multiple Parameters, the Velocity Editor.
#pragma once

#include "EclipseExport.h"

#include <QDialog>
#include <QWidget>

class Document;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

// The shape of a move between two keyframes: time across, the way gone up (both 0..1), and its two handles.
class ECLIPSE_UI_API EaseCurveEdit : public QWidget {
    Q_OBJECT

public:
    explicit EaseCurveEdit(QWidget* parent = nullptr);
    QPointF leave() const { return m_leave; }
    QPointF arrive() const { return m_arrive; }
    void setHandles(QPointF leave, QPointF arrive);
    // Where a handle is drawn in the widget (a handle of no length a third of the way along).
    QPointF handlePos(bool leaving) const;
    // The way gone (0..1, more or less past the ends) a share of the time (0..1) in.
    double progressAt(double time) const;

signals:
    void changed();     // while a handle is dragged
    void edited();      // when it is let go

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override { return QSize(220, 180); }

private:
    QRectF box() const;
    QPointF toWidget(QPointF p) const;
    QPointF fromWidget(QPointF p) const;

    QPointF m_leave{1.0 / 3.0, 1.0 / 3.0};
    QPointF m_arrive{2.0 / 3.0, 2.0 / 3.0};
    int m_drag = 0;     // 1: the leaving handle, 2: the arriving one
};

// Set Ease for Multiple Parameters: one ease given to the kinds of value ticked, between the keyframes selected.
class ECLIPSE_UI_API EaseMultipleDialog : public QDialog {
    Q_OBJECT

public:
    explicit EaseMultipleDialog(QWidget* parent = nullptr);
    EaseCurveEdit* curve() const { return m_curve; }
    // The kinds ticked: "motion", "rotation", "scale", "skew", "morphing", "other".
    QStringList kinds() const;
    void setKinds(QStringList const& kinds);
    // The ready-made eases (Slow / Medium / Fast Ease IN, OUT, IN/OUT): the handles of one.
    static void preset(QString const& name, QPointF& leave, QPointF& arrive);
    static QStringList presetNames();

signals:
    // Apply, Apply/Previous (-1), Apply/Next (1).
    void applyRequested(int step);

private:
    void showNumbers();
    EaseCurveEdit* m_curve;
    QComboBox* m_presets;
    QVector<QCheckBox*> m_kinds;
    QDoubleSpinBox* m_outTime;
    QDoubleSpinBox* m_outValue;
    QDoubleSpinBox* m_inTime;
    QDoubleSpinBox* m_inValue;
    bool m_filling = false;
};

// The Velocity Editor: the ease of the move from the keyframe on (or before) the current frame to the next.
class ECLIPSE_UI_API VelocityEditor : public QWidget {
    Q_OBJECT

public:
    explicit VelocityEditor(Document* document, QWidget* parent = nullptr);
    void setCurrentCell(int layer, int frame);
    EaseCurveEdit* curve() const { return m_curve; }
    // The keyframes the move goes between (frames), or -1 when there is none.
    int fromFrame() const { return m_from; }
    int toFrame() const { return m_to; }

private:
    void refresh();
    void commit();
    Document* m_document;
    EaseCurveEdit* m_curve;
    QLabel* m_what;
    int m_layer = 0, m_frame = 0, m_from = -1, m_to = -1;
};
