#pragma once

#include "EclipseExport.h"
#include "board/model/BoardDocument.h"

#include <QMainWindow>

class ActionManager;
class BoardStage;
class BoardThumbnails;
class QDockWidget;
class QMenu;

class ECLIPSE_BOARD_UI_API BoardWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BoardWindow(QWidget* parent = nullptr);
    bool openProject(QString const& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void showWelcome();
    void chooseProject();
    void showRecent(QMenu* menu);
    void restoreDefaultLayout();
    void saveProject();
    void editProjectSettings();
    void undo();
    void redo();
    void editArrangement(QString const& operation);
    void chooseStageTool(QString const& tool);
    void fitStage();
    void showThumbnails();

private:
    bool loadProject(QString const& path);
    bool saveChanges();
    bool confirmDiscard();
    void updateProjectState();
    void buildWorkspace();
    void refreshWorkspace();
    void selectPanel(int shot, int panel, bool fit = false);
    void updatePanelActions();
    void dropPanel(int sourceShot, int sourcePanel, int targetShot, int targetPanel, bool after, bool wholeScene);
    ActionManager* m_actions;
    BoardDocument m_document;
    QByteArray m_defaultLayout;
    BoardStage* m_stage = nullptr;
    BoardThumbnails* m_thumbnails = nullptr;
    QDockWidget* m_thumbnailDock = nullptr;
    QPair<int, int> m_selected{-1, -1};
};
