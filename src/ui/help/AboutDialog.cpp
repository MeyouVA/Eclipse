// Eclipse - the About box, documents and Report an Issue (see AboutDialog.h).
#include "ui/help/AboutDialog.h"

#include "actions/manager/ActionManager.h"
#include "ui/help/HelpCenter.h"
#include "ui/tools/ToolInfo.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace AboutText {

QString folder()
{
    return ActionManager::defaultDirectory() + QStringLiteral("/about");
}

QString document(QString const& name)
{
    static QHash<QString, QString> const files{{QStringLiteral("readme"), QStringLiteral("README.md")},
                                              {QStringLiteral("license"), QStringLiteral("LICENSE")},
                                              {QStringLiteral("credits"), QStringLiteral("credits.md")},
                                              {QStringLiteral("notices"), QStringLiteral("notices.md")}};
    QFile file(QDir(folder()).filePath(files.value(name, name)));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    // Pictures the Read Me shows on the web are not beside the program.
    if (name == QLatin1String("readme"))
        text.remove(QRegularExpression(QStringLiteral("!\\[[^\\]]*\\]\\([^)]*\\)")));
    return text;
}

QStringList credits()
{
    QStringList out;
    for (QString const& line : document(QStringLiteral("credits")).split(QLatin1Char('\n'))) {
        QStringList const cells = line.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        if (cells.size() == 3 && !line.contains(QLatin1String("---")) && cells[0].trimmed() != QLatin1String("Name"))
            out << QStringLiteral("%1 - %2 (%3)").arg(cells[0].trimmed(), cells[1].trimmed(), cells[2].trimmed());
    }
    return out;
}

QString systemReport()
{
    QStringList lines = ToolInfo::versionLines();
    lines << ToolInfo::systemLines();
    return lines.join(QLatin1Char('\n'));
}

}  // namespace AboutText

QLabel* hyperlinkLabel(QString const& text, QString const& linkName, QWidget* parent)
{
    QString const url = HelpCenter::link(linkName);
    QLabel* const label = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped()), parent);
    label->setObjectName(QStringLiteral("Link_") + linkName);
    label->setToolTip(url);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    label->setVisible(!url.isEmpty());
    return label;
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("AboutDialog"));
    setWindowTitle(tr("About Eclipse"));
    QHBoxLayout* const top = new QHBoxLayout;
    QLabel* const logo = new QLabel(this);
    logo->setPixmap(QPixmap(ActionManager::defaultDirectory() + QStringLiteral("/eclipse.png")).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    top->addWidget(logo, 0, Qt::AlignTop);
    QVBoxLayout* const words = new QVBoxLayout;
    QLabel* const name = new QLabel(tr("<h2>Eclipse %1</h2>2D animation: hand-drawn and cut-out.").arg(QApplication::applicationVersion()), this);
    name->setObjectName(QStringLiteral("AboutName"));
    words->addWidget(name);
    QLabel* const build = new QLabel(tr("Built %1 with Qt %2, running on Qt %3, %4.")
                                         .arg(QStringLiteral(__DATE__), QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion()), QSysInfo::prettyProductName()),
                                     this);
    build->setObjectName(QStringLiteral("AboutBuild"));
    build->setWordWrap(true);
    words->addWidget(build);
    QString const licence = AboutText::document(QStringLiteral("license")).section(QLatin1Char('\n'), 0, 2).simplified();
    QLabel* const licenceLine = new QLabel(licence.isEmpty() ? tr("zlib License") : licence, this);
    licenceLine->setObjectName(QStringLiteral("AboutLicence"));
    licenceLine->setWordWrap(true);
    words->addWidget(licenceLine);
    QLabel* const uses = new QLabel(tr("It uses: %1.").arg(AboutText::credits().join(QStringLiteral("; "))), this);
    uses->setObjectName(QStringLiteral("AboutCredits"));
    uses->setWordWrap(true);
    words->addWidget(uses);
    QHBoxLayout* const links = new QHBoxLayout;
    links->addWidget(hyperlinkLabel(tr("Website"), QStringLiteral("website"), this));
    links->addWidget(hyperlinkLabel(tr("Releases"), QStringLiteral("releases"), this));
    links->addWidget(hyperlinkLabel(tr("Report an Issue"), QStringLiteral("issues"), this));
    links->addStretch(1);
    words->addLayout(links);
    top->addLayout(words, 1);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addLayout(top);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto document = [this, buttons](QString const& text, QString const& title, QString const& doc, bool markdown) {
        QPushButton* const b = buttons->addButton(text, QDialogButtonBox::ActionRole);
        b->setObjectName(QStringLiteral("About_") + doc);
        connect(b, &QPushButton::clicked, this, [this, title, doc, markdown] {
            DocumentDialog view(title, AboutText::document(doc), markdown, this);
            view.exec();
        });
    };
    document(tr("Credits..."), tr("Credits"), QStringLiteral("credits"), true);
    document(tr("License..."), tr("License"), QStringLiteral("license"), false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, 300);
}

