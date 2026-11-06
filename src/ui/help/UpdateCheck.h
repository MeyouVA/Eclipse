// Eclipse - Check for Updates: the latest release read from the feed of resources/links.conf (the project's releases, as
// GitHub gives them, or a plain {version, notes, page, download}), compared with this version; the Update Available window
// (its release notes, what's new, Download Update...), and the download itself - into a folder, shown there, never run.
// Checked when asked, and when Eclipse starts if Automatically Check for Updates is on (at most once a day).
#pragma once

#include "EclipseExport.h"

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QUrl>

#include <functional>

class QCheckBox;

namespace UpdateCheck {

struct Release {
    QString version;    // "0.3.0" (a leading v taken off)
    QString name;
    QString notes;      // Markdown
    QUrl page;          // what's new in it
    QUrl download;      // the file to download (empty: the page)
    QDateTime published;
};

// A feed's answer read; false, with `error`, when it is not one.
ECLIPSE_UI_API bool parse(QByteArray const& json, Release& out, QString* error = nullptr);
// -1, 0 or 1 as a is older than, the same as, or newer than b ("1.2" = "1.2.0", "v0.10" > "0.9").
ECLIPSE_UI_API int compareVersions(QString const& a, QString const& b);
ECLIPSE_UI_API QString currentVersion();
// The feed: links.conf's [links] updates.
ECLIPSE_UI_API QUrl feed();

// The feed read (waiting at most `timeoutMs`, `cancel` asked as it waits); false, with `error`.
ECLIPSE_UI_API bool fetch(QUrl const& url, Release& out, QString* error = nullptr, int timeoutMs = 15000, std::function<bool()> const& cancel = {});
// A file downloaded to `path`; `progress` told the bytes (false from it stops). False, with `error`.
ECLIPSE_UI_API bool download(QUrl const& url, QString const& path, QString* error = nullptr, std::function<bool(qint64 done, qint64 total)> const& progress = {});

// Preferences: Automatically Check for Updates, and when it last checked.
ECLIPSE_UI_API bool automatic();
ECLIPSE_UI_API void setAutomatic(bool on);
ECLIPSE_UI_API bool dueToday();
ECLIPSE_UI_API void noteChecked();

}  // namespace UpdateCheck

// "Update Available": the new version and this one, the release notes, a link to what's new, Automatically Check for
// Updates, and Download Update... / Cancel.
class ECLIPSE_UI_API UpdateAvailableDialog : public QDialog {
    Q_OBJECT

public:
    UpdateAvailableDialog(UpdateCheck::Release const& release, QWidget* parent = nullptr);
    // Download Update... was pressed: where it went (empty: it was not, or it failed - `error()` says why).
    QString downloaded() const { return m_downloaded; }
    QString error() const { return m_error; }
    // Where a download goes (for checks; the user's Downloads folder to begin with).
    void setFolder(QString const& folder) { m_folder = folder; }

public slots:
    void downloadUpdate();

private:
    UpdateCheck::Release m_release;
    QCheckBox* m_automatic;
    QString m_folder;
    QString m_downloaded;
    QString m_error;
};
