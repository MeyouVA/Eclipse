// Eclipse - usage statistics (see UsageStats.h).
#include "animation/ui/help/UsageStats.h"

#include "animation/ui/tools/CommandLine.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

int const kMostHits = 2000;

QJsonObject countsJson(QHash<QString, int> const& counts)
{
    QJsonObject o;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        o[it.key()] = it.value();
    return o;
}

}  // namespace

UsageStats& UsageStats::instance()
{
    static UsageStats stats;
    return stats;
}

UsageStats::UsageStats()
{
    reload();
}

QString UsageStats::file()
{
    return CommandLine::dataFolder() + QStringLiteral("/usage.json");
}

bool UsageStats::enabled()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).value(QStringLiteral("prefs/usageStatistics"), false).toBool();
}

void UsageStats::setEnabled(bool on)
{
    QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse")).setValue(QStringLiteral("prefs/usageStatistics"), on);
}

void UsageStats::hit(Hit const& h)
{
    m_hits << h;
    if (m_hits.size() > kMostHits)
        m_hits.remove(0, m_hits.size() - kMostHits);
}

void UsageStats::command(QString const& id)
{
    if (!enabled() || id.isEmpty())
        return;
    ++m_commands[id];
}

void UsageStats::screen(QString const& name)
{
    if (!enabled() || name.isEmpty())
        return;
    ++m_screens[name];
    hit({QDateTime::currentDateTime(), QStringLiteral("screen"), QString(), name, QString(), QString()});
}

void UsageStats::event(QString const& category, QString const& action, QString const& label, double value)
{
    if (!enabled())
        return;
    hit({QDateTime::currentDateTime(), QStringLiteral("event"), category, action, label, QString::number(value)});
}

void UsageStats::setDimension(int index, QString const& value)
{
    if (!enabled())
        return;
    m_dimensions[index] = value;
    hit({QDateTime::currentDateTime(), QStringLiteral("dimension"), QString::number(index), QString(), QString(), value});
}

void UsageStats::setMetric(int index, double value)
{
    if (!enabled())
        return;
    m_metrics[index] = value;
    hit({QDateTime::currentDateTime(), QStringLiteral("metric"), QString::number(index), QString(), QString(), QString::number(value)});
}

void UsageStats::startSession()
{
    if (!enabled() || m_session.isValid())
        return;
    m_session = QDateTime::currentDateTime();
    ++m_sessions;
    hit({m_session, QStringLiteral("session"), QString(), QStringLiteral("start"), QString(), QString()});
}

void UsageStats::endSession()
{
    if (!m_session.isValid())
        return;
    QDateTime const now = QDateTime::currentDateTime();
    m_seconds += m_session.secsTo(now);
    hit({now, QStringLiteral("session"), QString(), QStringLiteral("end"), QString(), QString::number(m_session.secsTo(now))});
    m_session = QDateTime();
    save();
}

QJsonObject UsageStats::toJson() const
{
    QJsonArray hits;
    for (Hit const& h : m_hits)
        hits.append(QJsonObject{{QStringLiteral("when"), h.when.toString(Qt::ISODate)}, {QStringLiteral("kind"), h.kind},
                                {QStringLiteral("category"), h.category}, {QStringLiteral("action"), h.action},
                                {QStringLiteral("label"), h.label}, {QStringLiteral("value"), h.value}});
    QJsonObject dimensions, metrics;
    for (auto it = m_dimensions.constBegin(); it != m_dimensions.constEnd(); ++it)
        dimensions[QString::number(it.key())] = it.value();
    for (auto it = m_metrics.constBegin(); it != m_metrics.constEnd(); ++it)
        metrics[QString::number(it.key())] = it.value();
    return {{QStringLiteral("commands"), countsJson(m_commands)}, {QStringLiteral("screens"), countsJson(m_screens)},
            {QStringLiteral("sessions"), m_sessions}, {QStringLiteral("seconds"), m_seconds}, {QStringLiteral("dimensions"), dimensions},
            {QStringLiteral("metrics"), metrics}, {QStringLiteral("hits"), hits}};
}

void UsageStats::clear()
{
    m_commands.clear();
    m_screens.clear();
    m_dimensions.clear();
    m_metrics.clear();
    m_hits.clear();
    m_sessions = 0;
    m_seconds = 0;
    QFile::remove(file());
}

bool UsageStats::save() const
{
    if (!enabled())
        return false;
    QSaveFile out(file());
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(toJson()).toJson());
    return out.commit();
}

