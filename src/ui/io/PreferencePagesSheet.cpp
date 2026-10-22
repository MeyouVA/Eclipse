// Eclipse - Preferences, the Exposure Sheet page: how the Xsheet looks and behaves.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"

#include "actions/manager/ActionManager.h"
#include "ui/timeline/Xsheet.h"

void MainWindow::preferencesSheetMore(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    Q_UNUSED(dialog);
    QFormLayout* const form = w.sheetLayout;
    w.xsheetOmitLine = new QCheckBox(tr("Omit line for drawings exposed"), w.sheet);
    w.xsheetOmitLine->setObjectName(QStringLiteral("PrefXsheetOmitLine"));
    w.xsheetOmitLine->setToolTip(tr("Exposures this short show no line down their held frames."));
    w.xsheetOmitLine->setChecked(s.value(QStringLiteral("prefs/xsheetOmitLine"), false).toBool());
    w.xsheetLineHold = new QSpinBox(w.sheet);
    w.xsheetLineHold->setObjectName(QStringLiteral("PrefXsheetLineHold"));
    w.xsheetLineHold->setRange(1, 100);
    w.xsheetLineHold->setSuffix(tr(" frames or less"));
    w.xsheetLineHold->setValue(s.value(QStringLiteral("prefs/xsheetLineHold"), 3).toInt());
    w.xsheetLineHold->setEnabled(w.xsheetOmitLine->isChecked());
    connect(w.xsheetOmitLine, &QCheckBox::toggled, w.xsheetLineHold, &QWidget::setEnabled);
    form->addRow(w.xsheetOmitLine, w.xsheetLineHold);

    w.xsheetZoomMin = new QSpinBox(w.sheet);
    w.xsheetZoomMin->setObjectName(QStringLiteral("PrefXsheetZoomMin"));
    w.xsheetZoomMin->setRange(1, 6);
    w.xsheetZoomMin->setToolTip(tr("How far the rows can be zoomed out (each level is two pixels of row height)."));
    w.xsheetZoomMin->setValue(s.value(QStringLiteral("prefs/xsheetZoomMin"), 5).toInt());
    form->addRow(tr("Minimum Zoom Level"), w.xsheetZoomMin);
    w.xsheetZoomMax = new QSpinBox(w.sheet);
    w.xsheetZoomMax->setObjectName(QStringLiteral("PrefXsheetZoomMax"));
    w.xsheetZoomMax->setRange(14, 20);
    w.xsheetZoomMax->setToolTip(tr("How far the rows can be zoomed in."));
    w.xsheetZoomMax->setValue(s.value(QStringLiteral("prefs/xsheetZoomMax"), 16).toInt());
    form->addRow(tr("Maximum Zoom Level"), w.xsheetZoomMax);

    w.xsheetGestural = new QCheckBox(tr("Default Setting for Gestural Drag"), w.sheet);
    w.xsheetGestural->setObjectName(QStringLiteral("PrefXsheetGestural"));
    w.xsheetGestural->setToolTip(tr("Gestural Drag is on as Eclipse starts: drag the edge of a block to move it, the bottom of an exposure to hold it longer."));
    w.xsheetGestural->setChecked(s.value(QStringLiteral("prefs/xsheetGestural"), true).toBool());
    form->addRow(w.xsheetGestural);
    w.xsheetGrouping = new QCheckBox(tr("Show Grouping"), w.sheet);
    w.xsheetGrouping->setObjectName(QStringLiteral("PrefXsheetGrouping"));
    w.xsheetGrouping->setChecked(s.value(QStringLiteral("prefs/xsheetGrouping"), false).toBool());
    form->addRow(w.xsheetGrouping);
    w.xsheetSelection = new QCheckBox(tr("Show Selection"), w.sheet);
    w.xsheetSelection->setObjectName(QStringLiteral("PrefXsheetSelection"));
    w.xsheetSelection->setToolTip(tr("The Xsheet starts out showing only the columns of the layers picked in the other views."));
    w.xsheetSelection->setChecked(s.value(QStringLiteral("prefs/xsheetSelection"), false).toBool());
    form->addRow(w.xsheetSelection);
    w.xsheetMinimal = new QCheckBox(tr("Show Minimal Headers"), w.sheet);
    w.xsheetMinimal->setObjectName(QStringLiteral("PrefXsheetMinimal"));
    w.xsheetMinimal->setChecked(s.value(QStringLiteral("prefs/xsheetMinimal"), false).toBool());
    form->addRow(w.xsheetMinimal);
    w.xsheetGrid = new QCheckBox(tr("Show Grid"), w.sheet);
    w.xsheetGrid->setObjectName(QStringLiteral("PrefXsheetGrid"));
    w.xsheetGrid->setToolTip(tr("Lines between the frames of the Xsheet."));
    w.xsheetGrid->setChecked(s.value(QStringLiteral("prefs/xsheetGrid"), true).toBool());
    form->addRow(w.xsheetGrid);

    // Filtering: the kinds of column shown as Eclipse starts.
    QGroupBox* const filtering = new QGroupBox(tr("Filtering"), w.sheet);
    QVBoxLayout* const shown = new QVBoxLayout(filtering);
    static char const* const kShow[] = {QT_TR_NOOP("Show Drawing Columns"), QT_TR_NOOP("Show Function Columns"), QT_TR_NOOP("Show 3D Path Columns"),
                                        QT_TR_NOOP("Show 3D Rotation Columns"), QT_TR_NOOP("Show Annotation Columns")};
    static char const* const kKeys[] = {"Drawing", "Function", "Path", "Rotation", "Annotation"};
    for (int i = 0; i < 5; ++i) {
        w.xsheetShow[i] = new QCheckBox(tr(kShow[i]), filtering);
        w.xsheetShow[i]->setObjectName(QStringLiteral("PrefXsheetShow") + QLatin1String(kKeys[i]));
        w.xsheetShow[i]->setChecked(s.value(QStringLiteral("prefs/xsheetShow") + QLatin1String(kKeys[i]), true).toBool());
        shown->addWidget(w.xsheetShow[i]);
    }
    form->addRow(filtering);

    // Apply/Next in the Column Editor: which column comes next.
    QGroupBox* const next = new QGroupBox(tr("Apply / Next"), w.sheet);
    QVBoxLayout* const nextLayout = new QVBoxLayout(next);
    w.xsheetLeftToRight = new QRadioButton(tr("From Left to Right"), next);
    w.xsheetLeftToRight->setObjectName(QStringLiteral("PrefXsheetLeftToRight"));
    w.xsheetLeftToRight->setToolTip(tr("Apply/Next in the Column Editor goes on to the column on the right."));
    w.xsheetRightToLeft = new QRadioButton(tr("From Right to Left"), next);
    w.xsheetRightToLeft->setObjectName(QStringLiteral("PrefXsheetRightToLeft"));
    w.xsheetRightToLeft->setToolTip(tr("Apply/Next in the Column Editor goes on to the column on the left."));
    (s.value(QStringLiteral("prefs/xsheetApplyNextLeftToRight"), true).toBool() ? w.xsheetLeftToRight : w.xsheetRightToLeft)->setChecked(true);
    nextLayout->addWidget(w.xsheetLeftToRight);
    nextLayout->addWidget(w.xsheetRightToLeft);
    form->addRow(next);

    w.xsheetAddColumns = new QComboBox(w.sheet);
    w.xsheetAddColumns->setObjectName(QStringLiteral("PrefXsheetAddColumns"));
    w.xsheetAddColumns->addItems({tr("Right of the Current Column"), tr("Left of the Current Column"), tr("At the Start (left)"), tr("At the End (right)")});
    w.xsheetAddColumns->setCurrentIndex(std::clamp(s.value(QStringLiteral("prefs/xsheetAddColumns"), 3).toInt(), 0, 3));
    form->addRow(tr("Default Add Columns"), w.xsheetAddColumns);

    w.xsheetAnnotationTicks = new QCheckBox(tr("Show Frame Marker in Annotation Columns"), w.sheet);
    w.xsheetAnnotationTicks->setObjectName(QStringLiteral("PrefXsheetAnnotationTicks"));
    w.xsheetAnnotationTicks->setToolTip(tr("A small tick in the annotation columns on every odd-numbered frame."));
    w.xsheetAnnotationTicks->setChecked(s.value(QStringLiteral("prefs/xsheetAnnotationTicks"), false).toBool());
    form->addRow(w.xsheetAnnotationTicks);
    w.xsheetHardness = new QDoubleSpinBox(w.sheet);
    w.xsheetHardness->setObjectName(QStringLiteral("PrefXsheetHardness"));
    w.xsheetHardness->setRange(0.0, 100.0);
    w.xsheetHardness->setToolTip(tr("How smoothly the pictures in annotation columns are drawn (0: their pixels as they are)."));
    w.xsheetHardness->setValue(s.value(QStringLiteral("prefs/xsheetHardness"), 10.0).toDouble());
    form->addRow(tr("Annotation Column Antialiasing"), w.xsheetHardness);
}

