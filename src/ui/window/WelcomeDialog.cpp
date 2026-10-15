#include "ui/window/WelcomeDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QXmlStreamReader>

static QSettings settings()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}

static QString fpsText(double fps)
{
    return QString::number(fps, 'g', 6);
}

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

QVector<Resolution> WelcomeDialog::resolutions(QString const& resourceDirectory, QString* defaultName)
{
    QVector<Resolution> all;
    QFile file(QDir(resourceDirectory).filePath(QStringLiteral("resolutions.xml")));
    if (file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            QXmlStreamAttributes const a = xml.attributes();
            if (xml.name() == QLatin1String("resolutions") && defaultName)
                *defaultName = a.value(QLatin1String("default")).toString();
            if (xml.name() != QLatin1String("resolution"))
                continue;
            Resolution r;
            r.name = a.value(QLatin1String("name")).toString();
            r.width = a.value(QLatin1String("width")).toInt();
            r.height = a.value(QLatin1String("height")).toInt();
            r.fps = a.value(QLatin1String("fps")).toDouble();
            if (a.hasAttribute(QLatin1String("fov")))
                r.fov = std::clamp(a.value(QLatin1String("fov")).toDouble(), 1.0, 170.0);
            r.across = a.value(QLatin1String("fit")) == QLatin1String("horizontal");
            r.orthographic = a.value(QLatin1String("projection")) == QLatin1String("orthographic");
            if (!r.name.isEmpty() && r.width > 0 && r.height > 0 && r.fps > 0.0)
                all.push_back(r);
        }
    }
    if (all.isEmpty())
        all.push_back({QStringLiteral("HDTV_1080p24"), 1920, 1080, 24.0, false});  // the file is missing or empty
    // The user's own: "name|width|height|fps|fov" (the angle left out by sizes kept before there was one).
    for (QString const& line : settings().value(QStringLiteral("resolutions/custom")).toStringList()) {
        QStringList const parts = line.split(QLatin1Char('|'));
        if (parts.size() != 4 && parts.size() != 5)
            continue;
        Resolution r{parts[0], parts[1].toInt(), parts[2].toInt(), parts[3].toDouble(), true};
        if (parts.size() == 5 && parts[4].toDouble() > 0.0)
            r.fov = std::clamp(parts[4].toDouble(), 1.0, 170.0);
        if (!r.name.isEmpty() && r.width > 0 && r.height > 0 && r.fps > 0.0)
            all.push_back(r);
    }
    return all;
}

static void saveCustom(QVector<Resolution> const& all)
{
    QStringList lines;
    for (Resolution const& r : all)
        if (r.custom)
            lines << QStringLiteral("%1|%2|%3|%4|%5").arg(r.name).arg(r.width).arg(r.height).arg(fpsText(r.fps)).arg(QString::number(r.fov, 'g', 6));
    settings().setValue(QStringLiteral("resolutions/custom"), lines);
}

bool WelcomeDialog::shownAtStartup()
{
    return !settings().value(QStringLiteral("welcome/hide"), false).toBool();
}

WelcomeDialog::WelcomeDialog(QString const& resourceDirectory, QWidget* parent, bool newSceneOnly) : QDialog(parent), m_resources(resourceDirectory)
{
    setWindowTitle(tr("Welcome to Eclipse"));
    setObjectName(QStringLiteral("WelcomeDialog"));

    auto readOnly = [this](char const* name) {
        QLineEdit* const box = new QLineEdit(this);
        box->setObjectName(QLatin1String(name));
        box->setReadOnly(true);
        box->setFocusPolicy(Qt::NoFocus);
        box->setFixedWidth(70);
        return box;
    };

    // ---- a new scene ----
    QGroupBox* const createBox = new QGroupBox(tr("Create a New Scene"), this);
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
    QGroupBox* const openBox = new QGroupBox(tr("Open a Scene"), this);
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

    m_hide = new QCheckBox(tr("Do not show this window at startup"), this);
    m_hide->setObjectName(QStringLiteral("HideAtStartup"));
    m_hide->setChecked(!shownAtStartup());

    QHBoxLayout* const boxes = new QHBoxLayout;
    boxes->setContentsMargins(8, 8, 8, 0);
    boxes->addWidget(createBox, 1);
    boxes->addWidget(openBox);
    QVBoxLayout* const bottom = new QVBoxLayout;
    bottom->setContentsMargins(10, 4, 8, 8);
    bottom->addWidget(m_hide);
    QVBoxLayout* const all = new QVBoxLayout(this);
    all->setContentsMargins(0, 0, 0, 0);
    all->setSpacing(0);
    if (newSceneOnly) {
        setWindowTitle(tr("New Scene"));
        openBox->hide();
        m_hide->hide();
        boxes->setContentsMargins(8, 8, 8, 8);
    } else {
        all->addWidget(new Banner(this));
    }
    all->addLayout(boxes, 1);
    all->addLayout(bottom);

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
    connect(m_hide, &QCheckBox::toggled, this, [](bool hide) { settings().setValue(QStringLiteral("welcome/hide"), hide); });

    m_name->selectAll();
    m_name->setFocus();
    setFixedSize(sizeHint().expandedTo(QSize(640, 420)));
}

