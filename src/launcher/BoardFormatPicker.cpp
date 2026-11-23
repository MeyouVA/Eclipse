#include "launcher/BoardFormatPicker.h"
#include "actions/theme/AppSettings.h"
#include "board/model/BoardProject.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <numeric>

namespace {
QString const kCustomKey = QStringLiteral("board/resolutions/custom");

QString rateText(double fps)
{
    return QString::number(fps, 'g', 6);
}

QVector<Resolution> customSizes()
{
    QVector<Resolution> sizes;
    for (QString const& line : settings().value(kCustomKey).toStringList()) {
        QStringList const parts = line.split(QLatin1Char('|'));
        if (parts.size() != 4)
            continue;
        Resolution size{parts[0], parts[1].toInt(), parts[2].toInt(), parts[3].toDouble(), true};
        if (!size.name.trimmed().isEmpty() && BoardProject::validFormat(size.width, size.height, size.fps))
            sizes.push_back(size);
    }
    return sizes;
}

void storeCustom(QVector<Resolution> const& sizes)
{
    QStringList lines;
    for (Resolution const& size : sizes)
        if (size.custom)
            lines << QStringLiteral("%1|%2|%3|%4").arg(size.name).arg(size.width).arg(size.height).arg(rateText(size.fps));
    settings().setValue(kCustomKey, lines);
}
}

QVector<Resolution> BoardFormatPicker::resolutions(QString const& resources, QString* defaultName)
{
    QVector<Resolution> sizes = WelcomeDialog::resolutions(resources, defaultName, false);
    sizes += customSizes();
    return sizes;
}

bool BoardFormatPicker::addCustom(QString const& resources, Resolution const& size, QString* error)
{
    QString const name = size.name.trimmed();
    auto fail = [error](QString const& message) {
        if (error)
            *error = message;
        return false;
    };
    if (name.isEmpty() || name.size() > 64 || name.contains(QLatin1Char('|')))
        return fail(tr("Enter a name of up to 64 characters without a vertical bar."));
    if (!BoardProject::validFormat(size.width, size.height, size.fps))
        return fail(tr("Enter a valid width, height and frame rate."));
    for (Resolution const& existing : resolutions(resources))
        if (existing.name.compare(name, Qt::CaseInsensitive) == 0)
            return fail(tr("A camera size named “%1” already exists.").arg(name));
    QVector<Resolution> sizes = customSizes();
    sizes.push_back({name, size.width, size.height, size.fps, true});
    storeCustom(sizes);
    return true;
}

bool BoardFormatPicker::removeCustom(QString const& name)
{
    QVector<Resolution> sizes = customSizes();
    qsizetype const removed = sizes.removeIf([&](Resolution const& size) { return size.name == name; });
    if (removed > 0)
        storeCustom(sizes);
    return removed > 0;
}

BoardFormatPicker::BoardFormatPicker(QString const& resources, QWidget* parent)
    : QObject(parent), m_resources(resources), m_presetRow(new QWidget(parent)), m_presets(new QComboBox(m_presetRow)),
      m_add(new QToolButton(m_presetRow)), m_remove(new QToolButton(m_presetRow)), m_width(new QSpinBox(parent)),
      m_height(new QSpinBox(parent)), m_fps(new QDoubleSpinBox(parent)), m_aspect(new QLabel(parent))
{
    m_presets->setObjectName(QStringLiteral("BoardCameraSize"));
    m_presets->setMaxVisibleItems(10);
    m_add->setObjectName(QStringLiteral("AddBoardCameraSize"));
    m_add->setText(QStringLiteral("+"));
    m_add->setToolTip(tr("Save the width, height and frame rate as a new camera size"));
    m_remove->setObjectName(QStringLiteral("RemoveBoardCameraSize"));
    m_remove->setText(QStringLiteral("−"));
    m_remove->setToolTip(tr("Remove the selected custom camera size"));
    auto* row = new QHBoxLayout(m_presetRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_presets, 1);
    row->addWidget(m_add);
    row->addWidget(m_remove);
    m_width->setObjectName(QStringLiteral("BoardWidth"));
    m_height->setObjectName(QStringLiteral("BoardHeight"));
    m_fps->setObjectName(QStringLiteral("BoardFrameRate"));
    m_aspect->setObjectName(QStringLiteral("BoardAspectRatio"));
    m_width->setRange(1, 32768);
    m_height->setRange(1, 32768);
    m_fps->setRange(1.0, 240.0);
    m_fps->setDecimals(3);
    connect(m_presets, &QComboBox::currentIndexChanged, this, &BoardFormatPicker::choose);
    connect(m_width, &QSpinBox::valueChanged, this, &BoardFormatPicker::matchPreset);
    connect(m_height, &QSpinBox::valueChanged, this, &BoardFormatPicker::matchPreset);
    connect(m_fps, &QDoubleSpinBox::valueChanged, this, &BoardFormatPicker::matchPreset);
    connect(m_add, &QToolButton::clicked, this, &BoardFormatPicker::addPreset);
    connect(m_remove, &QToolButton::clicked, this, &BoardFormatPicker::removePreset);
    m_sizes = resolutions(m_resources, &m_defaultName);
    fill(QString());
}