void UsageStats::reload()
{
    m_commands.clear();
    m_screens.clear();
    m_dimensions.clear();
    m_metrics.clear();
    m_hits.clear();
    QFile in(file());
    if (!in.open(QIODevice::ReadOnly))
        return;
    QJsonObject const o = QJsonDocument::fromJson(in.readAll()).object();
    QJsonObject const commands = o[QStringLiteral("commands")].toObject();
    for (auto it = commands.constBegin(); it != commands.constEnd(); ++it)
        m_commands[it.key()] = it.value().toInt();
    QJsonObject const screens = o[QStringLiteral("screens")].toObject();
    for (auto it = screens.constBegin(); it != screens.constEnd(); ++it)
        m_screens[it.key()] = it.value().toInt();
    QJsonObject const dimensions = o[QStringLiteral("dimensions")].toObject();
    for (auto it = dimensions.constBegin(); it != dimensions.constEnd(); ++it)
        m_dimensions[it.key().toInt()] = it.value().toString();
    QJsonObject const metrics = o[QStringLiteral("metrics")].toObject();
    for (auto it = metrics.constBegin(); it != metrics.constEnd(); ++it)
        m_metrics[it.key().toInt()] = it.value().toDouble();
    m_sessions = o[QStringLiteral("sessions")].toInt();
    m_seconds = o[QStringLiteral("seconds")].toInteger();
    for (QJsonValue const& v : o[QStringLiteral("hits")].toArray()) {
        QJsonObject const h = v.toObject();
        m_hits << Hit{QDateTime::fromString(h[QStringLiteral("when")].toString(), Qt::ISODate), h[QStringLiteral("kind")].toString(),
                      h[QStringLiteral("category")].toString(), h[QStringLiteral("action")].toString(), h[QStringLiteral("label")].toString(),
                      h[QStringLiteral("value")].toString()};
    }
}

// ---- Help > Usage Statistics... ----

