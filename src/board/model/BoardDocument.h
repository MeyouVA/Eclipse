#pragma once

#include "board/model/BoardProject.h"

#include <QObject>
#include <QSet>
#include <QUndoStack>

class ECLIPSE_BOARD_MODEL_API BoardDocument : public QObject {
    Q_OBJECT
public:
    explicit BoardDocument(QObject* parent = nullptr);
    BoardProject const& project() const { return m_project; }
    QUndoStack* undoStack() { return &m_undo; }
    QString const& path() const { return m_path; }
    BoardPanel const* panel(int shot, int panel) const;

    bool open(QString const& path, QString* error = nullptr);
    void close();
    bool save(QString* error = nullptr);
    bool saveAs(QString const& path, QString* error = nullptr);
    QString extractProject(int first, int last, QString const& name, QString const& directory, QString* error = nullptr) const;
    QString splitProject(int firstSceneOfSecondPart, QString const& name, QString const& directory, QString* error = nullptr) const;
    bool insertProject(BoardProject source, QString const& sourceFile, int after, bool useImportedSound, QString* error = nullptr);
    bool setSceneDetails(int scene, QString const& name, QString const& sequence, QString const& act);
    bool setPanelName(int scene, int panel, QString const& name);
    bool setSceneNamesLocked(int scene, bool locked);
    bool renameSceneGroup(int scene, BoardProject::SceneGroup group, QString const& name);
    bool setTitles(QString const& title, QString const& subtitle);
    bool addShot(QString const& name, int frames, int after = -1);
    bool removeShot(int shot);
    bool addPanel(int shot, int frames, int after = -1);
    bool removePanel(int shot, int panel);
    bool setPanelLength(int shot, int panel, int frames);
    bool setDissolve(int shot, int frames);
    bool setCameraKey(int shot, int panel, int frame, BoardCameraPose const& pose);
    bool removeCameraKey(int shot, int panel, int frame);
    bool setSound(BoardSound const& sound);
    bool addLayer(int shot, int panel, QString const& name, BoardLayer::Kind kind, int before = -1);
    bool removeLayer(int shot, int panel, int layer);
    bool duplicateLayer(int shot, int panel, int layer);
    bool renameLayer(int shot, int panel, int layer, QString const& name);
    bool moveLayer(int shot, int panel, int layer, int to);
    bool setLayerFlags(int shot, int panel, int layer, bool visible, bool locked);
    bool setLayerDrawing(int shot, int panel, int layer, Drawing const& drawing, QString const& label = {});
    bool importPictures(int shot, int panel, QVector<QPair<QString, QImage>> const& images, int framesPerScene = 0);
    bool setCaption(int shot, int panel, QString const& field, QString const& text, quint64 session = 0);
    bool addCaptionField(QString const& field);
    bool removeCaptionField(QString const& field);
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
    QString writeSceneRange(int first, int last, QString const& name, QString const& directory, QString* error) const;
    QString uniqueShotName(QString const& base) const;
    QSet<QString> soundAssets() const;
    void resetProject(BoardProject project, QString path);
    void apply(BoardProject project, QString const& label, quint64 session = 0);

    BoardProject m_project;
    QString m_path;
    QUndoStack m_undo;
};