void MainWindow::saveSheetPreferences(PreferenceWidgets& w, QSettings& s)
{
    s.setValue(QStringLiteral("prefs/xsheetOmitLine"), w.xsheetOmitLine->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetLineHold"), w.xsheetLineHold->value());
    s.setValue(QStringLiteral("prefs/xsheetZoomMin"), w.xsheetZoomMin->value());
    s.setValue(QStringLiteral("prefs/xsheetZoomMax"), w.xsheetZoomMax->value());
    s.setValue(QStringLiteral("prefs/xsheetGestural"), w.xsheetGestural->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetGrouping"), w.xsheetGrouping->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetSelection"), w.xsheetSelection->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetMinimal"), w.xsheetMinimal->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetGrid"), w.xsheetGrid->isChecked());
    static char const* const kKeys[] = {"Drawing", "Function", "Path", "Rotation", "Annotation"};
    for (int i = 0; i < 5; ++i)
        s.setValue(QStringLiteral("prefs/xsheetShow") + QLatin1String(kKeys[i]), w.xsheetShow[i]->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetApplyNextLeftToRight"), w.xsheetLeftToRight->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetAddColumns"), w.xsheetAddColumns->currentIndex());
    s.setValue(QStringLiteral("prefs/xsheetAnnotationTicks"), w.xsheetAnnotationTicks->isChecked());
    s.setValue(QStringLiteral("prefs/xsheetHardness"), w.xsheetHardness->value());
}

