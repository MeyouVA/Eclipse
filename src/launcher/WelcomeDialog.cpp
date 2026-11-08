#include "launcher/WelcomeDialog.h"
#include "launcher/BoardWelcomePage.h"
#include "actions/config/ConfigFile.h"
#include "actions/theme/AppSettings.h"

#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

// The picture across the top: the sun behind the moon, and the name.
namespace {
class Banner : public QWidget {
public:
    explicit Banner(QWidget* parent) : QWidget(parent) { setFixedHeight(96); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient sky(0, 0, width(), 0);
        sky.setColorAt(0.0, QColor(0x10, 0x14, 0x22));
        sky.setColorAt(1.0, QColor(0x24, 0x30, 0x52));
        painter.fillRect(rect(), sky);

        QFont font = painter.font();
        font.setPixelSize(40);
        font.setBold(true);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 6);
        painter.setFont(font);
        QString const name = QStringLiteral("ECLIPSE");
        int const textWidth = painter.fontMetrics().horizontalAdvance(name);
        double const radius = 26.0;
        double const left = (width() - (2 * radius + 22 + textWidth)) / 2.0;
        QPointF const sun(left + radius, height() / 2.0);

        // The glow round the edge, then the dark disc that hides the rest.
        QRadialGradient glow(sun, radius * 1.7);
        glow.setColorAt(0.55, QColor(255, 214, 140, 230));
        glow.setColorAt(0.75, QColor(255, 170, 80, 70));
        glow.setColorAt(1.0, QColor(255, 170, 80, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(sun, radius * 1.7, radius * 1.7);
        painter.setBrush(QColor(0x0c, 0x0e, 0x16));
        painter.drawEllipse(sun + QPointF(2.5, -1.5), radius, radius);

        painter.setPen(QColor(0xf2, 0xf4, 0xf8));
        painter.drawText(QRectF(left + 2 * radius + 22, 0, textWidth + 8, height()), Qt::AlignVCenter | Qt::AlignLeft, name);
    }
};
}  // namespace

WelcomeDialog::WelcomeDialog(QString const& resourceDirectory, QWidget* parent, bool newSceneOnly)
    : WelcomeDialog(resourceDirectory, newSceneOnly ? Mode::NewScene : Mode::Animation, parent)
{
}

WelcomeDialog::WelcomeDialog(QString const& resourceDirectory, Mode mode, QWidget* parent)
    : QDialog(parent), m_resources(resourceDirectory)
{
    setWindowTitle(tr("Welcome to Eclipse"));
    setObjectName(QStringLiteral("WelcomeDialog"));

    auto* all = new QVBoxLayout(this);
    all->setContentsMargins(0, 0, 0, 0);
    all->setSpacing(0);
    bool const newSceneOnly = mode == Mode::NewScene;
    if (!newSceneOnly)
        all->addWidget(new Banner(this));
    auto boardPage = [this](QWidget* owner) {
        auto* page = new BoardWelcomePage(m_resources, owner);
        connect(page, &BoardWelcomePage::projectChosen, this, [this](QString const& path) {
            m_choice = OpenBoardProject;
            m_path = path;
            accept();
        });
        return page;
    };
    if (mode == Mode::Storyboard) {
        setWindowTitle(tr("Eclipse Storyboard"));
        auto* page = boardPage(this);
        all->addWidget(page);
        page->findChild<QPushButton*>(QStringLiteral("CreateBoardProject"))->setDefault(true);
        page->findChild<QLineEdit*>(QStringLiteral("BoardProjectName"))->setFocus();
        resize(sizeHint().expandedTo(QSize(680, 440)));
        return;
    }

    QTabWidget* tabs = nullptr;
    QWidget* animation = this;
    if (mode == Mode::Launcher) {
        setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                       | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        tabs = new QTabWidget(this);
        tabs->setObjectName(QStringLiteral("WelcomeTabs"));
        all->addWidget(tabs, 1);
        animation = new QWidget(tabs);
    }

    auto readOnly = [animation](char const* name) {
        QLineEdit* const box = new QLineEdit(animation);
        box->setObjectName(QLatin1String(name));
        box->setReadOnly(true);
        box->setFocusPolicy(Qt::NoFocus);
        box->setFixedWidth(70);
        return box;
    };

    // ---- a new scene ----
    QGroupBox* const createBox = new QGroupBox(tr("Create a New Scene"), animation);
    m_name = new QLineEdit(tr("NewScene"), createBox);
    m_name->setObjectName(QStringLiteral("SceneName"));
    QPushButton* const createButton = new QPushButton(tr("Create Scene"), createBox);
    createButton->setObjectName(QStringLiteral("CreateScene"));
    createButton->setDefault(true);

    m_location = new QComboBox(createBox);
    m_location->setObjectName(QStringLiteral("Location"));
    m_location->setEditable(true);
    m_location->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_location->setMinimumContentsLength(24);
    QStringList places = settings().value(QStringLiteral("welcome/locations")).toStringList();
    QString const documents = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!places.contains(documents))
        places << documents;
    m_location->addItems(places);
    m_location->lineEdit()->setCursorPosition(0);  // a long path shows its start, not its end
    QPushButton* const browseButton = new QPushButton(tr("Browse..."), createBox);

