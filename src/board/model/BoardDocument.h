#pragma once

#include "board/model/BoardProject.h"

#include <QObject>
#include <QUndoStack>

class ECLIPSE_BOARD_MODEL_API BoardDocument : public QObject {
    Q_OBJECT
public:
    explicit BoardDocument(QObject* parent = nullptr);
    BoardProject const& project() const { return m_project; }
    QUndoStack* undoStack() { return &m_undo; }
    QString const& path() const { return m_path; }

    bool open(QString const& path, QString* error = nullptr);
    bool save(QString* error = nullptr);
    bool setTitles(QString const& title, QString const& subtitle);
    bool addShot(QString const& name, int frames, int after = -1);
    bool removeShot(int shot);
    bool addPanel(int shot, int frames, int after = -1);
    bool removePanel(int shot, int panel);
    bool setPanelLength(int shot, int panel, int frames);
    bool addLayer(int shot, int panel, QString const& name, BoardLayer::Kind kind);
    bool removeLayer(int shot, int panel, int layer);
    bool setLayerDrawing(int shot, int panel, int layer, Drawing const& drawing, QString const& label = {});
    bool setCaption(int shot, int panel, QString const& field, QString const& text);
    bool duplicateShot(int shot);
    bool splitShot(int shot, int firstPanel);
    bool duplicatePanel(int shot, int panel);
    bool splitPanel(int shot, int panel, int firstFrames);
    int moveShot(int source, int target, bool after);
    QPair<int, int> movePanel(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after);

signals:
    void projectChanged();

private:
    bool hasPanel(int shot, int panel) const;
    bool hasLayer(int shot, int panel, int layer) const;
    bool fitsFrames(int frames, int replacing = 0) const;
    bool appendPanelAllowed(int frames) const;
    bool canGrow(qsizetype panels, qint64 frames) const;
    QString uniqueShotName(QString const& base) const;
    void apply(BoardProject project, QString const& label);

    BoardProject m_project;
    QString m_path;
    QUndoStack m_undo;
};
