#include "launcher/WelcomeDialog.h"
#include "actions/theme/AppSettings.h"
#include "model/scene/Layer.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

static QPair<double, double> fovLimits()
{
    QSettings const s = settings();
    double minimum = s.value(QStringLiteral("prefs/minimumFov"), Layer::kMinFov).toDouble();
    double maximum = s.value(QStringLiteral("prefs/maximumFov"), Layer::kMaxFov).toDouble();
    minimum = std::isfinite(minimum) ? std::clamp(minimum, Layer::kMinFov, Layer::kMaxFov) : Layer::kMinFov;
    maximum = std::isfinite(maximum) ? std::clamp(maximum, minimum, Layer::kMaxFov) : Layer::kMaxFov;
    return {minimum, maximum};
}

static QString fpsText(double fps)
{
    return QString::number(fps, 'g', 6);
}

QVector<Resolution> WelcomeDialog::resolutions(QString const& resourceDirectory, QString* defaultName, bool includeCustom)
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
                r.fov = std::clamp(a.value(QLatin1String("fov")).toDouble(), Layer::kMinFov, Layer::kMaxFov);
            r.across = a.value(QLatin1String("fit")) == QLatin1String("horizontal");
            r.orthographic = a.value(QLatin1String("projection")) == QLatin1String("orthographic");
            if (!r.name.isEmpty() && r.width > 0 && r.height > 0 && r.fps > 0.0)
                all.push_back(r);
        }
    }
    if (all.isEmpty())
        all.push_back({QStringLiteral("HDTV_1080p24"), 1920, 1080, 24.0, false});  // the file is missing or empty
    if (!includeCustom)
        return all;
    // The user's own: "name|width|height|fps|fov" (the angle left out by sizes kept before there was one).
    for (QString const& line : settings().value(QStringLiteral("resolutions/custom")).toStringList()) {
        QStringList const parts = line.split(QLatin1Char('|'));
        if (parts.size() != 4 && parts.size() != 5)
            continue;
        Resolution r{parts[0], parts[1].toInt(), parts[2].toInt(), parts[3].toDouble(), true};
        if (parts.size() == 5 && parts[4].toDouble() > 0.0)
            r.fov = std::clamp(parts[4].toDouble(), Layer::kMinFov, Layer::kMaxFov);
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
    int const i = m_size ? m_size->currentIndex() : -1;
    Resolution result = i >= 0 && i < m_resolutions.size() ? m_resolutions[i] : Resolution();
    auto const [minimum, maximum] = fovLimits();
    result.fov = std::clamp(result.fov, minimum, maximum);
    return result;
}

void WelcomeDialog::showResolution()
{
    Resolution const r = resolution();
    m_width->setText(QString::number(r.width));
    m_height->setText(QString::number(r.height));
    m_fps->setText(fpsText(r.fps));
    m_fov->setText(QString::number(r.fov, 'g', 6));
    int const index = m_size->currentIndex();
    m_fov->setToolTip(index >= 0 && index < m_resolutions.size() && r.fov != m_resolutions[index].fov
                          ? tr("Limited by the camera field-of-view preferences for this new scene.") : QString());
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
    fov->setDecimals(3);
    auto const [minimum, maximum] = fovLimits();
    fov->setRange(minimum, maximum);
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