DocumentDialog::DocumentDialog(QString const& title, QString const& text, bool markdown, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("DocumentDialog"));
    setWindowTitle(title);
    QVBoxLayout* const layout = new QVBoxLayout(this);
    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("DocumentText"));
    m_browser->setOpenExternalLinks(true);
    if (text.isEmpty())
        m_browser->setPlainText(tr("%1 was not found beside the program (%2).").arg(title, QDir::toNativeSeparators(AboutText::folder())));
    else if (markdown)
        m_browser->setMarkdown(text);
    else
        m_browser->setPlainText(text);
    layout->addWidget(m_browser, 1);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(700, 600);
}

ReportIssueDialog::ReportIssueDialog(QStringList const& recentMessages, QWidget* parent) : QDialog(parent), m_messages(recentMessages)
{
    setObjectName(QStringLiteral("ReportIssueDialog"));
    setWindowTitle(tr("Report an Issue"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    QLabel* const intro = new QLabel(tr("Say what happened and how to make it happen again. The report adds the version of Eclipse and of the "
                                        "computer, and the last messages of the Message Log; nothing leaves this computer until you paste or "
                                        "attach it yourself."),
                                     this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QFormLayout* const form = new QFormLayout;
    m_what = new QPlainTextEdit(this);
    m_what->setObjectName(QStringLiteral("IssueWhat"));
    m_steps = new QPlainTextEdit(this);
    m_steps->setObjectName(QStringLiteral("IssueSteps"));
    form->addRow(tr("What happened:"), m_what);
    form->addRow(tr("Steps to make it happen:"), m_steps);
    layout->addLayout(form);
    QPlainTextEdit* const details = new QPlainTextEdit(AboutText::systemReport(), this);
    details->setObjectName(QStringLiteral("IssueDetails"));
    details->setReadOnly(true);
    details->setMaximumHeight(140);
    layout->addWidget(new QLabel(tr("Added to the report:"), this));
    layout->addWidget(details);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* const copy = buttons->addButton(tr("Copy Report"), QDialogButtonBox::ActionRole);
    copy->setObjectName(QStringLiteral("IssueCopy"));
    QPushButton* const save = buttons->addButton(tr("Save Report..."), QDialogButtonBox::ActionRole);
    QPushButton* const page = buttons->addButton(tr("Open the Issue Page"), QDialogButtonBox::ActionRole);
    page->setObjectName(QStringLiteral("IssuePage"));
    page->setVisible(!HelpCenter::link(QStringLiteral("issues")).isEmpty());
    connect(copy, &QPushButton::clicked, this, &ReportIssueDialog::copyReport);
    connect(save, &QPushButton::clicked, this, [this] {
        QString const path = QFileDialog::getSaveFileName(this, tr("Save Report"), QStringLiteral("eclipse-report.txt"), tr("Text (*.txt)"));
        if (!path.isEmpty())
            saveTo(path);
    });
    connect(page, &QPushButton::clicked, this, &ReportIssueDialog::openIssuePage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(620, 560);
}

QString ReportIssueDialog::report() const
{
    QString out = tr("What happened:\n%1\n\nSteps to make it happen:\n%2\n\n%3\n").arg(m_what->toPlainText(), m_steps->toPlainText(), AboutText::systemReport());
    if (!m_messages.isEmpty())
        out += tr("\nLast messages:\n%1\n").arg(m_messages.mid(std::max<qsizetype>(0, m_messages.size() - 30)).join(QLatin1Char('\n')));
    return out;
}

void ReportIssueDialog::setWhatHappened(QString const& text)
{
    m_what->setPlainText(text);
}

bool ReportIssueDialog::saveTo(QString const& path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(report().toUtf8());
    return file.commit();
}

void ReportIssueDialog::copyReport()
{
    QApplication::clipboard()->setText(report());
}

void ReportIssueDialog::openIssuePage()
{
    // The report goes on the clipboard, to be pasted in the page.
    copyReport();
    QDesktopServices::openUrl(QUrl(HelpCenter::link(QStringLiteral("issues"))));
}
