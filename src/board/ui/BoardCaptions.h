#pragma once

#include <QWidget>
#include <QStringList>
#include <QVector>

class BoardDocument;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QVBoxLayout;

class BoardCaptions : public QWidget {
    Q_OBJECT
public:
    explicit BoardCaptions(BoardDocument* document, QWidget* parent = nullptr);
    void showPanel(int shot, int panel);
    void setCustomPanelNames(bool enabled);
    bool writingCaption() const { return m_writing; }
    void editFields(bool add);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void rebuildFields(QStringList const& fields);
    void updatePanelLabel();
    void storeText(QPlainTextEdit* editor, QString const& field);
    BoardDocument* m_document;
    QLineEdit* m_sceneField;
    QLineEdit* m_panelField;
    QSpinBox* m_duration;
    QLabel* m_durationHint;
    QVBoxLayout* m_fieldsLayout;
    QStringList m_fields;
    QVector<QPlainTextEdit*> m_editors;
    QObject* m_editing = nullptr;
    quint64 m_session = 0;
    int m_shot = -1;
    int m_panel = -1;
    bool m_writing = false;
    bool m_customPanelNames = false;
};
