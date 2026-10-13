// Eclipse - Scene > Check Files, and Edit > Preferences.
#include "ui/window/MainWindow.h"

#include <functional>

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "actions/manager/ActionManager.h"
#include <QAction>
#include <QToolButton>
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"

QStringList MainWindow::sceneFiles(bool missingOnly) const
{
    QStringList files;
    auto add = [&](QString const& file) {
        if (!file.isEmpty() && !files.contains(file) && !(missingOnly && QFileInfo::exists(file)))
            files << file;
    };
    for (Layer const& layer : m_document->scene().layers) {
        if (layer.kind == Layer::Sound)
            add(layer.soundFile);
        for (Drawing const& drawing : layer.drawings)
            for (Picture const& picture : drawing.pictures)
                if (picture.linked)
                    add(picture.file);
    }
    return files;
}

void MainWindow::onActionCheckFiles()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CheckFilesDialog"));
    dialog.setWindowTitle(tr("Check Files"));
    QListWidget* const list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("CheckFilesList"));
    list->setMinimumSize(520, 240);
    QLabel* const summary = new QLabel(&dialog);
    auto fill = [this, list, summary] {
        QStringList const all = sceneFiles(false), missing = sceneFiles(true);
        list->clear();
        for (QString const& file : all) {
            bool const gone = missing.contains(file);
            QListWidgetItem* const item = new QListWidgetItem((gone ? tr("MISSING   ") : tr("found        ")) + QDir::toNativeSeparators(file), list);
            item->setData(Qt::UserRole, file);
            if (gone)
                item->setForeground(QColor(240, 110, 90));
        }
        summary->setText(all.isEmpty() ? tr("The scene uses no files outside itself: everything is kept in the scene file.")
                         : missing.isEmpty() ? tr("All %1 files the scene uses are where they should be.").arg(all.size())
                                             : tr("%1 of the %2 files the scene uses cannot be found.").arg(missing.size()).arg(all.size()));
    };
    fill();
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // A file that has been moved: say where it is now.
    QPushButton* const locate = buttons->addButton(tr("Locate..."), QDialogButtonBox::ActionRole);
    locate->setObjectName(QStringLiteral("CheckFilesLocate"));
    locate->setToolTip(tr("Point the file chosen in the list at where it is now."));
    locate->setEnabled(false);
    connect(list, &QListWidget::currentRowChanged, locate, [locate](int row) { locate->setEnabled(row >= 0); });
    connect(locate, &QPushButton::clicked, &dialog, [this, list, fill, &dialog] {
        if (!list->currentItem())
            return;
        QString const was = list->currentItem()->data(Qt::UserRole).toString();
        QString const now = QFileDialog::getOpenFileName(&dialog, tr("Locate %1").arg(QFileInfo(was).fileName()), QFileInfo(was).absolutePath());
        if (!now.isEmpty() && m_document->relinkFile(was, now))
            fill();
    });
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(summary);
    layout->addWidget(list);
    layout->addWidget(buttons);
    dialog.exec();
}

