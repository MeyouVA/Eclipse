// Eclipse - usage statistics: anonymous counts of how Eclipse is used - the commands run, the views shown, the sessions
// and how long they last, events and custom values - kept on this computer only (usage.json in the programs' shared
// folder), and only when the user says so (Help > Usage Statistics...). Nothing is sent anywhere. The Usage Statistics
// Tester (Debug Mode) records hits by hand and shows them.
#pragma once

#include "EclipseExport.h"

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QVector>

class QCheckBox;
class QLabel;
class QTableWidget;

class ECLIPSE_UI_API UsageStats {
public:
    struct Hit {
        QDateTime when;
        QString kind;       // "event", "screen", "dimension", "metric", "session"
        QString category, action, label;
        QString value;
    };

    static UsageStats& instance();
    // Where they are kept; whether they are kept at all (off to begin with).
    static QString file();
    static bool enabled();
    static void setEnabled(bool on);

    void command(QString const& id);
    void screen(QString const& name);
    void event(QString const& category, QString const& action, QString const& label = QString(), double value = 0);
    void setDimension(int index, QString const& value);
    void setMetric(int index, double value);
    void startSession();
    void endSession();
    bool inSession() const { return m_session.isValid(); }

    QHash<QString, int> commands() const { return m_commands; }
    QHash<QString, int> screens() const { return m_screens; }
    int sessions() const { return m_sessions; }
    qint64 seconds() const { return m_seconds; }
    QVector<Hit> hits() const { return m_hits; }
    QHash<int, QString> dimensions() const { return m_dimensions; }
    QHash<int, double> metrics() const { return m_metrics; }

    // Everything as kept (and shown before it is cleared or saved elsewhere).
    QJsonObject toJson() const;
    void clear();
    bool save() const;
    void reload();

private:
    UsageStats();
    void hit(Hit const& h);
    QHash<QString, int> m_commands, m_screens;
    QHash<int, QString> m_dimensions;
    QHash<int, double> m_metrics;
    QVector<Hit> m_hits;
    int m_sessions = 0;
    qint64 m_seconds = 0;
    QDateTime m_session;
};

// Help > Usage Statistics...: whether they are kept, what is kept (the commands used most, the views, the sessions), and
// Clear / Save As....
class ECLIPSE_UI_API UsageStatsDialog : public QDialog {
    Q_OBJECT

public:
    explicit UsageStatsDialog(QWidget* parent = nullptr);
    void refresh();

private:
    QCheckBox* m_enabled;
    QTableWidget* m_table;
    QLabel* m_summary;
};

// The Usage Statistics Tester (Help menu, in Debug Mode): Custom Dimension and Custom Metric (Index, Value, Set), Event
// Tracking (category, Action, Label, Value, Send Event Hit), Screen Name (Send Screen View), Session (Start, End), and the
// hits recorded.
class ECLIPSE_UI_API UsageTesterDialog : public QDialog {
    Q_OBJECT

public:
    explicit UsageTesterDialog(QWidget* parent = nullptr);
    void refresh();

private:
    QTableWidget* m_hits;
};