void MainWindow::applySheetPreferences(QSettings const& s, bool starting)
{
    if (!m_xsheet)
        return;
    m_xsheet->setLineThreshold(s.value(QStringLiteral("prefs/xsheetOmitLine"), false).toBool() ? s.value(QStringLiteral("prefs/xsheetLineHold"), 3).toInt() : 0);
    m_xsheet->setZoomRange(s.value(QStringLiteral("prefs/xsheetZoomMin"), 5).toInt(), s.value(QStringLiteral("prefs/xsheetZoomMax"), 16).toInt());
    m_xsheet->setGridShown(s.value(QStringLiteral("prefs/xsheetGrid"), true).toBool());
    m_xsheet->setAnnotationFrameMarks(s.value(QStringLiteral("prefs/xsheetAnnotationTicks"), false).toBool());
    m_xsheet->setAnnotationSmoothing(s.value(QStringLiteral("prefs/xsheetHardness"), 10.0).toDouble());
    if (!starting)
        return;
    // (These set how the Xsheet starts: its own menu changes them afterwards.)
    m_xsheet->setGesturalDrag(s.value(QStringLiteral("prefs/xsheetGestural"), true).toBool());
    m_xsheet->setGrouping(s.value(QStringLiteral("prefs/xsheetGrouping"), false).toBool());
    m_xsheet->setMinimalHeaders(s.value(QStringLiteral("prefs/xsheetMinimal"), false).toBool());
    if (s.value(QStringLiteral("prefs/xsheetSelection"), false).toBool())
        m_xsheet->setSelectionOnly(true, m_timeline->selectedLayers());
    int shown = Xsheet::ShowEverything;
    static std::pair<char const*, int> const kKeys[] = {{"Drawing", Xsheet::ShowDrawings}, {"Function", Xsheet::ShowFunctions}, {"Path", Xsheet::ShowPaths},
                                                       {"Rotation", Xsheet::ShowRotations}, {"Annotation", Xsheet::ShowAnnotations}};
    for (auto const& [key, flag] : kKeys)
        if (!s.value(QStringLiteral("prefs/xsheetShow") + QLatin1String(key), true).toBool())
            shown &= ~flag;
    m_xsheet->setShown(shown);
    m_actions->setChecked(QStringLiteral("XsheetGesturalDrag"), m_xsheet->gesturalDrag());
    syncXsheetChecks();
}
