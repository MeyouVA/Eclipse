#pragma once

#include <QWidget>

class BoardFormatPicker;
class QLineEdit;
class QListWidget;

class BoardWelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit BoardWelcomePage(QString const& resources, QWidget* parent);

signals:
    void projectChosen(QString const& path);

private:
    void createProject();
    void openProject(QString const& path);

    QLineEdit* m_name;
    QLineEdit* m_directory;
    QLineEdit* m_title;
    QLineEdit* m_subtitle;
    BoardFormatPicker* m_format;
};
