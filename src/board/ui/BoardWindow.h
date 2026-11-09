#pragma once

#include "EclipseExport.h"
#include "board/model/BoardDocument.h"

#include <QMainWindow>

class ActionManager;
class BoardCaptions;
class BoardLayers;
class BoardPlayback;
class BoardStage;
class BoardThumbnails;
class BoardTimeline;
class QDockWidget;
class QMenu;
class QListView;
class QTabWidget;

class ECLIPSE_BOARD_UI_API BoardWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BoardWindow(QWidget* parent = nullptr);
    bool openProject(QString const& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void showWelcome();
    void chooseProject();
    void showRecent(QMenu* menu);
    void restoreDefaultLayout();
    void switchWorkspace(QString const& name);
    void editPreferences();
    void editShortcuts();
    void saveProject();
    void saveProjectAs();
    void saveNewVersion();
    void closeProject();
    void regenerateThumbnails();
    void editProjectSettings();
    void undo();
    void redo();
    void editArrangement(QString const& operation);
    void chooseStageTool(QString const& tool);
    void fitStage();
    void showBoardView(QString const& view);
    void editCaptionFields(QString const& operation);
    void useTransport(QString const& operation);
    void loopPlayback(bool enabled);
    void editTiming(QString const& operation);
    void editSound(QString const& operation);
    void importBoardImages(QString const& operation);
    void exportBoardFrame();
    void exportBoard(QString const& operation);
    void editCamera(QString const& operation);
    void showCameraPreview(bool show);
    void showCameraGuide(bool show);

private:
    bool loadProject(QString const& path);
    bool saveChanges();
    void saveProjectCopy(QString const& path);
    void rememberProjectPath();
    bool confirmDiscard();
    void updateProjectState();
    void buildWorkspace();
    void sizeReferenceLayout();
    void loadWorkspaceSettings();
    void saveWorkspaceSettings();
    void loadWorkspace();
    void applyWorkspacePreset();
    void applyPreferences();
    int defaultPanelFrames(double fps) const;
    void refreshWorkspace();
    void selectPanel(int shot, int panel, bool fit = false, bool syncTiming = true);
    void updatePanelActions();
    void dropPanel(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after, bool wholeScene);
    void buildTimeline();
    void showPlaybackFrame(int frame);
    void updatePlaybackActions();
    void navigateBoard(QString const& operation);
    void updateCameraActions();
    void importBoardSound();
    ActionManager* m_actions;
    BoardDocument m_document;
    QByteArray m_defaultLayout;
    BoardStage* m_stage = nullptr;
    BoardThumbnails* m_thumbnails = nullptr;
    BoardCaptions* m_captions = nullptr;
    BoardLayers* m_layers = nullptr;
    BoardTimeline* m_timeline = nullptr;
    BoardPlayback* m_playback = nullptr;
    QDockWidget* m_thumbnailDock = nullptr;
    QDockWidget* m_captionDock = nullptr;
    QDockWidget* m_layerDock = nullptr;
    QDockWidget* m_timelineDock = nullptr;
    QTabWidget* m_inspectorTabs = nullptr;
    QListView* m_outline = nullptr;
    bool m_cameraPreview = false;
    bool m_exporting = false;
    bool m_sizeDefaultOnMaximize = false;
    QString m_workspace = QStringLiteral("horizontal");
    QPair<int, int> m_selected{-1, -1};
};