    m_size = new QComboBox(createBox);
    m_size->setObjectName(QStringLiteral("CameraSize"));
    m_size->setMaxVisibleItems(10);
    QToolButton* const add = new QToolButton(createBox);
    add->setObjectName(QStringLiteral("AddResolution"));
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    add->setToolTip(tr("Make a camera size of your own."));
    m_remove = new QToolButton(createBox);
    m_remove->setObjectName(QStringLiteral("RemoveResolution"));
    m_remove->setText(QString(QChar(0x2212)));
    m_remove->setAutoRaise(true);
    m_remove->setToolTip(tr("Remove this camera size. Only the ones you made can be removed."));
    m_width = readOnly("Width");
    m_height = readOnly("Height");
    m_fps = readOnly("FrameRate");
    m_fov = readOnly("FieldOfView");

    QHBoxLayout* const sizeButtons = new QHBoxLayout;
    sizeButtons->addWidget(add);
    sizeButtons->addWidget(m_remove);
    sizeButtons->addStretch(1);

    QGridLayout* const grid = new QGridLayout(createBox);
    auto label = [&](QString const& text, int row) {
        QLabel* const l = new QLabel(text, createBox);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(l, row, 0);
    };
    label(tr("Name:"), 0);
    grid->addWidget(m_name, 0, 1);
    grid->addWidget(createButton, 0, 2);
    label(tr("Location:"), 1);
    grid->addWidget(m_location, 1, 1);
    grid->addWidget(browseButton, 1, 2);
    label(tr("Camera Size:"), 2);
    grid->addWidget(m_size, 2, 1);
    grid->addLayout(sizeButtons, 2, 2);
    label(tr("Width:"), 3);
    grid->addWidget(m_width, 3, 1, Qt::AlignLeft);
    label(tr("Height:"), 4);
    grid->addWidget(m_height, 4, 1, Qt::AlignLeft);
    label(tr("Frame Rate:"), 5);
    grid->addWidget(m_fps, 5, 1, Qt::AlignLeft);
    label(tr("Field of View:"), 6);
    grid->addWidget(m_fov, 6, 1, Qt::AlignLeft);
    grid->setRowStretch(7, 1);
    grid->setColumnStretch(1, 1);

