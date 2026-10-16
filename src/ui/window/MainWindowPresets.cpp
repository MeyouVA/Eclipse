// Eclipse - the main window: tool presets - made, managed, written to a file and read from one.
#include "ui/window/MainWindow.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QComboBox>
#include <cmath>
#include <QVBoxLayout>

#include "actions/manager/ActionManager.h"
#include "actions/dialogs/ShortcutsDialog.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LayerPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/WelcomeDialog.h"
#include "ui/timeline/FunctionView.h"
#include "ui/panels/LibraryView.h"

#include "ui/window/MainWindowPrivate.h"
#include <QStatusBar>

QString MainWindow::presetKind() const
{
    switch (m_canvas->tool()) {
    case Canvas::Brush: return QStringLiteral("brush");
    case Canvas::Eraser: return QStringLiteral("eraser");
    case Canvas::Pencil: case Canvas::Line: case Canvas::Rectangle: case Canvas::Ellipse: case Canvas::Polyline: return QStringLiteral("pencil");
    default: return QString();
    }
}

void MainWindow::onActionNewToolPreset()
{
    QString const kind = presetKind();
    if (kind.isEmpty()) {
        statusBar()->showMessage(tr("Presets are for the brush, the pencil, the eraser and the shape tools."), 5000);
        return;
    }
    bool ok = false;
    QString const name = QInputDialog::getText(this, tr("New Tool Preset"), tr("Name of the preset (it keeps the size in hand):"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty() || name.contains(QLatin1Char('|')))
        return;
    QSettings s = settings();
    QStringList list = s.value(QStringLiteral("presets/") + kind).toStringList();
    // The size in hand; and, for the tools that have one, the smoothing.
    QSpinBox* const smoothing = findChild<QSpinBox*>(tr("Smoothing"));
    if (smoothing && kind != QLatin1String("eraser"))
        list << QStringLiteral("%1|%2|%3").arg(name).arg(int(m_canvas->brushWidth())).arg(smoothing->value());
    else
        list << QStringLiteral("%1|%2").arg(name).arg(int(m_canvas->brushWidth()));
    s.setValue(QStringLiteral("presets/") + kind, list);
    if (m_refreshToolProperties)
        m_refreshToolProperties();
}

void MainWindow::onActionManagePresets()
{
    // The presets of your own, tool by tool; the ones that come with the program stay.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Manage Presets"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("UserPresets"));
    auto fill = [list] {
        list->clear();
        QSettings s = settings();
        for (char const* kind : {"brush", "pencil", "eraser"}) {
            QStringList const presets = s.value(QStringLiteral("presets/") + QLatin1String(kind)).toStringList();
            for (int i = 0; i < presets.size(); ++i) {
                QStringList const parts = presets[i].split(QLatin1Char('|'));
                QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("%1: %2   (%3)").arg(QLatin1String(kind), parts.value(0), parts.value(1)), list);
                item->setData(Qt::UserRole, QLatin1String(kind));
                item->setData(Qt::UserRole + 1, i);
            }
        }
        if (list->count() == 0) {
            QListWidgetItem* const none = new QListWidgetItem(tr("You have made no presets yet. Use New Tool Preset."), list);
            none->setFlags(Qt::NoItemFlags);
        }
    };
    fill();
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* const remove = buttons->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    remove->setObjectName(QStringLiteral("DeletePreset"));
    connect(remove, &QPushButton::clicked, &dialog, [list, fill] {
        QListWidgetItem* const item = list->currentItem();
        if (!item || item->data(Qt::UserRole).toString().isEmpty())
            return;
        QSettings s = settings();
        QString const key = QStringLiteral("presets/") + item->data(Qt::UserRole).toString();
        QStringList presets = s.value(key).toStringList();
        int const index = item->data(Qt::UserRole + 1).toInt();
        if (index >= 0 && index < presets.size())
            presets.removeAt(index);
        s.setValue(key, presets);
        fill();
    });
    QPushButton* const rename = buttons->addButton(tr("Rename..."), QDialogButtonBox::ActionRole);
    rename->setObjectName(QStringLiteral("RenamePreset"));
    connect(rename, &QPushButton::clicked, &dialog, [list, fill, &dialog] {
        QListWidgetItem* const item = list->currentItem();
        if (!item || item->data(Qt::UserRole).toString().isEmpty())
            return;
        QSettings s = settings();
        QString const key = QStringLiteral("presets/") + item->data(Qt::UserRole).toString();
        QStringList presets = s.value(key).toStringList();
        int const index = item->data(Qt::UserRole + 1).toInt();
        if (index < 0 || index >= presets.size())
            return;
        QStringList parts = presets[index].split(QLatin1Char('|'));
        bool ok = false;
        QString const name = QInputDialog::getText(&dialog, tr("Rename Preset"), tr("Name:"), QLineEdit::Normal, parts.value(0), &ok).trimmed();
        if (!ok || name.isEmpty() || name.contains(QLatin1Char('|')))
            return;
        parts[0] = name;
        presets[index] = parts.join(QLatin1Char('|'));
        s.setValue(key, presets);
        fill();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(list);
    layout->addWidget(buttons);
    dialog.resize(380, 300);
    dialog.exec();
    if (m_refreshToolProperties)
        m_refreshToolProperties();
}

int MainWindow::exportPresets(QString const& path) const
{
    QSettings const s = settings();
    QJsonObject all;
    int count = 0;
    for (char const* kind : {"brush", "pencil", "eraser"}) {
        QStringList const presets = s.value(QStringLiteral("presets/") + QLatin1String(kind)).toStringList();
        all.insert(QLatin1String(kind), QJsonArray::fromStringList(presets));
        count += int(presets.size());
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return -1;
    file.write(QJsonDocument(QJsonObject{{"format", "eclipse-presets"}, {"presets", all}}).toJson());
    return count;
}

int MainWindow::importPresets(QString const& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    QJsonObject const root = QJsonDocument::fromJson(file.readAll()).object();
    if (root["format"].toString() != QLatin1String("eclipse-presets"))
        return -1;
    QSettings s = settings();
    int count = 0;
    for (char const* kind : {"brush", "pencil", "eraser"}) {
        QString const key = QStringLiteral("presets/") + QLatin1String(kind);
        QStringList list = s.value(key).toStringList();
        for (QJsonValue const& v : root["presets"].toObject()[QLatin1String(kind)].toArray()) {
            QString const preset = v.toString();
            // A name, a size and perhaps a smoothing; nothing else is taken in.
            QStringList const parts = preset.split(QLatin1Char('|'));
            bool sized = false;
            if (parts.size() < 2 || parts.size() > 3 || parts[0].trimmed().isEmpty() || parts[1].toInt(&sized) <= 0 || !sized || list.contains(preset))
                continue;
            list << preset;
            ++count;
        }
        s.setValue(key, list);
    }
    if (m_refreshToolProperties)
        m_refreshToolProperties();
    return count;
}

void MainWindow::onActionExportPresets()
{
    QString const path = QFileDialog::getSaveFileName(this, tr("Export Presets"), QString(), tr("Eclipse presets (*.eclpresets)"));
    if (path.isEmpty())
        return;
    int const count = exportPresets(path);
    statusBar()->showMessage(count < 0 ? tr("The file could not be written.") : tr("%n preset(s) written.", nullptr, count), 6000);
}

void MainWindow::onActionImportPresets()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import Presets"), QString(), tr("Eclipse presets (*.eclpresets)"));
    if (path.isEmpty())
        return;
    int const count = importPresets(path);
    statusBar()->showMessage(count < 0 ? tr("That is not a file of Eclipse presets.") : tr("%n preset(s) taken in.", nullptr, count), 6000);
}

void MainWindow::onActionChangeBrushSize(QString const& way)
{
    // A step of a tenth of the size (at least 1), as the size box of Tool Properties takes it.
    QSpinBox* const box = findChild<QSpinBox*>(QStringLiteral("ToolSize"));
    if (!box)
        return;
    int const step = std::max(1, int(std::lround(box->value() * 0.1)));
    box->setValue(box->value() + (way == QStringLiteral("down") ? -step : step));
    statusBar()->showMessage(tr("Brush size: %1").arg(box->value()), 1500);
}

void MainWindow::onActionToggleStabilizer()
{
    QComboBox* const mode = findChild<QComboBox*>(QStringLiteral("Stabiliser"));
    if (!mode)
        return;
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    mode->setCurrentIndex(mode->currentIndex() != 0 ? 0 : std::clamp(s.value(QStringLiteral("tools/stabiliserLast"), 1).toInt(), 1, 2));
    statusBar()->showMessage(mode->currentIndex() != 0 ? tr("Stabilizer on") : tr("Stabilizer off"), 1500);
}
