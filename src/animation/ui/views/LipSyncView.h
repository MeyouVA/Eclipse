// Eclipse - the Lip-Sync view: the sound of a sound layer frame by frame, with the shape of
// the mouth on each frame, and the chart of the eight shapes to change it from.
#pragma once

#include "EclipseExport.h"

#include <QChar>
#include <QIcon>
#include <QWidget>

class Document;
class QPainter;
class QToolButton;
class QLabel;
class Scene;

// The strip: the waveform and, under it, a letter for each frame.
class ECLIPSE_UI_API LipSyncStrip : public QWidget {
    Q_OBJECT

public:
    explicit LipSyncStrip(Document* document, QWidget* parent = nullptr);
    void setCell(int soundLayer, int frame);
    // Where a frame is in the strip.
    QRectF frameRect(int frame) const;
    QSize sizeHint() const override { return QSize(420, 96); }

signals:
    void framePicked(int frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    Document* m_document;
    int m_layer = -1;
    int m_frame = 0;
};

class ECLIPSE_UI_API LipSyncView : public QWidget {
    Q_OBJECT

public:
    explicit LipSyncView(Document* document, QWidget* parent = nullptr);

    static int soundLayerFor(Scene const& scene, int current);
    // The shape of a mouth, drawn: for the chart, and wherever a shape is to be shown.
    static void paintMouth(QPainter& painter, QRectF const& box, QChar shape);
    static QIcon mouthIcon(QChar shape, int size = 40);

    int soundLayer() const { return m_soundLayer; }
    LipSyncStrip* strip() const { return m_strip; }

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void framePicked(int frame);
    void mouthChosen(QString const& shape);
    void detectRequested();
    void mapRequested();
    void clearRequested();

private:
    void refresh();

    Document* m_document;
    int m_layer = 0;
    int m_frame = 0;
    int m_soundLayer = -1;
    LipSyncStrip* m_strip;
    QLabel* m_title;
    QVector<QToolButton*> m_mouths;
};
