// Eclipse - the Preferences dialog: the Drawing and Colours pages.
#include "ui/window/MainWindow.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QTimer>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QColorDialog>
#include <QTabWidget>
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
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDirIterator>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "ui/canvas/Canvas.h"
#include "ui/timeline/Xsheet.h"
#include "ui/timeline/FunctionView.h"
#include "ui/views/PreviewManagerView.h"
#include "ui/views/CoordView.h"
#include "actions/manager/ActionManager.h"
#include <QAction>
#include <QToolButton>
#include "ui/panels/LibraryView.h"
#include "ui/panels/SubstitutionPanel.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/io/ImportExportPrivate.h"
#include "ui/views/NodeView.h"
#include "ui/window/InterfaceColours.h"
#include <QHeaderView>
#include <QTreeWidget>
#include <QLibraryInfo>
#include <QTranslator>
#include <tuple>
#include "ui/io/PreferenceWidgets.h"

void MainWindow::preferencesDrawing(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Drawing: the light table.
    w.drawing = new QGroupBox(tr("Drawing"), &dialog);
    w.wash = new QSpinBox(w.drawing);
    w.wash->setObjectName(QStringLiteral("PrefLightTableWash"));
    w.wash->setRange(10, 90);
    w.wash->setSuffix(tr(" %"));
    w.wash->setToolTip(tr("With the light table on: how strongly the layers that are not being drawn on show."));
    w.wash->setValue(m_canvas->lightTableWash());
    w.drawingLayout = new QFormLayout(w.drawing);
    w.drawingLayout->addRow(tr("Light table: other layers show at"), w.wash);
    w.onTop = new QCheckBox(tr("Current drawing on top when Eclipse starts"), w.drawing);
    w.onTop->setObjectName(QStringLiteral("PrefDrawingOnTop"));
    w.onTop->setToolTip(tr("The layer being drawn on is shown over the others in the Camera view (View > Current Drawing on Top)."));
    w.onTop->setChecked(s.value(QStringLiteral("prefs/drawingOnTop"), false).toBool());
    w.drawingLayout->addRow(w.onTop);
    w.lineArtBitmap = new QCheckBox(tr("New drawing layers: line art in bitmap"), w.drawing);
    w.lineArtBitmap->setObjectName(QStringLiteral("PrefLineArtBitmap"));
    w.lineArtBitmap->setToolTip(tr("Add Drawing Layer starts with the line art painted in pixels rather than drawn in vectors."));
    w.lineArtBitmap->setChecked(s.value(QStringLiteral("prefs/lineArtBitmap"), false).toBool());
    w.drawingLayout->addRow(w.lineArtBitmap);
    w.pasteBehaviour = new QComboBox(w.drawing);
    w.pasteBehaviour->setObjectName(QStringLiteral("PrefPasteBehaviour"));
    w.pasteBehaviour->addItems({tr("Normal Paste"), tr("Always Paste in Centre"), tr("Paste in Centre if Outside")});
    w.pasteBehaviour->setToolTip(tr("Where Paste puts what is pasted in a drawing: where it was, or in the middle of the view."));
    w.pasteBehaviour->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/pasteBehaviour"), 0).toInt(), 0, 2));
    w.drawingLayout->addRow(tr("Paste behaviour"), w.pasteBehaviour);
    w.selectLasso = new QCheckBox(tr("Select tool is lasso when Eclipse starts"), w.drawing);
    w.selectLasso->setObjectName(QStringLiteral("PrefSelectLasso"));
    w.selectLasso->setChecked(s.value(QStringLiteral("prefs/selectLasso"), true).toBool());
    w.drawingLayout->addRow(w.selectLasso);
    w.showStrokes = new QCheckBox(tr("Show strokes when Eclipse starts"), w.drawing);
    w.showStrokes->setObjectName(QStringLiteral("PrefShowStrokes"));
    w.showStrokes->setChecked(s.value(QStringLiteral("prefs/showStrokes"), false).toBool());
    w.drawingLayout->addRow(w.showStrokes);
    w.drawingCycle = new QCheckBox(tr("Drawing substitution goes round (cycle)"), w.drawing);
    w.drawingCycle->setObjectName(QStringLiteral("PrefDrawingCycle"));
    w.drawingCycle->setToolTip(tr("Substitute Drawing Next on the last drawing goes back to the first (and Previous on the first, to the last); off: it stops."));
    w.drawingCycle->setChecked(s.value(QStringLiteral("prefs/drawingCycle"), true).toBool());
    w.drawingLayout->addRow(w.drawingCycle);
    w.fieldsAcross = new QSpinBox(w.drawing);
    w.fieldsAcross->setObjectName(QStringLiteral("PrefFieldsAcross"));
    w.fieldsAcross->setRange(2, 64);
    w.fieldsAcross->setValue(s.value(QStringLiteral("prefs/fieldsAcross"), 12).toInt());
    w.fieldsDown = new QSpinBox(w.drawing);
    w.fieldsDown->setObjectName(QStringLiteral("PrefFieldsDown"));
    w.fieldsDown->setRange(2, 64);
    w.fieldsDown->setValue(s.value(QStringLiteral("prefs/fieldsDown"), 12).toInt());
    w.fields = new QHBoxLayout;
    w.fields->addWidget(w.fieldsAcross);
    w.fields->addWidget(new QLabel(tr("across,"), w.drawing));
    w.fields->addWidget(w.fieldsDown);
    w.fields->addWidget(new QLabel(tr("down"), w.drawing));
    w.fields->addStretch(1);
    w.drawingLayout->addRow(tr("Field grid: fields"), w.fields);
    w.closeGap = new QDoubleSpinBox(w.drawing);
    w.closeGap->setObjectName(QStringLiteral("PrefCloseGap"));
    w.closeGap->setRange(0.0, 50.0);
    w.closeGap->setDecimals(1);
    w.closeGap->setSuffix(tr(" pixels"));
    w.closeGap->setToolTip(tr("The gaps the paint bucket closes by itself, as Eclipse starts (0: none)."));
    w.closeGap->setValue(s.value(QStringLiteral("prefs/closeGap"), 2.0).toDouble());
    w.drawingLayout->addRow(tr("Auto gap closing on startup"), w.closeGap);
    w.ikIterations = new QSpinBox(w.drawing);
    w.ikIterations->setObjectName(QStringLiteral("PrefIkIterations"));
    w.ikIterations->setRange(1, 500);
    w.ikIterations->setToolTip(tr("The IK tool: how many times at most it goes round the chain of bones, turning each to bring the tip nearer."));
    w.ikIterations->setValue(s.value(QStringLiteral("prefs/ikIterations"), 24).toInt());
    w.drawingLayout->addRow(tr("IK: most iterations"), w.ikIterations);
    w.ikError = new QDoubleSpinBox(w.drawing);
    w.ikError->setObjectName(QStringLiteral("PrefIkError"));
    w.ikError->setRange(0.001, 50.0);
    w.ikError->setDecimals(3);
    w.ikError->setToolTip(tr("The IK tool: how near the pointer the tip must come for it to stop (scene units)."));
    w.ikError->setValue(s.value(QStringLiteral("prefs/ikError"), 0.2).toDouble());
    w.drawingLayout->addRow(tr("IK: near enough"), w.ikError);
    w.thumbnails = new QSpinBox(w.drawing);
    w.thumbnails->setObjectName(QStringLiteral("PrefThumbnailSize"));
    w.thumbnails->setRange(50, 200);
    w.thumbnails->setSuffix(tr(" %"));
    w.thumbnails->setToolTip(tr("How big the pictures are in the Library and in Drawing Substitution."));
    w.thumbnails->setValue(s.value(QStringLiteral("prefs/thumbnailSize"), 100).toInt());
    w.drawingLayout->addRow(tr("Thumbnail size"), w.thumbnails);
    w.lineOpacity = new QSpinBox(w.drawing);
    w.lineOpacity->setObjectName(QStringLiteral("PrefPencilLinesOpacity"));
    w.lineOpacity->setRange(0, 100);
    w.lineOpacity->setSuffix(tr(" %"));
    w.lineOpacity->setToolTip(tr("How strongly pencil lines show in the views (what is exported is not changed)."));
    w.lineOpacity->setValue(s.value(QStringLiteral("prefs/pencilLinesOpacity"), 100).toInt());
    w.drawingLayout->addRow(tr("Pencil lines opacity"), w.lineOpacity);
    w.bitmapCanvas = new QSpinBox(w.drawing);
    w.bitmapCanvas->setObjectName(QStringLiteral("PrefBitmapCanvas"));
    w.bitmapCanvas->setRange(100, 400);
    w.bitmapCanvas->setSuffix(tr(" %"));
    w.bitmapCanvas->setToolTip(tr("Default Canvas for Bitmap Layers: how much of the scene's frame the pixels of a new bitmap drawing cover."));
    w.bitmapCanvas->setValue(s.value(QStringLiteral("prefs/bitmapCanvas"), 150).toInt());
    // Its height apart, how fine the pixels are, and the area it is based on.
    w.bitmapCanvasHeight = new QSpinBox(w.drawing);
    w.bitmapCanvasHeight->setObjectName(QStringLiteral("PrefBitmapCanvasHeight"));
    w.bitmapCanvasHeight->setRange(100, 400);
    w.bitmapCanvasHeight->setSuffix(tr(" %"));
    w.bitmapCanvasHeight->setToolTip(tr("Canvas Height: how much of the area's height the pixels of a new bitmap drawing cover."));
    w.bitmapCanvasHeight->setValue(s.value(QStringLiteral("prefs/bitmapCanvasHeight"), w.bitmapCanvas->value()).toInt());
    w.bitmapCanvas->setToolTip(tr("Canvas Width: how much of the area's width the pixels of a new bitmap drawing cover."));
    w.canvasSize = new QHBoxLayout;
    w.canvasSize->addWidget(new QLabel(tr("Width"), w.drawing));
    w.canvasSize->addWidget(w.bitmapCanvas, 1);
    w.canvasSize->addWidget(new QLabel(tr("Height"), w.drawing));
    w.canvasSize->addWidget(w.bitmapCanvasHeight, 1);
    w.drawingLayout->addRow(tr("Bitmap canvas size"), w.canvasSize);
    w.bitmapPpi = new QSpinBox(w.drawing);
    w.bitmapPpi->setObjectName(QStringLiteral("PrefBitmapPpi"));
    w.bitmapPpi->setRange(18, 1200);
    w.bitmapPpi->setSuffix(tr(" ppi"));
    w.bitmapPpi->setToolTip(tr("How fine the pixels of new bitmap drawings are: 72 ppi is a pixel for each unit of the scene (at the layer's own resolution)."));
    w.bitmapPpi->setValue(s.value(QStringLiteral("prefs/bitmapPpi"), 72).toInt());
    w.drawingLayout->addRow(tr("Bitmap resolution"), w.bitmapPpi);
    w.canvasBasis = new QComboBox(w.drawing);
    w.canvasBasis->setObjectName(QStringLiteral("PrefBitmapCanvasBasis"));
    w.canvasBasis->addItems({tr("The Scene's Frame"), tr("Based on the Area Covered by the Camera"), tr("Camera Move over Current")});
    w.canvasBasis->setToolTip(tr("What the canvas of a new bitmap drawing is sized from: the scene's frame; the area the camera covers on the frame;\n"
                               "or all the camera covers as it moves over the scene."));
    w.canvasBasis->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/bitmapCanvasBasis"), 0).toInt(), 0, 2));
    w.drawingLayout->addRow(tr("Bitmap canvas from"), w.canvasBasis);
    // The w.drawing tool in hand when Eclipse starts.
    w.initialTool = new QComboBox(w.drawing);
    w.initialTool->setObjectName(QStringLiteral("PrefInitialTool"));
    w.initialTool->setToolTip(tr("Initial Drawing Tool: the tool in hand when Eclipse starts."));
    for (char const* id : {"ToolBrush", "ToolPencil", "ToolSelect", "ToolCutter", "ToolContourEditor", "ToolEraser", "ToolPaint", "ToolLine",
                           "ToolRectangle", "ToolEllipse", "ToolPolyline", "ToolStroke", "ToolText"})
        if (QAction* const action = m_actions->action(QLatin1String(id)))
            w.initialTool->addItem(action->text().remove(QLatin1Char('&')), QLatin1String(id));
    w.initialTool->setCurrentIndex(std::max(0, w.initialTool->findData(s.value(QStringLiteral("prefs/initialTool"), QStringLiteral("ToolBrush")).toString())));
    w.drawingLayout->addRow(tr("Initial drawing tool"), w.initialTool);
    w.scalePencil = new QCheckBox(tr("Scale pencil line thickness"), w.drawing);
    w.scalePencil->setObjectName(QStringLiteral("PrefScalePencilThickness"));
    w.scalePencil->setToolTip(tr("The Select tool's Scale Pencil Line Thickness is on to begin with: pencil lines scaled get thicker or thinner."));
    w.scalePencil->setChecked(s.value(QStringLiteral("prefs/scalePencilThickness"), false).toBool());
    w.drawingLayout->addRow(w.scalePencil);

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}

