// Eclipse - the help's window (see HelpView.h).
#include "animation/ui/help/HelpView.h"

#include "animation/ui/help/HelpCenter.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

HelpView::HelpView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("HelpView"));
    m_pages = new QTextBrowser(this);
    m_pages->setObjectName(QStringLiteral("HelpPages"));
    m_pages->setSearchPaths(HelpCenter::searchPaths());
    m_pages->setOpenLinks(false);
    // Pages of the help are followed here; the web is opened in the browser.
    connect(m_pages, &QTextBrowser::anchorClicked, this, [this](QUrl const& url) {
        if (url.scheme().startsWith(QLatin1String("http")) || url.scheme() == QLatin1String("mailto")) {
            QDesktopServices::openUrl(url);
            return;
        }
        showTopic(url.toString());
    });

    QPushButton* const back = new QPushButton(tr("Back"), this);
    back->setObjectName(QStringLiteral("HelpBack"));
    QPushButton* const forward = new QPushButton(tr("Forward"), this);
    forward->setObjectName(QStringLiteral("HelpForward"));
    QPushButton* const contents = new QPushButton(tr("Contents"), this);
    contents->setObjectName(QStringLiteral("HelpContents"));
    back->setEnabled(false);
    forward->setEnabled(false);
    connect(back, &QPushButton::clicked, m_pages, &QTextBrowser::backward);
    connect(forward, &QPushButton::clicked, m_pages, &QTextBrowser::forward);
    connect(contents, &QPushButton::clicked, this, &HelpView::showContents);
    connect(m_pages, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
    connect(m_pages, &QTextBrowser::forwardAvailable, forward, &QPushButton::setEnabled);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("HelpSearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, [this] { searchFor(m_search->text()); });
    QHBoxLayout* const bar = new QHBoxLayout;
    bar->addWidget(back);
    bar->addWidget(forward);
    bar->addWidget(contents);
    bar->addStretch(1);
    bar->addWidget(m_search, 1);

    // The contents and the search's results, beside the page.
    QTabWidget* const side = new QTabWidget(this);
    side->setObjectName(QStringLiteral("HelpSide"));
    m_contents = new QListWidget(side);
    m_contents->setObjectName(QStringLiteral("HelpTopics"));
    m_results = new QListWidget(side);
    m_results->setObjectName(QStringLiteral("HelpResults"));
    m_results->setWordWrap(true);
    side->addTab(m_contents, tr("Contents"));
    side->addTab(m_results, tr("Search"));
    connect(m_contents, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { showTopic(item->data(Qt::UserRole).toString()); });
    connect(m_contents, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { showTopic(item->data(Qt::UserRole).toString()); });
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { showResult(m_results->row(item)); });
    connect(m_search, &QLineEdit::returnPressed, side, [side] { side->setCurrentIndex(1); });
    QSplitter* const split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(side);
    split->addWidget(m_pages);
    split->setStretchFactor(1, 1);
    split->setSizes({200, 560});

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("HelpTopicLabel"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(split, 1);
    layout->addWidget(m_status);
    fillContents();
}

void HelpView::fillContents()
{
    m_contents->clear();
    QListWidgetItem* const top = new QListWidgetItem(tr("Contents"), m_contents);
    top->setData(Qt::UserRole, QStringLiteral("index.html"));
    for (auto const& [page, title] : HelpCenter::contents()) {
        QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("    ") + title, m_contents);
        item->setData(Qt::UserRole, page);
    }
    for (QString const& page : {QStringLiteral("commands.html"), QStringLiteral("preferences.html")})
        if (HelpCenter::pageExists(page)) {
            QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("    ") + (page.startsWith(QLatin1Char('c')) ? tr("Commands") : tr("Preferences")), m_contents);
            item->setData(Qt::UserRole, page);
        }
}

bool HelpView::showTopic(QString const& topic)
{
    m_topic = topic;
    m_pages->setSearchPaths(HelpCenter::searchPaths());
    QString const page = topic.section(QLatin1Char('#'), 0, 0);
    if (!HelpCenter::pageExists(page)) {
        QString const message = tr("Help file not found: %1. Verify your installation.").arg(page);
        m_pages->setHtml(QStringLiteral("<p>%1</p><p><a href=\"index.html\">%2</a></p>").arg(message.toHtmlEscaped(), tr("Contents")));
        m_status->setText(message);
        return false;
    }
    m_pages->setSource(QUrl(topic));
    m_status->setText(topic == QLatin1String("index.html") ? QString() : tr("Help topic requested: %1").arg(topic));
    return true;
}

void HelpView::showContents()
{
    fillContents();
    showTopic(QStringLiteral("index.html"));
}

int HelpView::searchFor(QString const& words)
{
    m_search->setText(words);
    m_results->clear();
    m_resultPages.clear();
    QVector<HelpCenter::Hit> const hits = HelpCenter::search(words);
    for (HelpCenter::Hit const& hit : hits) {
        QListWidgetItem* const item = new QListWidgetItem(hit.title, m_results);
        item->setToolTip(hit.text);
        m_resultPages << hit.page;
    }
    m_status->setText(hits.isEmpty() ? tr("Nothing was found for \"%1\".").arg(words) : tr("%n place(s) found for \"%1\".", nullptr, int(hits.size())).arg(words));
    if (!hits.isEmpty()) {
        m_results->setCurrentRow(0);
        showResult(0);
        m_status->setText(tr("%n place(s) found for \"%1\".", nullptr, int(hits.size())).arg(words));
    }
    return int(hits.size());
}

void HelpView::showResult(int row)
{
    if (row >= 0 && row < m_resultPages.size())
        showTopic(m_resultPages[row]);
}

QString HelpView::message() const
{
    return m_status->text();
}