void BoardFormatPicker::fill(QString const& select)
{
    {
        QSignalBlocker const block(m_presets);
        m_presets->clear();
        m_presets->addItem(tr("Custom"));
        for (Resolution const& size : m_sizes)
            m_presets->addItem(size.custom ? tr("%1 (custom)").arg(size.name) : size.name);
    }
    if (!select.isEmpty())
        for (qsizetype i = 0; i < m_sizes.size(); ++i)
            if (m_sizes[i].name == select) {
                m_presets->setCurrentIndex(int(i) + 1);
                return;
            }
    matchPreset();
}

void BoardFormatPicker::selectDefault()
{
    for (qsizetype i = 0; i < m_sizes.size(); ++i)
        if (m_sizes[i].name == m_defaultName) {
            m_presets->setCurrentIndex(int(i) + 1);
            return;
        }
    if (!m_sizes.isEmpty())
        m_presets->setCurrentIndex(1);
}

void BoardFormatPicker::setFormat(int width, int height, double fps)
{
    QSignalBlocker const widthBlock(m_width), heightBlock(m_height), fpsBlock(m_fps);
    m_width->setValue(width);
    m_height->setValue(height);
    m_fps->setValue(fps);
    matchPreset();
}

void BoardFormatPicker::choose(int index)
{
    if (index > 0 && index <= m_sizes.size()) {
        Resolution const& size = m_sizes[index - 1];
        QSignalBlocker const widthBlock(m_width), heightBlock(m_height), fpsBlock(m_fps);
        m_width->setValue(size.width);
        m_height->setValue(size.height);
        m_fps->setValue(size.fps);
    }
    refresh();
}

void BoardFormatPicker::refresh()
{
    int const index = m_presets->currentIndex();
    m_remove->setEnabled(index > 0 && index <= m_sizes.size() && m_sizes[index - 1].custom);
    int const divisor = std::gcd(m_width->value(), m_height->value());
    m_aspect->setText(tr("%1 (%2:%3)").arg(double(m_width->value()) / m_height->value(), 0, 'f', 3)
                          .arg(m_width->value() / divisor).arg(m_height->value() / divisor));
    emit changed();
}

void BoardFormatPicker::matchPreset()
{
    int match = 0;
    int const current = m_presets->currentIndex();
    auto fits = [this](Resolution const& size) {
        return size.width == m_width->value() && size.height == m_height->value() && qFuzzyCompare(size.fps, m_fps->value());
    };
    if (current > 0 && current <= m_sizes.size() && fits(m_sizes[current - 1]))
        match = current;
    for (qsizetype i = 0; match == 0 && i < m_sizes.size(); ++i)
        if (fits(m_sizes[i]))
            match = int(i) + 1;
    {
        QSignalBlocker const block(m_presets);
        m_presets->setCurrentIndex(match);
    }
    refresh();
}

void BoardFormatPicker::addPreset()
{
    auto* parent = qobject_cast<QWidget*>(this->parent());
    bool accepted = false;
    QString const name = QInputDialog::getText(parent, tr("New Camera Size"),
        tr("Name for %1 x %2 at %3 fps:").arg(m_width->value()).arg(m_height->value()).arg(rateText(m_fps->value())),
        QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted)
        return;
    QString error;
    if (!addCustom(m_resources, {name, m_width->value(), m_height->value(), m_fps->value(), true}, &error)) {
        QMessageBox::warning(parent, tr("New Camera Size"), error);
        return;
    }
    m_sizes = resolutions(m_resources, &m_defaultName);
    fill(name);
}

void BoardFormatPicker::removePreset()
{
    int const index = m_presets->currentIndex();
    if (index <= 0 || index > m_sizes.size() || !m_sizes[index - 1].custom)
        return;
    removeCustom(m_sizes[index - 1].name);
    m_sizes = resolutions(m_resources, &m_defaultName);
    fill(QString());
}