UsageStatsDialog::UsageStatsDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("UsageStatsDialog"));
    setWindowTitle(tr("Usage Statistics"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    QLabel* const intro = new QLabel(tr("Thank you for using Eclipse. To see how you use it, Eclipse can keep anonymous counts of the commands and views "
                                        "you use and how long you work, on this computer only. Nothing is sent anywhere; you can look at it, "
                                        "save it to share it yourself, or clear it."),
                                     this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_enabled = new QCheckBox(tr("Keep anonymous usage statistics on this computer"), this);
    m_enabled->setObjectName(QStringLiteral("UsageEnabled"));
    m_enabled->setChecked(UsageStats::enabled());
    connect(m_enabled, &QCheckBox::toggled, this, [](bool on) {
        UsageStats::setEnabled(on);
        if (on)
            UsageStats::instance().startSession();
        else
            UsageStats::instance().endSession();
    });
    layout->addWidget(m_enabled);
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("UsageSummary"));
    layout->addWidget(m_summary);
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName(QStringLiteral("UsageTable"));
    m_table->setHorizontalHeaderLabels({tr("Kind"), tr("Name"), tr("Times")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* const clear = buttons->addButton(tr("Clear"), QDialogButtonBox::ResetRole);
    clear->setObjectName(QStringLiteral("UsageClear"));
    QPushButton* const saveAs = buttons->addButton(tr("Save As..."), QDialogButtonBox::ActionRole);
    connect(clear, &QPushButton::clicked, this, [this] {
        UsageStats::instance().clear();
        refresh();
    });
    connect(saveAs, &QPushButton::clicked, this, [this] {
        QString const path = QFileDialog::getSaveFileName(this, tr("Save Usage Statistics"), QStringLiteral("eclipse-usage.json"), tr("JSON (*.json)"));
        QSaveFile out(path);
        if (!path.isEmpty() && out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(UsageStats::instance().toJson()).toJson());
            out.commit();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refresh();
    resize(520, 480);
}

void UsageStatsDialog::refresh()
{
    UsageStats const& stats = UsageStats::instance();
    m_summary->setText(tr("%n session(s), %1 minute(s) in all.", nullptr, stats.sessions()).arg(stats.seconds() / 60));
    m_table->setRowCount(0);
    auto add = [this](QString const& kind, QHash<QString, int> const& counts) {
        QVector<QPair<QString, int>> list;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            list.push_back({it.key(), it.value()});
        std::sort(list.begin(), list.end(), [](auto const& a, auto const& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
        for (auto const& [name, times] : list) {
            int const row = m_table->rowCount();
            m_table->insertRow(row);
            m_table->setItem(row, 0, new QTableWidgetItem(kind));
            m_table->setItem(row, 1, new QTableWidgetItem(name));
            m_table->setItem(row, 2, new QTableWidgetItem(QString::number(times)));
        }
    };
    add(tr("Command"), stats.commands());
    add(tr("View"), stats.screens());
}

// ---- the Usage Statistics Tester ----

UsageTesterDialog::UsageTesterDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("UsageTesterDialog"));
    setWindowTitle(tr("Usage Statistics Tester"));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    if (!UsageStats::enabled())
        layout->addWidget(new QLabel(tr("Usage statistics are not kept (Help > Usage Statistics...): the hits below are not recorded."), this));

    auto group = [&](QString const& title) {
        QGroupBox* const box = new QGroupBox(title, this);
        layout->addWidget(box);
        return new QFormLayout(box);
    };
    QFormLayout* const dimension = group(tr("Custom Dimension"));
    QSpinBox* const dimensionIndex = new QSpinBox(this);
    dimensionIndex->setRange(1, 200);
    QLineEdit* const dimensionValue = new QLineEdit(this);
    QPushButton* const setDimension = new QPushButton(tr("Set Custom Dimension"), this);
    dimension->addRow(tr("Index"), dimensionIndex);
    dimension->addRow(tr("Value"), dimensionValue);
    dimension->addRow(setDimension);
    connect(setDimension, &QPushButton::clicked, this, [=, this] {
        UsageStats::instance().setDimension(dimensionIndex->value(), dimensionValue->text());
        refresh();
    });

    QFormLayout* const metric = group(tr("Custom Metric"));
    QSpinBox* const metricIndex = new QSpinBox(this);
    metricIndex->setRange(1, 200);
    QDoubleSpinBox* const metricValue = new QDoubleSpinBox(this);
    metricValue->setRange(-1e9, 1e9);
    QPushButton* const setMetric = new QPushButton(tr("Set Custom Metric"), this);
    metric->addRow(tr("Index"), metricIndex);
    metric->addRow(tr("Value"), metricValue);
    metric->addRow(setMetric);
    connect(setMetric, &QPushButton::clicked, this, [=, this] {
        UsageStats::instance().setMetric(metricIndex->value(), metricValue->value());
        refresh();
    });

    QFormLayout* const event = group(tr("Event Tracking"));
    QLineEdit* const category = new QLineEdit(this);
    category->setObjectName(QStringLiteral("TesterCategory"));
    QLineEdit* const action = new QLineEdit(this);
    action->setObjectName(QStringLiteral("TesterAction"));
    QLineEdit* const label = new QLineEdit(this);
    QDoubleSpinBox* const value = new QDoubleSpinBox(this);
    value->setRange(-1e9, 1e9);
    QPushButton* const send = new QPushButton(tr("Send Event Hit"), this);
    send->setObjectName(QStringLiteral("TesterSendEvent"));
    event->addRow(tr("category"), category);
    event->addRow(tr("Action"), action);
    event->addRow(tr("Label"), label);
    event->addRow(tr("Value"), value);
    event->addRow(send);
    connect(send, &QPushButton::clicked, this, [=, this] {
        UsageStats::instance().event(category->text(), action->text(), label->text(), value->value());
        refresh();
    });

    QFormLayout* const screen = group(tr("Screen View"));
    QLineEdit* const screenName = new QLineEdit(this);
    QPushButton* const sendScreen = new QPushButton(tr("Send Screen View"), this);
    screen->addRow(tr("Screen Name"), screenName);
    screen->addRow(sendScreen);
    connect(sendScreen, &QPushButton::clicked, this, [=, this] {
        UsageStats::instance().screen(screenName->text());
        refresh();
    });

    QFormLayout* const session = group(tr("Session"));
    QHBoxLayout* const sessionRow = new QHBoxLayout;
    QPushButton* const start = new QPushButton(tr("Start"), this);
    QPushButton* const end = new QPushButton(tr("End"), this);
    sessionRow->addWidget(start);
    sessionRow->addWidget(end);
    session->addRow(sessionRow);
    connect(start, &QPushButton::clicked, this, [this] {
        UsageStats::instance().startSession();
        refresh();
    });
    connect(end, &QPushButton::clicked, this, [this] {
        UsageStats::instance().endSession();
        refresh();
    });

    m_hits = new QTableWidget(0, 5, this);
    m_hits->setObjectName(QStringLiteral("TesterHits"));
    m_hits->setHorizontalHeaderLabels({tr("When"), tr("Kind"), tr("Category"), tr("Action / Name"), tr("Label / Value")});
    m_hits->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_hits, 1);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refresh();
}

void UsageTesterDialog::refresh()
{
    QVector<UsageStats::Hit> const hits = UsageStats::instance().hits();
    m_hits->setRowCount(0);
    for (qsizetype i = hits.size() - 1; i >= 0 && m_hits->rowCount() < 200; --i) {
        UsageStats::Hit const& h = hits[i];
        int const row = m_hits->rowCount();
        m_hits->insertRow(row);
        QStringList const cells{h.when.toString(QStringLiteral("HH:mm:ss")), h.kind, h.category, h.action, h.label.isEmpty() ? h.value : h.label + QStringLiteral(" / ") + h.value};
        for (int c = 0; c < 5; ++c)
            m_hits->setItem(row, c, new QTableWidgetItem(cells[c]));
    }
}
