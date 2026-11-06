// Eclipse - the main window: File > Import / Export > Animation File (ATOM).
#include "ui/window/MainWindow.h"

#include "formats/anim/AtomAnimation.h"
#include "model/document/Commands.h"
#include "ui/timeline/Timeline.h"
#include "ui/window/MainWindowPrivate.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QStatusBar>

namespace {

bool hasScreenHere()
{
    QString const platform = QApplication::platformName();
    return platform != QLatin1String("offscreen") && platform != QLatin1String("minimal");
}

// The result: Import / Export Success, Warnings or Error.
void tell(QWidget* parent, QStatusBar* bar, bool exporting, AtomAnimation::Result const& result, QStringList* said)
{
    QString const what = exporting ? QObject::tr("Export") : QObject::tr("Import");
    QString title, text;
    if (!result.ok) {
        title = exporting ? QObject::tr("Export Error") : QObject::tr("Import Error");
        text = result.error;
    } else if (!result.warnings.isEmpty()) {
        title = exporting ? QObject::tr("Export Warnings") : QObject::tr("Import Warnings");
        text = (exporting ? QObject::tr("Export completed with %n warnings!", nullptr, int(result.warnings.size()))
                          : QObject::tr("Import completed with %n warnings!", nullptr, int(result.warnings.size())))
               + QStringLiteral("\n\n") + result.warnings.join(QLatin1Char('\n'));
    } else {
        title = exporting ? QObject::tr("Export Success") : QObject::tr("Import Success");
        text = exporting ? QObject::tr("Export completed!") : QObject::tr("Import completed!");
    }
    if (!result.notes.isEmpty())
        text += QStringLiteral("\n\n") + result.notes.join(QLatin1Char('\n'));
    if (said)
        *said = QStringList{title} + text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bar->showMessage(QStringLiteral("%1: %2").arg(title, text.section(QLatin1Char('\n'), 0, 0)), 8000);
    Q_UNUSED(what);
    if (!hasScreenHere())
        return;
    if (!result.ok)
        QMessageBox::critical(parent, title, text);
    else if (!result.warnings.isEmpty())
        QMessageBox::warning(parent, title, text);
    else
        QMessageBox::information(parent, title, text);
}

// The options asked: positions in fields, the additional scale; writing, whether to replace a file and to take only the layers selected.
bool askOptions(QWidget* parent, bool exporting, bool& fields, double& scale, bool& overwrite, bool& selectedOnly)
{
    QDialog dialog(parent);
    dialog.setObjectName(exporting ? QStringLiteral("ExportAnimationDialog") : QStringLiteral("ImportAnimationDialog"));
    dialog.setWindowTitle(exporting ? QObject::tr("Export ATOM Animation File") : QObject::tr("Import ATOM Animation File"));
    QFormLayout* const form = new QFormLayout(&dialog);
    QCheckBox* const inFields = new QCheckBox(QObject::tr("Positions in fields (1 = a twelfth of the scene's width)"), &dialog);
    inFields->setChecked(fields);
    inFields->setToolTip(exporting ? QObject::tr("Exported position X,Y,Z values will be converted to align with Eclipse Field units.")
                                   : QObject::tr("Imported position X,Y,Z values will be converted to align with Eclipse Field units."));
    QDoubleSpinBox* const extra = new QDoubleSpinBox(&dialog);
    extra->setRange(0.001, 1000.0);
    extra->setDecimals(3);
    extra->setValue(scale);
    extra->setToolTip(QObject::tr("An additional scale for the positions."));
    form->addRow(inFields);
    form->addRow(QObject::tr("Scale"), extra);
    QCheckBox* const replace = new QCheckBox(QObject::tr("Overwrite a file there already"), &dialog);
    QCheckBox* const selected = new QCheckBox(QObject::tr("Only the layers selected"), &dialog);
    replace->setChecked(overwrite);
    selected->setChecked(selectedOnly);
    if (exporting) {
        form->addRow(replace);
        form->addRow(selected);
    }
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    fields = inFields->isChecked();
    scale = extra->value();
    overwrite = replace->isChecked();
    selectedOnly = selected->isChecked();
    return true;
}

}  // namespace

bool MainWindow::importAnimation(QString const& path, bool fields, double scale, QStringList* said)
{
    AtomAnimation::Options options;
    options.fields = fields;
    options.scale = scale;
    Scene read = m_document->scene();
    AtomAnimation::Result const result = AtomAnimation::read(read, path, options);
    if (result.ok)
        m_document->editScene(tr("Import ATOM Animation File"), [&read](Scene& s) { s = read; });
    tell(this, statusBar(), false, result, said);
    return result.ok;
}

bool MainWindow::exportAnimation(QString const& path, bool fields, double scale, bool overwrite, bool selectedOnly, QStringList* said)
{
    AtomAnimation::Options options;
    options.fields = fields;
    options.scale = scale;
    options.overwrite = overwrite;
    options.selectedOnly = selectedOnly;
    if (selectedOnly) {
        options.layers = m_timeline->selectedLayers();
        if (options.layers.isEmpty() && layer() >= 0)
            options.layers = {layer()};
    }
    AtomAnimation::Result const result = AtomAnimation::write(m_document->scene(), path, options);
    tell(this, statusBar(), true, result, said);
    return result.ok;
}

void MainWindow::onActionImportAnimation()
{
    QString const path = QFileDialog::getOpenFileName(this, tr("Import ATOM Animation File"), QString(), tr("ATOM animation (*.atom);;All files (*)"));
    if (path.isEmpty())
        return;
    bool fields = true, overwrite = true, selectedOnly = false;
    double scale = 1.0;
    if (!askOptions(this, false, fields, scale, overwrite, selectedOnly))
        return;
    importAnimation(path, fields, scale);
}

void MainWindow::onActionExportAnimation()
{
    QString const path = QFileDialog::getSaveFileName(this, tr("Export ATOM Animation File"), QString(), tr("ATOM animation (*.atom)"),
                                                      nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty())
        return;
    bool fields = true, overwrite = false, selectedOnly = !m_timeline->selectedLayers().isEmpty();
    double scale = 1.0;
    if (!askOptions(this, true, fields, scale, overwrite, selectedOnly))
        return;
    exportAnimation(path.endsWith(QLatin1String(".atom"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".atom"), fields, scale, overwrite, selectedOnly);
}
