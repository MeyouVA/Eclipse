// Eclipse - the main window: the scene markers of the Timeline's ruler - made and edited in one dialog (name, frames, colour, note).
#include "ui/window/MainWindow.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include "actions/theme/AppSettings.h"
#include "model/document/Commands.h"
#include "ui/panels/ColourEditor.h"

namespace {

// The Timeline Marker dialog: false when it was cancelled.
bool markerDialog(QWidget* parent, Scene::Marker& marker, int frames, QString const& title)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("SceneMarkerDialog"));
    dialog.setWindowTitle(title);
    QFormLayout* const form = new QFormLayout(&dialog);
    QLineEdit* const name = new QLineEdit(marker.name, &dialog);
    name->setObjectName(QStringLiteral("MarkerName"));
    QSpinBox* const time = new QSpinBox(&dialog);
    time->setObjectName(QStringLiteral("MarkerTime"));
    time->setRange(1, frames);
    time->setValue(marker.frame + 1);
    QSpinBox* const duration = new QSpinBox(&dialog);
    duration->setObjectName(QStringLiteral("MarkerDuration"));
    duration->setRange(1, frames);
    duration->setValue(marker.length);
    duration->setSuffix(QObject::tr(" frames"));
    QColor colour = marker.color;
    QPushButton* const swatch = new QPushButton(&dialog);
    swatch->setObjectName(QStringLiteral("MarkerColour"));
    auto paint = [swatch, &colour] { swatch->setStyleSheet(QStringLiteral("background: %1; min-width: 60px;").arg(colour.name())); };
    paint();
    QObject::connect(swatch, &QPushButton::clicked, &dialog, [&] {
        QColor const picked = ColourEditor::getColour(colour, &dialog, QObject::tr("Marker Colour"));
        if (picked.isValid()) {
            colour = picked;
            paint();
        }
    });
    QPlainTextEdit* const note = new QPlainTextEdit(marker.note, &dialog);
    note->setObjectName(QStringLiteral("MarkerNote"));
    note->setMaximumHeight(90);
    form->addRow(QObject::tr("Name:"), name);
    form->addRow(QObject::tr("Time:"), time);
    form->addRow(QObject::tr("Duration:"), duration);
    form->addRow(QObject::tr("Colour:"), swatch);
    form->addRow(QObject::tr("Note:"), note);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    marker.name = name->text();
    marker.frame = time->value() - 1;
    marker.length = duration->value();
    marker.color = colour;
    marker.note = note->toPlainText();
    return true;
}

}  // namespace

void MainWindow::onActionCreateSceneMarker()
{
    // Over the frames selected in the Timeline, in the colour used last.
    Scene const& scene = m_document->scene();
    Scene::Marker marker;
    marker.frame = m_timeline->selectionFirstFrame();
    marker.length = m_timeline->selectionLastFrame() - m_timeline->selectionFirstFrame() + 1;
    marker.color = QColor(settings().value(QStringLiteral("timeline/lastMarkerColour"), marker.color.name()).toString());
    if (!markerDialog(this, marker, scene.frameCount(), tr("Create Scene Marker")))
        return;
    for (Scene::Marker const& other : scene.markers)
        if (other.frame == marker.frame) {
            QMessageBox::warning(this, tr("Create Scene Marker"), tr("A scene marker already exists at this frame."));
            return;
        }
    settings().setValue(QStringLiteral("timeline/lastMarkerColour"), marker.color.name());
    m_document->beginMacro(tr("Create Scene Marker"));
    int const at = m_document->addMarker(marker.frame, marker.length, marker.name, marker.color);
    if (!marker.note.isEmpty())
        m_document->setMarkerNote(at, marker.note);
    m_document->endMacro();
}

void MainWindow::onActionEditSceneMarker()
{
    Scene const& scene = m_document->scene();
    int const index = scene.markerAt(frame());
    if (index < 0)
        return;
    Scene::Marker marker = scene.markers[index];
    if (!markerDialog(this, marker, scene.frameCount(), tr("Edit Scene Marker")))
        return;
    if (!m_document->setMarker(index, marker))
        QMessageBox::warning(this, tr("Edit Scene Marker"), tr("Cannot move timeline marker.\nTwo markers cannot have the same start frame."));
    else
        settings().setValue(QStringLiteral("timeline/lastMarkerColour"), marker.color.name());
}