void WelcomeDialog::fillResolutions(QString const& select)
{
    QSignalBlocker const block(m_size);
    m_size->clear();
    int chosen = 0;
    for (int i = 0; i < m_resolutions.size(); ++i) {
        m_size->addItem(m_resolutions[i].name);
        if (m_resolutions[i].name == select)
            chosen = i;
    }
    m_size->setCurrentIndex(chosen);
    showResolution();
}

Resolution WelcomeDialog::resolution() const
{
    int const i = m_size->currentIndex();
    return i >= 0 && i < m_resolutions.size() ? m_resolutions[i] : Resolution();
}

void WelcomeDialog::showResolution()
{
    Resolution const r = resolution();
    m_width->setText(QString::number(r.width));
    m_height->setText(QString::number(r.height));
    m_fps->setText(fpsText(r.fps));
    m_fov->setText(QString::number(r.fov, 'g', 6));
    m_remove->setEnabled(r.custom);
}

void WelcomeDialog::addResolution()
{
    Resolution const from = resolution();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Resolution"));
    QLineEdit* const name = new QLineEdit(from.name, &dialog);
    name->setObjectName(QStringLiteral("ResolutionName"));

    QGroupBox* const sizeBox = new QGroupBox(tr("Resolution"), &dialog);
    QSpinBox* const width = new QSpinBox(sizeBox);
    QSpinBox* const height = new QSpinBox(sizeBox);
    width->setObjectName(QStringLiteral("ResolutionWidth"));
    height->setObjectName(QStringLiteral("ResolutionHeight"));
    for (QSpinBox* box : {width, height})
        box->setRange(1, 32768);
    width->setValue(from.width);
    height->setValue(from.height);
    QLabel* const ratio = new QLabel(sizeBox);
    QDoubleSpinBox* const fov = new QDoubleSpinBox(sizeBox);
    fov->setObjectName(QStringLiteral("ResolutionFov"));
    fov->setRange(1.0, 170.0);
    fov->setDecimals(3);
    fov->setSuffix(tr(" deg"));
    fov->setToolTip(tr("The camera's angle of view, top to bottom: how much nearer layers grow, and further ones shrink, with depth."));
    fov->setValue(from.fov);
    QFormLayout* const sizeForm = new QFormLayout(sizeBox);
    sizeForm->addRow(tr("Width:"), width);
    sizeForm->addRow(tr("Height:"), height);
    sizeForm->addRow(tr("Aspect Ratio:"), ratio);
    sizeForm->addRow(tr("Field of View:"), fov);

    QGroupBox* const rateBox = new QGroupBox(tr("Frame Rate"), &dialog);
    QComboBox* const preset = new QComboBox(rateBox);
    static double const kRates[] = {8, 10, 12, 15, 23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60};
    for (double rate : kRates)
        preset->addItem(fpsText(rate), rate);
    QDoubleSpinBox* const fps = new QDoubleSpinBox(rateBox);
    fps->setObjectName(QStringLiteral("ResolutionFps"));
    fps->setRange(1.0, 240.0);
    fps->setDecimals(3);
    fps->setValue(from.fps);
    QFormLayout* const rateForm = new QFormLayout(rateBox);
    rateForm->addRow(tr("Preset:"), preset);
    rateForm->addRow(tr("Frames per second:"), fps);

    QDialogButtonBox* const buttons = new QDialogButtonBox(&dialog);
    QPushButton* const createButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);

    QHBoxLayout* const nameRow = new QHBoxLayout;
    nameRow->addWidget(new QLabel(tr("Resolution Name"), &dialog));
    nameRow->addWidget(name, 1);
    QHBoxLayout* const boxes = new QHBoxLayout;
    boxes->addWidget(sizeBox);
    boxes->addWidget(rateBox);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addLayout(nameRow);
    layout->addLayout(boxes);
    layout->addWidget(buttons);

    // A size needs a name no other size has.
    auto check = [&] {
        QString const wanted = name->text().trimmed();
        bool taken = wanted.isEmpty() || wanted.contains(QLatin1Char('|'));
        for (Resolution const& r : m_resolutions)
            taken = taken || r.name.compare(wanted, Qt::CaseInsensitive) == 0;
        createButton->setEnabled(!taken);
        ratio->setText(QString::number(double(width->value()) / height->value(), 'f', 3));
        int const at = preset->findData(fps->value());
        QSignalBlocker const block(preset);
        preset->setCurrentIndex(at);
    };
    connect(name, &QLineEdit::textChanged, &dialog, check);
    connect(width, &QSpinBox::valueChanged, &dialog, check);
    connect(height, &QSpinBox::valueChanged, &dialog, check);
    connect(fps, &QDoubleSpinBox::valueChanged, &dialog, check);
    connect(preset, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        if (index >= 0)
            fps->setValue(preset->itemData(index).toDouble());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    check();
    name->selectAll();

    if (dialog.exec() != QDialog::Accepted)
        return;
    m_resolutions.push_back({name->text().trimmed(), width->value(), height->value(), fps->value(), true, fov->value()});
    saveCustom(m_resolutions);
    fillResolutions(m_resolutions.last().name);
}

