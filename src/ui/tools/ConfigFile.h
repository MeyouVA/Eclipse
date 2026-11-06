// Eclipse - a configuration file of the programs beside Eclipse (manager.conf, vectorize.conf, services.conf): sections in
// [brackets], "key = value" lines, # for notes. Read and written again as it was, notes and order kept, with only the
// values changed.
#pragma once

#include "EclipseExport.h"

#include <QString>
#include <QStringList>
#include <QVector>

class ECLIPSE_UI_API ConfigFile {
public:
    ConfigFile() = default;
    explicit ConfigFile(QString const& path) { load(path); }

    bool load(QString const& path, QString* error = nullptr);
    // Written where it was read (or `path`); false with `error`.
    bool save(QString const& path = QString(), QString* error = nullptr);
    void parse(QString const& text);
    QString text() const;
    QString path() const { return m_path; }
    bool isEmpty() const;

    QStringList sections() const;
    QStringList keys(QString const& section) const;
    bool contains(QString const& section, QString const& key) const;
    QString value(QString const& section, QString const& key, QString const& fallback = QString()) const;
    int intValue(QString const& section, QString const& key, int fallback) const;
    bool boolValue(QString const& section, QString const& key, bool fallback) const;
    // Changed where it is, else added at the end of its section (made at the end when there is none).
    void setValue(QString const& section, QString const& key, QString const& value);
    bool remove(QString const& section, QString const& key);
    void removeSection(QString const& section);
    // The note lines just above a key (without the #).
    QString note(QString const& section, QString const& key) const;

    // The defaults of the programs beside Eclipse: resources/manager.conf.
    static QString managerFile();
    static QString manager(QString const& section, QString const& key, QString const& fallback = QString());

private:
    struct Line {
        QString raw;
        QString section;
        QString key, value;
        enum Kind { Other, Section, Key } kind = Other;
    };
    int find(QString const& section, QString const& key) const;
    QVector<Line> m_lines;
    QString m_path;
};
