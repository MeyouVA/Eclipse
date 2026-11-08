// Eclipse - Check for Updates (see UpdateCheck.h).
#include "animation/ui/help/UpdateCheck.h"

#include "animation/ui/help/HelpCenter.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace UpdateCheck {

namespace {

QSettings settings() { return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")); }

QNetworkRequest request(QUrl const& url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eclipse/%1").arg(currentVersion()));
    r.setRawHeader("Accept", "application/json, */*");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

}  // namespace

QString currentVersion()
{
    QString const v = QApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("0.3.0") : v;
}

QUrl feed()
{
    return QUrl(HelpCenter::link(QStringLiteral("updates")));
}

int compareVersions(QString const& a, QString const& b)
{
    auto parts = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            v = v.mid(1);
        QVector<int> out;
        for (QString const& p : v.split(QRegularExpression(QStringLiteral("[.\\-+]"))))
            out << p.toInt();
        while (out.size() > 1 && out.last() == 0)
            out.removeLast();
        return out;
    };
    QVector<int> const x = parts(a), y = parts(b);
    for (qsizetype i = 0; i < std::max(x.size(), y.size()); ++i) {
        int const p = x.value(i), q = y.value(i);
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}

bool parse(QByteArray const& json, Release& out, QString* error)
{
    QJsonParseError problem;
    QJsonDocument const doc = QJsonDocument::fromJson(json, &problem);
    QJsonObject o = doc.object();
    // A list of releases: the first that is not a draft or a pre-release.
    if (doc.isArray())
        for (QJsonValue const& v : doc.array())
            if (!v.toObject()[QStringLiteral("draft")].toBool() && !v.toObject()[QStringLiteral("prerelease")].toBool()) {
                o = v.toObject();
                break;
            }
    if (o.isEmpty()) {
        if (error)
            *error = QObject::tr("The update feed's answer could not be read (%1).").arg(problem.error == QJsonParseError::NoError ? QObject::tr("no release in it") : problem.errorString());
        return false;
    }
    out = Release();
    QString version = o[QStringLiteral("tag_name")].toString();
    if (version.isEmpty())
        version = o[QStringLiteral("version")].toString();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        version = version.mid(1);
    out.version = version;
    out.name = o[QStringLiteral("name")].toString(version);
    out.notes = o.contains(QStringLiteral("body")) ? o[QStringLiteral("body")].toString() : o[QStringLiteral("notes")].toString();
    out.page = QUrl(o.contains(QStringLiteral("html_url")) ? o[QStringLiteral("html_url")].toString() : o[QStringLiteral("page")].toString());
    out.published = QDateTime::fromString(o[QStringLiteral("published_at")].toString(), Qt::ISODate);
    QString download = o[QStringLiteral("download")].toString();
    // GitHub: the release's zip for Windows, else its first file.
    QJsonArray const assets = o[QStringLiteral("assets")].toArray();
    for (QJsonValue const& a : assets) {
        QString const name = a.toObject()[QStringLiteral("name")].toString().toLower();
        if (download.isEmpty() && name.endsWith(QLatin1String(".zip")) && (name.contains(QLatin1String("win")) || assets.size() == 1))
            download = a.toObject()[QStringLiteral("browser_download_url")].toString();
    }
    if (download.isEmpty() && !assets.isEmpty())
        download = assets.first().toObject()[QStringLiteral("browser_download_url")].toString();
    out.download = QUrl(download);
    if (out.version.isEmpty()) {
        if (error)
            *error = QObject::tr("The update feed's answer has no version.");
        return false;
    }
    return true;
}

bool fetch(QUrl const& url, Release& out, QString* error, int timeoutMs, std::function<bool()> const& cancel)
{
    if (!url.isValid() || url.isEmpty()) {
        if (error)
            *error = QObject::tr("No update feed is set (resources/links.conf, [links] updates).");
        return false;
    }
    QNetworkAccessManager network;
    QNetworkReply* const reply = network.get(request(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer tick;
    QElapsedTimer waited;
    waited.start();
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (waited.elapsed() > timeoutMs || (cancel && cancel()))
            reply->abort();
    });
    tick.start(50);
    if (!reply->isFinished())
        loop.exec();
    QByteArray const bytes = reply->readAll();
    bool const ok = reply->error() == QNetworkReply::NoError;
    QString const why = waited.elapsed() > timeoutMs ? QObject::tr("The update feed did not answer in time.") : reply->errorString();
    reply->deleteLater();
    if (!ok) {
        if (error)
            *error = why;
        return false;
    }
    return parse(bytes, out, error);
}

bool download(QUrl const& url, QString const& path, QString* error, std::function<bool(qint64, qint64)> const& progress)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QNetworkAccessManager network;
    QNetworkReply* const reply = network.get(request(url));
    QEventLoop loop;
    bool stopped = false;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] { file.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 done, qint64 total) {
        if (progress && !progress(done, total)) {
            stopped = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();
    file.write(reply->readAll());
    bool const ok = reply->error() == QNetworkReply::NoError && !stopped;
    QString const why = stopped ? QObject::tr("The download was stopped.") : reply->errorString();
    reply->deleteLater();
    if (!ok) {
        file.cancelWriting();
        if (error)
            *error = why;
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool automatic()
{
    return settings().value(QStringLiteral("prefs/autoUpdateCheck"), false).toBool();
}

void setAutomatic(bool on)
{
    settings().setValue(QStringLiteral("prefs/autoUpdateCheck"), on);
}

bool dueToday()
{
    QDateTime const last = settings().value(QStringLiteral("updates/lastCheck")).toDateTime();
    return !last.isValid() || last.secsTo(QDateTime::currentDateTime()) > 24 * 3600;
}

void noteChecked()
{
    settings().setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentDateTime());
}

}  // namespace UpdateCheck

UpdateAvailableDialog::UpdateAvailableDialog(UpdateCheck::Release const& release, QWidget* parent)
    : QDialog(parent), m_release(release), m_folder(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
{
    setObjectName(QStringLiteral("UpdateAvailableDialog"));
    setWindowTitle(tr("Update Available"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    QLabel* const title = new QLabel(tr("<b>An update to %1 is available.</b>").arg(QApplication::applicationName()), this);
    title->setObjectName(QStringLiteral("UpdateTitle"));
    layout->addWidget(title);
    layout->addWidget(new QLabel(tr("New version: %1").arg(release.version), this));
    layout->addWidget(new QLabel(tr("Current version: %1").arg(UpdateCheck::currentVersion()), this));
    layout->addWidget(new QLabel(tr("Release notes:"), this));
    QTextBrowser* const notes = new QTextBrowser(this);
    notes->setObjectName(QStringLiteral("UpdateNotes"));
    notes->setOpenExternalLinks(true);
    notes->setMarkdown(release.notes.isEmpty() ? tr("(This release has no notes.)") : release.notes);
    layout->addWidget(notes, 1);
    if (release.page.isValid() && !release.page.isEmpty()) {
        QLabel* const whatsNew = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>").arg(release.page.toString(), tr("Click here to see what’s new in this update.")), this);
        whatsNew->setObjectName(QStringLiteral("UpdateWhatsNew"));
        whatsNew->setOpenExternalLinks(true);
        layout->addWidget(whatsNew);
    }
    m_automatic = new QCheckBox(tr("Automatically Check for Updates"), this);
    m_automatic->setObjectName(QStringLiteral("UpdateAutomatic"));
    m_automatic->setChecked(UpdateCheck::automatic());
    connect(m_automatic, &QCheckBox::toggled, this, [](bool on) { UpdateCheck::setAutomatic(on); });
    layout->addWidget(m_automatic);
    QDialogButtonBox* const buttons = new QDialogButtonBox(this);
    QPushButton* const get = buttons->addButton(tr("Download Update..."), QDialogButtonBox::AcceptRole);
    get->setObjectName(QStringLiteral("UpdateDownload"));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(get, &QPushButton::clicked, this, &UpdateAvailableDialog::downloadUpdate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(520, 420);
}

void UpdateAvailableDialog::downloadUpdate()
{
    // Without a file to download, the release's page is opened.
    if (!m_release.download.isValid() || m_release.download.isEmpty()) {
        if (m_release.page.isValid())
            QDesktopServices::openUrl(m_release.page);
        accept();
        return;
    }
    QString name = QFileInfo(m_release.download.path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("Eclipse-%1.zip").arg(m_release.version);
    QString const path = QDir(m_folder).filePath(name);
    QProgressDialog progress(tr("Downloading %1...").arg(name), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("UpdateDownloadProgress"));
    progress.setWindowTitle(tr("Download Update"));
    progress.setMinimumDuration(300);
    bool const ok = UpdateCheck::download(m_release.download, path, &m_error, [&progress](qint64 done, qint64 total) {
        progress.setMaximum(total > 0 ? 1000 : 0);
        if (total > 0)
            progress.setValue(int(done * 1000 / total));
        return !progress.wasCanceled();
    });
    progress.close();
    if (!ok)
        return;
    m_downloaded = path;
    // Shown in its folder; it is never run from here.
    if (QApplication::platformName() != QLatin1String("offscreen"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    accept();
}