void WelcomeDialog::removeResolution()
{
    int const i = m_size->currentIndex();
    if (i < 0 || i >= m_resolutions.size() || !m_resolutions[i].custom)
        return;
    m_resolutions.removeAt(i);
    saveCustom(m_resolutions);
    fillResolutions(m_resolutions[std::min(i, int(m_resolutions.size()) - 1)].name);
}

void WelcomeDialog::browse()
{
    QString const dir = QFileDialog::getExistingDirectory(this, tr("New Scene Directory"), m_location->currentText());
    if (!dir.isEmpty())
        m_location->setCurrentText(QDir::toNativeSeparators(dir));
}

void WelcomeDialog::create()
{
    // A scene is a folder of its own name, with the scene file inside it.
    QString const name = m_name->text().trimmed();
    static QString const kForbidden = QStringLiteral("\\/:*?\"<>|");
    bool bad = name.isEmpty() || name.endsWith(QLatin1Char('.'));
    for (QChar c : name)
        bad = bad || kForbidden.contains(c);
    if (bad) {
        QMessageBox::warning(this, tr("Eclipse"), tr("Give the scene a name, without any of these: %1").arg(kForbidden));
        return;
    }
    QString const location = m_location->currentText().trimmed();
    if (location.isEmpty() || !QDir(location).exists()) {
        QMessageBox::warning(this, tr("Eclipse"), tr("The location does not exist:\n%1").arg(location));
        return;
    }
    QDir const folder(QDir(location).filePath(name));
    if (folder.exists()) {
        QMessageBox::warning(this, tr("Eclipse"),
                             tr("There is already something called %1 there. Choose another name or another location.").arg(name));
        return;
    }
    // Remembered for next time: where scenes go, and how big they are.
    QSettings s = settings();
    QStringList places = s.value(QStringLiteral("welcome/locations")).toStringList();
    QString const native = QDir::toNativeSeparators(QDir(location).absolutePath());
    places.removeAll(native);
    places.prepend(native);
    while (places.size() > 8)
        places.removeLast();
    s.setValue(QStringLiteral("welcome/locations"), places);
    s.setValue(QStringLiteral("welcome/resolution"), resolution().name);

    m_path = folder.filePath(name + QStringLiteral(".ecl"));
    m_choice = CreateScene;
    accept();
}

void WelcomeDialog::openOther()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Open Scene"), m_location->currentText(), QStringLiteral("Eclipse Scene (*.ecl)"));
    if (path.isEmpty())
        return;
    m_path = path;
    m_choice = OpenScene;
    accept();
}

void WelcomeDialog::openRecent(int row)
{
    QListWidgetItem* const item = m_recent->item(row);
    QString const path = item ? item->data(Qt::UserRole).toString() : QString();
    if (path.isEmpty())
        return;
    m_path = path;
    m_choice = OpenScene;
    accept();
}
