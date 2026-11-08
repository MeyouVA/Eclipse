// The startup chooser and the in-app project forms.
#pragma once

#include "EclipseExport.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

// A named camera size.
struct Resolution {
    QString name;
    int width = 1920;
    int height = 1080;
    double fps = 24.0;
    bool custom = false;  // made by the user, kept with the settings
    double fov = 41.112;  // the angle of view of the camera (degrees, top to bottom - or, `across`, side to side)
    bool across = false;  // horizontal fit
    bool orthographic = false;
};

class ECLIPSE_LAUNCHER_API WelcomeDialog : public QDialog {
    Q_OBJECT

public:
    enum Choice { Nothing, CreateScene, OpenScene, ShowHelp, OpenBoardProject };
    enum class Mode { Animation, NewScene, Launcher, Storyboard };

    explicit WelcomeDialog(QString const& resourceDirectory, QWidget* parent = nullptr, bool newSceneOnly = false);
    WelcomeDialog(QString const& resourceDirectory, Mode mode, QWidget* parent = nullptr);

    // What the user asked for, once the window has closed.
    Choice choice() const { return m_choice; }
    // The file of the scene to open, or of the scene to make.
    QString path() const { return m_path; }
    // Get Help / Getting Started: the help page asked for (with ShowHelp).
    QString helpTopic() const { return m_helpTopic; }
    Resolution resolution() const;

    // The sizes that come with the program, then the user's own.
    static QVector<Resolution> resolutions(QString const& resourceDirectory, QString* defaultName = nullptr, bool includeCustom = true);

private:
    void fillResolutions(QString const& select);
    void showResolution();
    void addResolution();
    void removeResolution();
    void browse();
    void create();
    void openOther();
    void openRecent(int row);

    QString m_resources;
    QVector<Resolution> m_resolutions;
    Choice m_choice = Nothing;
    QString m_path;
    QString m_helpTopic;

    QLineEdit* m_name = nullptr;
    QComboBox* m_location = nullptr;
    QComboBox* m_size = nullptr;
    QToolButton* m_remove = nullptr;
    QLineEdit* m_width = nullptr;
    QLineEdit* m_height = nullptr;
    QLineEdit* m_fps = nullptr;
    QLineEdit* m_fov = nullptr;
    QListWidget* m_recent = nullptr;
};
