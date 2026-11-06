// Eclipse - the storyboard of a scene: its shots (in sequences and acts), each a run of panels with their captions.
//
// The shots follow one another from `start`: a panel lasts so many frames, a shot as long as its panels.  An animatic
// read in from an editing program fills it (a shot for each clip); the Storyboard view edits it; the storyboard PDF
// prints it, a panel a picture of the frame it starts on with its captions under or beside it.
#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

struct Storyboard {
    struct Caption {
        QString field;          // which of `captionFields` it is ("Dialog", "Action Notes" ...)
        QString text;
        bool operator==(Caption const&) const = default;
    };
    struct Panel {
        QString name;           // its own name (kept only where panels may be named: Naming::customPanelNames)
        int length = 24;        // frames
        QVector<Caption> captions;
        QString image;          // a picture it was made from (an animatic's frame), when there is one
        bool operator==(Panel const&) const = default;
    };
    struct Shot {
        QString name;
        QString sequence;
        QString act;
        QVector<Panel> panels;
        // How it begins after the shot before: a cut, or a transition ("Dissolve", "Wipe" ...) over so many frames.
        QString transition;
        int transitionLength = 0;
        QString clip;           // the editing program's clip it came from, when it did
        int length() const;
        bool operator==(Shot const&) const = default;
    };
    // How panels and shots are named (Preferences > Storyboard).
    struct Naming {
        bool customPanelNames = false;  // Allow User to Name Panels: a panel's own name, else the automatic one
        enum PanelRule { Numbers, Letters, ShotAndNumber };
        int panelRule = Numbers;        // Rule For Panel Naming: 1, 2, 3 / A, B, C / 12-1, 12-2
        bool showTotal = true;          // Display Total in Automatic Panel Name: 2/3
        bool shotLeadingZeros = false;  // Automatically Add Leading Zeros When Naming Shots ("5" -> "05")
        int shotZeros = 2;              // Number of Characters to Use for Automatic Leading Zeros
        bool acts = false;              // Enable Act: shots grouped into acts too
    };

    QVector<Shot> shots;
    int start = 0;                      // the scene's frame the first shot starts on
    QString title, subtitle;            // Project Title / Project Subtitle (printed in the PDF's headers and on its cover)
    QStringList captionFields = defaultCaptionFields();
    static QStringList defaultCaptionFields();

    bool isEmpty() const { return shots.isEmpty(); }
    int length() const;                 // frames, every shot
    int panelCount() const;
    int shotStart(int shot) const;
    int panelStart(int shot, int panel) const;
    // The shot a frame is in (-1: before or after them all), and the panel too (-1, -1).
    int shotAt(int frame) const;
    QPair<int, int> panelAt(int frame) const;

    // What a panel is called: its own name, or the automatic one (by the rule; with the total when it says).
    QString panelName(int shot, int panel, Naming const& naming) const;
    // A shot's name as typed, with leading zeros put in when the naming says ("5A" -> "05A").
    static QString shotName(QString const& typed, Naming const& naming);
    // Every shot named again in order: first, first + step ... (leading zeros as the naming says).
    void renumberShots(Naming const& naming, int first = 1, int step = 1);
    // The names of the sequences and acts, in the order they first come.
    QStringList sequences() const;
    QStringList acts() const;

    // ---- changes ----
    // A shot of `frames` frames (one panel) put in before shot `at` (-1: after the last); its place.
    int addShot(int at, int frames, QString const& name = QString());
    // A new panel at `frame`: the panel there split in two (false: no panel there, or the frame is its first already).
    bool splitPanel(int frame);
    // A new shot from `frame` on: the shot there cut in two between panels (false: not at a panel's start).
    bool splitShot(int frame);
    // The shot taken into the one before it (its panels after that shot's).
    bool joinShot(int shot);
    void removeShot(int shot);
    void removePanel(int shot, int panel);
    // A panel made `frames` long (at least one); what follows moves with it.
    void setPanelLength(int shot, int panel, int frames);
    QString caption(int shot, int panel, QString const& field) const;
    void setCaption(int shot, int panel, QString const& field, QString const& text);
    // Made `frames` long by stretching or shortening its last panel (shots dropped when they are past the end).
    void fitTo(int frames);

    // ---- out ----
    // Every caption, a block a panel ("Shot 1, Panel 2" then "Dialog: ..."); with `fields`, only those.
    QString captionsText(QStringList const& fields = {}, Naming const& naming = {}) const;
    // A row a panel: act, sequence, shot, panel, start, duration and a column for each caption field.
    QByteArray captionsCsv(double fps, bool timecode, Naming const& naming = {}) const;
    // A duration as frames ("36") or as a time code ("00:00:01:12").
    static QString duration(int frames, double fps, bool timecode);
    QJsonObject toJson() const;
    static Storyboard fromJson(QJsonObject const& json);
    bool operator==(Storyboard const&) const = default;
};