void MainWindow::preferencesColours(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    // Colours: of the view round the frame, and of the onion skin.
    w.colours = new QGroupBox(tr("Colours"), &dialog);
    w.coloursLayout = new QFormLayout(w.colours);
    auto colourButton = [&](char const* name, QColor const& now, QString const& title) {
        QPushButton* const button = new QPushButton(w.colours);
        button->setObjectName(QLatin1String(name));
        auto show = [button](QColor const& colour) {
            button->setProperty("colour", colour);
            button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(colour.name()));
        };
        show(now);
        connect(button, &QPushButton::clicked, &dialog, [button, show, title, &dialog] {
            QColor const chosen = QColorDialog::getColor(button->property("colour").value<QColor>(), &dialog, title);
            if (chosen.isValid())
                show(chosen);
        });
        return button;
    };
    w.backdrop = colourButton("PrefBackdrop", m_canvas->backdrop(), tr("Around the Frame"));
    w.onionBefore = colourButton("PrefOnionBefore", m_canvas->onionBefore(), tr("Onion Skin: Before"));
    w.onionAfter = colourButton("PrefOnionAfter", m_canvas->onionAfter(), tr("Onion Skin: After"));
    w.borderColour = colourButton("PrefViewBorderColour", currentViewBorderColour(), tr("Current View Border"));
    w.boneColour = colourButton("PrefBoneColour", m_canvas->boneColour(), tr("Default Bone Colour"));
    w.boneSelected = colourButton("PrefBoneSelectedColour", m_canvas->boneSelectedColour(), tr("Selected Bone Colour"));
    w.coloursLayout->addRow(tr("Camera view, around the frame"), w.backdrop);
    w.coloursLayout->addRow(tr("Onion skin, drawings before"), w.onionBefore);
    w.coloursLayout->addRow(tr("Onion skin, drawings after"), w.onionAfter);
    w.coloursLayout->addRow(tr("Current view border"), w.borderColour);
    w.coloursLayout->addRow(tr("Bones"), w.boneColour);
    w.coloursLayout->addRow(tr("Bone or joint picked"), w.boneSelected);
    // And every colour the views paint with, grouped as the views are: double-click one to change it.
    w.viewColours = new QTreeWidget(w.colours);
    w.viewColours->setObjectName(QStringLiteral("PrefInterfaceColours"));
    w.viewColours->setHeaderLabels({tr("Colour"), QString()});
    w.viewColours->setRootIsDecorated(true);
    w.viewColours->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    w.viewColours->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    w.viewColours->header()->resizeSection(1, 60);
    w.viewColours->setMinimumHeight(220);
    auto showColour = [](QTreeWidgetItem* item, QColor const& colour) {
        item->setData(1, Qt::UserRole, colour);
        QPixmap swatch(40, 14);
        swatch.fill(colour);
        item->setIcon(1, QIcon(swatch));
        item->setToolTip(1, colour.name(colour.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
    };
    {
        QHash<QString, QTreeWidgetItem*> groups;
        for (InterfaceColours::Entry const& e : InterfaceColours::entries()) {
            QString const group = tr(e.group);
            QTreeWidgetItem*& parent = groups[group];
            if (!parent) {
                parent = new QTreeWidgetItem(w.viewColours, {group});
                parent->setFlags(Qt::ItemIsEnabled);
                parent->setExpanded(true);
            }
            QTreeWidgetItem* const item = new QTreeWidgetItem(parent, {tr(e.label)});
            item->setData(0, Qt::UserRole, QString::fromLatin1(e.key));
            item->setData(0, Qt::UserRole + 1, e.usual);
            showColour(item, InterfaceColours::get(e.key));
        }
    }
    connect(w.viewColours, &QTreeWidget::itemDoubleClicked, &dialog, [showColour, &dialog](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).toString().isEmpty())
            return;
        QColor const chosen = QColorDialog::getColor(item->data(1, Qt::UserRole).value<QColor>(), &dialog, item->text(0), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            showColour(item, chosen);
    });
    w.coloursLayout->addRow(w.viewColours);
    w.usual = new QPushButton(tr("Restore the Usual Colours"), w.colours);
    w.usual->setObjectName(QStringLiteral("PrefColoursReset"));
    w.coloursLayout->addRow(w.usual);
    connect(w.usual, &QPushButton::clicked, &dialog, [showColour, &w] {
        for (int g = 0; g < w.viewColours->topLevelItemCount(); ++g)
            for (int i = 0; i < w.viewColours->topLevelItem(g)->childCount(); ++i) {
                QTreeWidgetItem* const item = w.viewColours->topLevelItem(g)->child(i);
                showColour(item, item->data(0, Qt::UserRole + 1).value<QColor>());
            }
    });
    connect(w.usual, &QPushButton::clicked, &dialog, [&w] {
        struct Usual {
            QPushButton* button;
            QColor colour;
        };
        for (Usual const& one : {Usual{w.backdrop, QColor(127, 127, 127)}, Usual{w.onionBefore, QColor(220, 60, 60)}, Usual{w.onionAfter, QColor(50, 160, 80)},
                                 Usual{w.borderColour, QColor(0xc8, 0x28, 0x1e)}, Usual{w.boneColour, QColor(90, 220, 140)}, Usual{w.boneSelected, QColor(Qt::white)}}) {
            one.button->setProperty("colour", one.colour);
            one.button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #202020; min-height: 16px; min-width: 60px;").arg(one.colour.name()));
        }
    });

    Q_UNUSED(s);
    Q_UNUSED(dialog);
}
