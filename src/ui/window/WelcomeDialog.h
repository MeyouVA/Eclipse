// Eclipse - the window shown when the program starts: make a new scene of a chosen camera
// size, or open one worked on lately.
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

class ECLIPSE_UI_API WelcomeDialog : public QDialog {
    Q_OBJECT

public:
    enum Choice { Nothing, CreateScene, OpenScene };

    explicit WelcomeDialog(QString const& resourceDirectory, QWidget* parent = nullptr, bool newSceneOnly = false);

    // What the user asked for, once the window has closed.
    Choice choice() const { return m_choice; }
    // The file of the scene to open, or of the scene to make.
    QString path() const { return m_path; }
    Resolution resolution() const;

    // The sizes that come with the program, then the user's own.
    static QVector<Resolution> resolutions(QString const& resourceDirectory, QString* defaultName = nullptr);

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

    QLineEdit* m_name;
    QComboBox* m_location;
    QComboBox* m_size;
    QToolButton* m_remove;
    QLineEdit* m_width;
    QLineEdit* m_height;
    QLineEdit* m_fps;
    QLineEdit* m_fov;
    QListWidget* m_recent;
};