    // ---- a scene there already is ----
    QGroupBox* const openBox = new QGroupBox(tr("Open a Scene"), animation);
    m_recent = new QListWidget(openBox);
    m_recent->setObjectName(QStringLiteral("RecentScenes"));
    m_recent->setFrameShape(QFrame::NoFrame);
    m_recent->setMinimumWidth(170);
    for (QString const& path : settings().value(QStringLiteral("recentFiles")).toStringList()) {
        if (!QFileInfo::exists(path))
            continue;
        QListWidgetItem* const item = new QListWidgetItem(QFileInfo(path).completeBaseName(), m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    if (m_recent->count() == 0) {
        QListWidgetItem* const none = new QListWidgetItem(tr("No scene opened recently"), m_recent);
        none->setFlags(Qt::NoItemFlags);
    }
    QPushButton* const openButton = new QPushButton(tr("Open..."), openBox);
    openButton->setObjectName(QStringLiteral("OpenScene"));
    openButton->setAutoDefault(false);
    browseButton->setAutoDefault(false);
    QVBoxLayout* const openLayout = new QVBoxLayout(openBox);
    openLayout->addWidget(m_recent, 1);
    openLayout->addWidget(openButton, 0, Qt::AlignRight);

    // ---- help ----
    QGroupBox* const helpBox = new QGroupBox(tr("Get Help"), openBox);
    helpBox->setObjectName(QStringLiteral("WelcomeHelp"));
    QVBoxLayout* const helpLayout = new QVBoxLayout(helpBox);
    QLabel* const question = new QLabel(tr("Have a question? We're here to help."), helpBox);
    question->setWordWrap(true);
    helpLayout->addWidget(question);
    QHBoxLayout* const helpButtons = new QHBoxLayout;
    auto helpButton = [&](QString const& text, QString const& name, QString const& topic) {
        QPushButton* const b = new QPushButton(text, helpBox);
        b->setObjectName(name);
        b->setAutoDefault(false);
        helpButtons->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, topic] {
            m_choice = ShowHelp;
            m_helpTopic = topic;
            accept();
        });
    };
    helpButton(tr("Getting Started"), QStringLiteral("WelcomeGettingStarted"), QStringLiteral("start.html"));
    helpButton(tr("Get Help"), QStringLiteral("WelcomeGetHelp"), QStringLiteral("index.html"));
    helpLayout->addLayout(helpButtons);
    // Video tutorials, where links.conf has a place for them.
    ConfigFile const links(QDir(m_resources).filePath(QStringLiteral("links.conf")));
    if (QString const tutorials = links.value(QStringLiteral("links"), QStringLiteral("tutorials")); !tutorials.isEmpty()) {
        QLabel* const videos = new QLabel(QStringLiteral("<a href=\"%1\"><b>%2</b></a><br>%3").arg(tutorials.toHtmlEscaped(), tr("Watch Video Tutorials"),
                                                                                                tr("Learn Eclipse at your own pace online.")),
                                          helpBox);
        videos->setObjectName(QStringLiteral("WelcomeTutorials"));
        videos->setOpenExternalLinks(true);
        videos->setWordWrap(true);
        helpLayout->addWidget(videos);
    }
    openLayout->addWidget(helpBox);

    QHBoxLayout* const boxes = new QHBoxLayout;
    boxes->setContentsMargins(8, 8, 8, 0);
    boxes->addWidget(createBox, 1);
    boxes->addWidget(openBox);
    if (newSceneOnly) {
        setWindowTitle(tr("New Scene"));
        openBox->hide();
        boxes->setContentsMargins(8, 8, 8, 8);
    }
    if (tabs)
        animation->setLayout(boxes);
    else
        all->addLayout(boxes, 1);

    QString defaultName;
    m_resolutions = resolutions(m_resources, &defaultName);
    fillResolutions(settings().value(QStringLiteral("welcome/resolution"), defaultName).toString());

    connect(m_size, &QComboBox::currentIndexChanged, this, [this] { showResolution(); });
    connect(add, &QToolButton::clicked, this, [this] { addResolution(); });
    connect(m_remove, &QToolButton::clicked, this, [this] { removeResolution(); });
    connect(browseButton, &QPushButton::clicked, this, [this] { browse(); });
    connect(createButton, &QPushButton::clicked, this, [this] { create(); });
    connect(openButton, &QPushButton::clicked, this, [this] { openOther(); });
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) { openRecent(m_recent->row(item)); });

    m_name->selectAll();
    if (!tabs) {
        m_name->setFocus();
        setFixedSize(sizeHint().expandedTo(QSize(640, 420)));
        return;
    }
    tabs->addTab(animation, tr("Animation"));
    auto* board = boardPage(tabs);
    tabs->addTab(board, tr("Storyboard"));
    auto* boardCreate = board->findChild<QPushButton*>(QStringLiteral("CreateBoardProject"));
    auto* boardName = board->findChild<QLineEdit*>(QStringLiteral("BoardProjectName"));
    auto activate = [this, createButton, boardCreate, boardName](int index) {
        bool const storyboard = index == 1;
        settings().setValue(QStringLiteral("welcome/lastTab"), storyboard ? QStringLiteral("board") : QStringLiteral("animation"));
        (storyboard ? createButton : boardCreate)->setDefault(false);
        (storyboard ? boardCreate : createButton)->setDefault(true);
        (storyboard ? boardName : m_name)->setFocus();
    };
    tabs->setCurrentIndex(settings().value(QStringLiteral("welcome/lastTab")).toString() == QLatin1String("board") ? 1 : 0);
    activate(tabs->currentIndex());
    connect(tabs, &QTabWidget::currentChanged, this, activate);
    resize(sizeHint().expandedTo(QSize(700, 465)));
}
