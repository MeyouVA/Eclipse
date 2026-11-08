#include "launcher/BoardWelcomePage.h"
#include "launcher/WelcomeDialog.h"
#include "actions/theme/AppSettings.h"
#include "board/model/BoardProject.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

BoardWelcomePage::BoardWelcomePage(QString const& resources, QWidget* parent) : QWidget(parent)
{
    auto* create = new QGroupBox(tr("Create a New Project"), this);
    auto* form = new QGridLayout(create);
    auto row = [create, form](int index, QString const& text, QWidget* widget, Qt::Alignment alignment = {}) {
        auto* label = new QLabel(text, create);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setBuddy(widget);
        form->addWidget(label, index, 0);
        form->addWidget(widget, index, 1, alignment);
    };
    auto field = [create](char const* id, int maximum) {
        auto* edit = new QLineEdit(create);
        edit->setObjectName(QLatin1String(id));
        edit->setMaxLength(maximum);
        return edit;
    };
    m_name = field("BoardProjectName", 120);
    m_name->setText(tr("NewProject"));
    m_name->selectAll();
    m_directory = field("BoardProjectDirectory", 32767);
    QString const documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_directory->setText(settings().value(QStringLiteral("board/welcome/directory"), QDir::toNativeSeparators(documents)).toString());
    m_directory->setCursorPosition(0);
    m_title = field("BoardProjectTitle", 4096);
    m_subtitle = field("BoardProjectSubtitle", 4096);

    auto* browse = new QPushButton(tr("Browse..."), create);
    browse->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, this, [this] {
        QString const chosen = QFileDialog::getExistingDirectory(this, tr("Project Directory"), m_directory->text());
        if (!chosen.isEmpty())
            m_directory->setText(QDir::toNativeSeparators(chosen));
    });

    auto* presets = new QComboBox(create);
    presets->setObjectName(QStringLiteral("BoardCameraSize"));
    presets->setMaxVisibleItems(10);
    presets->addItem(tr("Custom"));
    QString defaultName;
    QVector<Resolution> const sizes = WelcomeDialog::resolutions(resources, &defaultName, false);
    int preferred = 1;
    for (Resolution const& size : sizes) {
        presets->addItem(size.name);
        if (size.name == defaultName)
            preferred = presets->count() - 1;
    }
    m_width = new QSpinBox(create);
    m_width->setObjectName(QStringLiteral("BoardWidth"));
    m_height = new QSpinBox(create);
    m_height->setObjectName(QStringLiteral("BoardHeight"));
    m_fps = new QDoubleSpinBox(create);
    m_fps->setObjectName(QStringLiteral("BoardFrameRate"));
    m_width->setRange(1, 32768);
    m_height->setRange(1, 32768);
    m_fps->setRange(1.0, 240.0);
    m_fps->setDecimals(3);
    auto applyPreset = [this, sizes](int index) {
        if (index <= 0)
            return;
        Resolution const& size = sizes[index - 1];
        QSignalBlocker const widthBlock(m_width), heightBlock(m_height), fpsBlock(m_fps);
        m_width->setValue(size.width);
        m_height->setValue(size.height);
        m_fps->setValue(size.fps);
    };
    connect(presets, &QComboBox::currentIndexChanged, this, applyPreset);
    auto useCustom = [presets] { presets->setCurrentIndex(0); };
    connect(m_width, &QSpinBox::valueChanged, this, useCustom);
    connect(m_height, &QSpinBox::valueChanged, this, useCustom);
    connect(m_fps, &QDoubleSpinBox::valueChanged, this, useCustom);
    presets->setCurrentIndex(preferred);

    auto* createButton = new QPushButton(tr("Create Project"), create);
    createButton->setObjectName(QStringLiteral("CreateBoardProject"));
    row(0, tr("Project Name:"), m_name);
    form->addWidget(createButton, 0, 2);
    row(1, tr("Directory:"), m_directory);
    form->addWidget(browse, 1, 2);
    row(2, tr("Title:"), m_title);
    row(3, tr("Subtitle:"), m_subtitle);
    row(4, tr("Camera Size:"), presets);
    row(5, tr("Width:"), m_width, Qt::AlignLeft);
    row(6, tr("Height:"), m_height, Qt::AlignLeft);
    row(7, tr("Frame Rate:"), m_fps, Qt::AlignLeft);
    form->setColumnStretch(1, 1);
    form->setRowStretch(8, 1);
    connect(createButton, &QPushButton::clicked, this, &BoardWelcomePage::createProject);

    auto* recentBox = new QGroupBox(tr("Recent Projects"), this);
    auto* recentLayout = new QVBoxLayout(recentBox);
    auto* recent = new QListWidget(recentBox);
    recent->setObjectName(QStringLiteral("RecentBoardProjects"));
    recent->setFrameShape(QFrame::NoFrame);
    recent->setMinimumWidth(170);
    for (QString const& path : settings().value(QStringLiteral("board/recentProjects")).toStringList()) {
        if (!QFileInfo(path).isFile())
            continue;
        auto* item = new QListWidgetItem(QFileInfo(path).completeBaseName(), recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    if (recent->count() == 0)
        (new QListWidgetItem(tr("No project opened recently"), recent))->setFlags(Qt::NoItemFlags);
    recentLayout->addWidget(recent, 1);
    connect(recent, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        openProject(item->data(Qt::UserRole).toString());
    });

    auto* open = new QPushButton(tr("Open Project..."), recentBox);
    open->setObjectName(QStringLiteral("OpenBoardProject"));
    open->setAutoDefault(false);
    recentLayout->addWidget(open, 0, Qt::AlignRight);
    connect(open, &QPushButton::clicked, this, [this] {
        openProject(QFileDialog::getOpenFileName(this, tr("Open Project"), m_directory->text(),
                                                tr("Eclipse Storyboard (*.ecb)")));
    });

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 0);
    layout->addWidget(create, 1);
    layout->addWidget(recentBox);
}

void BoardWelcomePage::createProject()
{
    BoardProject const project{m_name->text().trimmed(), m_title->text(), m_subtitle->text(),
                               m_width->value(), m_height->value(), m_fps->value()};
    QString error;
    QString const directory = m_directory->text().trimmed();
    QString const path = project.create(directory, &error);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), error);
        return;
    }
    settings().setValue(QStringLiteral("board/welcome/directory"), QDir::toNativeSeparators(QDir(directory).absolutePath()));
    emit projectChosen(path);
}

void BoardWelcomePage::openProject(QString const& path)
{
    if (path.isEmpty())
        return;
    BoardProject project;
    QString error;
    if (!BoardProject::open(path, project, &error)) {
        QMessageBox::warning(this, tr("Eclipse Storyboard"), tr("Could not open the project:\n%1").arg(error));
        return;
    }
    emit projectChosen(path);
}
