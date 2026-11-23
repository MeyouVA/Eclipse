#pragma once

#include "EclipseExport.h"
#include "launcher/WelcomeDialog.h"

#include <QObject>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QToolButton;
class QWidget;

class ECLIPSE_LAUNCHER_API BoardFormatPicker : public QObject {
    Q_OBJECT
public:
    BoardFormatPicker(QString const& resources, QWidget* parent);

    QWidget* presetRow() const { return m_presetRow; }
    QSpinBox* width() const { return m_width; }
    QSpinBox* height() const { return m_height; }
    QDoubleSpinBox* fps() const { return m_fps; }
    QLabel* aspect() const { return m_aspect; }
    void setFormat(int width, int height, double fps);
    void selectDefault();

    static QVector<Resolution> resolutions(QString const& resources, QString* defaultName = nullptr);
    static bool addCustom(QString const& resources, Resolution const& size, QString* error = nullptr);
    static bool removeCustom(QString const& name);

signals:
    void changed();

private:
    void fill(QString const& select);
    void choose(int index);
    void refresh();
    void matchPreset();
    void addPreset();
    void removePreset();

    QString m_resources;
    QString m_defaultName;
    QVector<Resolution> m_sizes;
    QWidget* m_presetRow;
    QComboBox* m_presets;
    QToolButton* m_add;
    QToolButton* m_remove;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QDoubleSpinBox* m_fps;
    QLabel* m_aspect;
};
