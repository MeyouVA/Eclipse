// Eclipse - Storyboard: the scene's shots and their panels, their timing and captions.
//
// A row a shot, its panels under it; the panel the current frame is in is marked in the Current Panel colour, the one
// picked in the Selected Panel colour.  Picking a panel goes to its first frame.  Shots and panels are made, split,
// joined and deleted here; a panel's captions are written under the list, its picture shown beside them.
#pragma once

#include "EclipseExport.h"

#include "model/scene/Storyboard.h"

#include <QWidget>

#include <functional>

class Document;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class ECLIPSE_UI_API StoryboardView : public QWidget {
    Q_OBJECT

public:
    explicit StoryboardView(Document* document, QWidget* parent = nullptr);
    // How shots and panels are named, and whether durations show as time codes (Preferences > Storyboard).
    static Storyboard::Naming naming();
    static bool timecodeDurations();
    // Preferences > Storyboard: the page, and what keeps its choices (called when the preferences are accepted).
    static QGroupBox* preferencesPage(QWidget* parent, std::function<void()>* save);
    // The shot and panel picked in the list (-1, -1: none).
    QPair<int, int> picked() const;
    void pick(int shot, int panel);
    QTreeWidget* list() const { return m_tree; }

    // ---- the commands (the buttons call them; one undo step each) ----
    void newShot();                 // after the one picked (or at the end), as long as a second
    bool newPanel();                // the panel at the current frame split there
    bool splitShot();               // a new shot from the current frame (at a panel's start)
    bool joinShot();                // the picked shot into the one before
    void deletePicked();
    void renumberShots();
    void fitToScene();              // the shots made to end with the scene
    void setCaption(QString const& field, QString const& text);

public slots:
    void setCurrentCell(int layer, int frame);

signals:
    void frameRequested(int frame);

private:
    void rebuild();
    void edit(QString const& text, std::function<void(Storyboard&)> const& change);
    void showCaption();
    void refreshThumbnail();
    Document* m_document;
    int m_frame = 0;
    QLineEdit* m_title;
    QLineEdit* m_subtitle;
    QTreeWidget* m_tree;
    QComboBox* m_field;
    QPlainTextEdit* m_caption;
    QLabel* m_thumbnail;
    QTimer* m_thumbnailTimer;
    bool m_rebuilding = false;
};