static QSettings preferences()
{
    return QSettings(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
}

void MainWindow::applyPreferences()
{
    QSettings const s = preferences();
    m_canvas->setLasso(s.value(QStringLiteral("prefs/lasso"), true).toBool());
    // A drawing has four sheets; two of them - overlay and underlay - are out of the way
    // unless they are asked for.
    bool const moreArt = s.value(QStringLiteral("prefs/overlayUnderlay"), false).toBool();
    for (char const* id : {"SetOverlayArt", "SetUnderlayArt"}) {
        if (QAction* const a = m_actions->action(QLatin1String(id)))
            a->setVisible(moreArt);
        if (QToolButton* const b = findChild<QToolButton*>(QStringLiteral("Art") + QLatin1String(id)))
            b->setVisible(moreArt);
    }
    if (!moreArt && (m_canvas->art() == Canvas::OverlayArt || m_canvas->art() == Canvas::UnderlayArt))
        m_canvas->setArt(Canvas::LineArt);
    m_document->setExtendPreviousExposure(s.value(QStringLiteral("prefs/extendExposure"), true).toBool());
    m_canvas->setNudgeStep(std::clamp(s.value(QStringLiteral("prefs/nudge"), 1).toInt(), 1, 100));
    m_canvas->setProportionParts(std::clamp(s.value(QStringLiteral("prefs/proportionAcross"), 3).toInt(), 2, 12),
                                 std::clamp(s.value(QStringLiteral("prefs/proportionDown"), 3).toInt(), 2, 12));
    m_canvas->setSafeMargin(std::clamp(s.value(QStringLiteral("prefs/safeMargin"), 10).toInt(), 1, 30) / 100.0);
    m_autoSaveMinutes = std::clamp(s.value(QStringLiteral("prefs/autosaveMinutes"), 0).toInt(), 0, 240);
    m_autoSave.stop();
    if (m_autoSaveMinutes > 0)
        m_autoSave.start(m_autoSaveMinutes * 60 * 1000);
}

void MainWindow::onActionPreferences()
{
    QSettings s = preferences();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Preferences"));

    QGroupBox* const general = new QGroupBox(tr("General"), &dialog);
    QCheckBox* const welcome = new QCheckBox(tr("Show the welcome screen when Eclipse starts"), general);
    welcome->setChecked(!s.value(QStringLiteral("welcome/hide"), false).toBool());
    QSpinBox* const autoSave = new QSpinBox(general);
    autoSave->setRange(0, 240);
    autoSave->setSpecialValueText(tr("Never"));
    autoSave->setSuffix(tr(" minutes"));
    autoSave->setValue(m_autoSaveMinutes);
    QFormLayout* const generalLayout = new QFormLayout(general);
    generalLayout->addRow(welcome);
    generalLayout->addRow(tr("Save the scene by itself every"), autoSave);
    generalLayout->addRow(new QLabel(tr("(Only a scene that has been saved once, and only when it has changed.)"), general));

    QGroupBox* const camera = new QGroupBox(tr("Camera"), &dialog);
    QComboBox* const selectMode = new QComboBox(camera);
    selectMode->addItems({tr("Lasso"), tr("Marquee")});
    selectMode->setCurrentIndex(s.value(QStringLiteral("prefs/lasso"), true).toBool() ? 0 : 1);
    QFormLayout* const cameraLayout = new QFormLayout(camera);
    cameraLayout->addRow(tr("The Select tool starts with"), selectMode);
    QSpinBox* const nudge = new QSpinBox(camera);
    nudge->setObjectName(QStringLiteral("PrefNudge"));
    nudge->setRange(1, 100);
    nudge->setSuffix(tr(" pixels"));
    nudge->setToolTip(tr("How far an arrow key moves what the Select tool has selected (ten times as far with Shift)."));
    nudge->setValue(int(m_canvas->nudgeStep()));
    cameraLayout->addRow(tr("Nudging factor"), nudge);
    QSpinBox* const gridAcross = new QSpinBox(camera);
    gridAcross->setObjectName(QStringLiteral("PrefGridAcross"));
    gridAcross->setRange(2, 12);
    gridAcross->setValue(m_canvas->proportionAcross());
    QSpinBox* const gridDown = new QSpinBox(camera);
    gridDown->setObjectName(QStringLiteral("PrefGridDown"));
    gridDown->setRange(2, 12);
    gridDown->setValue(m_canvas->proportionDown());
    QHBoxLayout* const gridParts = new QHBoxLayout;
    gridParts->addWidget(gridAcross);
    gridParts->addWidget(new QLabel(tr("across by"), camera));
    gridParts->addWidget(gridDown);
    gridParts->addWidget(new QLabel(tr("down"), camera));
    gridParts->addStretch(1);
    cameraLayout->addRow(tr("Proportion grid"), gridParts);
    QSpinBox* const safe = new QSpinBox(camera);
    safe->setObjectName(QStringLiteral("PrefSafeMargin"));
    safe->setRange(1, 30);
    safe->setSuffix(tr(" %"));
    safe->setToolTip(tr("How much of the frame, at each edge, lies outside the safe area (the title-safe area leaves twice as much)."));
    safe->setValue(int(std::lround(m_canvas->safeMargin() * 100.0)));
    cameraLayout->addRow(tr("TV safety"), safe);

    QGroupBox* const sheet = new QGroupBox(tr("Exposure Sheet"), &dialog);
    QCheckBox* const extend = new QCheckBox(tr("Extend Exposure of Previous Drawing"), sheet);
    extend->setObjectName(QStringLiteral("PrefExtendExposure"));
    extend->setToolTip(tr("When you draw on an empty frame, the drawing before it is held over the empty frames up to it.\n"
                          "Off: those frames stay empty."));
    extend->setChecked(m_document->extendsPreviousExposure());
    QFormLayout* const sheetLayout = new QFormLayout(sheet);
    sheetLayout->addRow(extend);

    QGroupBox* const advanced = new QGroupBox(tr("Advanced"), &dialog);
    QCheckBox* const moreArt = new QCheckBox(tr("Support Overlay and Underlay Arts"), advanced);
    moreArt->setObjectName(QStringLiteral("PrefOverlayUnderlay"));
    moreArt->setToolTip(tr("Two more sheets in every drawing: an overlay over the line art and an underlay beneath the colour art."));
    moreArt->setChecked(s.value(QStringLiteral("prefs/overlayUnderlay"), false).toBool());
    QFormLayout* const advancedLayout = new QFormLayout(advanced);
    advancedLayout->addRow(moreArt);

    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->addWidget(general);
    layout->addWidget(camera);
    layout->addWidget(sheet);
    layout->addWidget(advanced);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    s.setValue(QStringLiteral("welcome/hide"), !welcome->isChecked());
    s.setValue(QStringLiteral("prefs/autosaveMinutes"), autoSave->value());
    s.setValue(QStringLiteral("prefs/lasso"), selectMode->currentIndex() == 0);
    s.setValue(QStringLiteral("prefs/overlayUnderlay"), moreArt->isChecked());
    s.setValue(QStringLiteral("prefs/nudge"), nudge->value());
    s.setValue(QStringLiteral("prefs/extendExposure"), extend->isChecked());
    s.setValue(QStringLiteral("prefs/proportionAcross"), gridAcross->value());
    s.setValue(QStringLiteral("prefs/proportionDown"), gridDown->value());
    s.setValue(QStringLiteral("prefs/safeMargin"), safe->value());
    s.sync();
    applyPreferences();
}
