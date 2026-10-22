// Eclipse - Tool Properties: thickness stencils, bitmap brush tips and their shapes, snapping rows.
// options, the Rigging tool's mode, the ends and corners of lines, the thickness of a point.
#include "ui/panels/ToolProperties.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QBuffer>
#include <QFileInfo>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QPainter>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
#include "canvas/PencilTexture.h"
#include "ui/window/MainWindowPrivate.h"
#include <QFormLayout>

void ToolProperties::fillStencils(QComboBox* box)
{
    QSignalBlocker const quiet(box);
    box->clear();
    box->addItems({tr("Uniform"), tr("Tapered"), tr("Tapered Beginning"), tr("Tapered End"), tr("Medium Tapered"), tr("Pointy"), tr("Fat Ends"),
                   tr("Curved Above"), tr("Curved Below")});
    QSettings s = settings();
    s.beginGroup(QStringLiteral("tools/stencils"));
    QStringList names = s.childKeys();
    s.endGroup();
    names.sort(Qt::CaseInsensitive);
    for (QString const& name : std::as_const(names))
        box->addItem(name, name);
}

bool ToolProperties::makeStencil(QString const& name)
{
    QVector<float> const profile = m_canvas->selectedLineProfile();
    QComboBox* const box = findChild<QComboBox*>(QStringLiteral("ThicknessStencil"));
    if (profile.isEmpty() || name.isEmpty() || !box)
        return false;
    QStringList shares;
    for (float share : profile)
        shares << QString::number(share, 'g', 4);
    settings().setValue(QStringLiteral("tools/stencils/") + name, shares.join(QLatin1Char(',')));
    fillStencils(box);
    box->setCurrentIndex(std::max(0, box->findData(name)));
    return true;
}

void ToolProperties::fillBitmapTips(QComboBox* box)
{
    QSignalBlocker const quiet(box);
    box->clear();
    box->addItems({tr("Round"), tr("Square"), tr("Chisel"), tr("Spray")});
    QSettings s = settings();
    s.beginGroup(QStringLiteral("tools/bitmapTips"));
    QStringList names = s.childKeys();
    s.endGroup();
    names.sort(Qt::CaseInsensitive);
    for (QString const& name : std::as_const(names))
        box->addItem(name, name);
}

int ToolProperties::importBitmapTips(QString const& path, QComboBox* box)
{
    // A picture: one tip (its light parts paint); a Photoshop brush file: each of its sampled brushes.
    QVector<QImage> tips;
    QString why;
    if (path.endsWith(QStringLiteral(".abr"), Qt::CaseInsensitive)) {
        tips = Canvas::readAbr(path, &why);
    } else {
        QImage const picture(path);
        if (!picture.isNull())
            tips.push_back(picture.convertToFormat(QImage::Format_Grayscale8));
    }
    if (tips.isEmpty()) {
        QMessageBox::warning(this, tr("Import Brushes"), why.isEmpty() ? tr("Unable to read %1").arg(path) : why);
        return 0;
    }
    QString const base = QFileInfo(path).completeBaseName();
    QString last;
    for (int i = 0; i < tips.size(); ++i) {
        // Kept small enough to stamp quickly.
        QImage tip = tips[i];
        if (tip.width() > 256 || tip.height() > 256)
            tip = tip.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        tip.save(&buffer, "PNG");
        last = tips.size() == 1 ? base : QStringLiteral("%1 %2").arg(base).arg(i + 1);
        settings().setValue(QStringLiteral("tools/bitmapTips/") + last, png);
    }
    if (box) {
        fillBitmapTips(box);
        box->setCurrentIndex(std::max(0, box->findData(last)));
    }
    return int(tips.size());
}

void ToolProperties::loadTipShape()
{
    QStringList const v = settings().value(QStringLiteral("tools/tipShape")).toString().split(QLatin1Char(','));
    Canvas::TipShape t;
    if (v.size() >= 12) {
        t.angle = v[0].toInt();
        t.roundness = std::clamp(v[1].toInt(), 1, 100);
        t.spacing = std::clamp(v[2].toInt(), 0, 500);
        t.count = std::clamp(v[3].toInt(), 1, 20);
        t.flipX = v[4] == QStringLiteral("1");
        t.flipY = v[5] == QStringLiteral("1");
        t.randomAngle = std::clamp(v[6].toInt(), 0, 180);
        t.randomSpacing = std::clamp(v[7].toInt(), 0, 100);
        t.randomCount = std::clamp(v[8].toInt(), 0, 20);
        t.randomSize = std::clamp(v[9].toInt(), 0, 100);
        t.randomOffset = std::clamp(v[10].toInt(), 0, 200);
        t.minOpacity = std::clamp(v[11].toInt(), 0, 100);
    }
    if (v.size() == 28) {
        t.followDirection = v[12] == QStringLiteral("1");
        t.mode = std::clamp(v[13].toInt(), 0, 3);
        t.dual = v[14] == QStringLiteral("1");
        t.dualKind = std::clamp(v[15].toInt(), 0, 4);
        t.dualSize = std::clamp(v[16].toInt(), 1, 400);
        t.dualSpacing = std::clamp(v[17].toInt(), 0, 500);
        t.dualCount = std::clamp(v[18].toInt(), 1, 20);
        t.dualRoundness = std::clamp(v[19].toInt(), 1, 100);
        t.dualAngle = v[20].toInt();
        t.dualFlipX = v[21] == QStringLiteral("1");
        t.dualFlipY = v[22] == QStringLiteral("1");
        t.dualRandomAngle = std::clamp(v[23].toInt(), 0, 180);
        t.dualRandomSpacing = std::clamp(v[24].toInt(), 0, 100);
        t.dualRandomCount = std::clamp(v[25].toInt(), 0, 20);
        t.dualRandomSize = std::clamp(v[26].toInt(), 0, 100);
        t.dualRandomOffset = std::clamp(v[27].toInt(), 0, 200);
    }
    m_canvas->setTipShape(t);
    m_canvas->setDualTipImage(QImage::fromData(settings().value(QStringLiteral("tools/bitmapTips/") + settings().value(QStringLiteral("tools/dualTipName")).toString()).toByteArray()));
}

void ToolProperties::saveTipShape(Canvas::TipShape const& t)
{
    m_canvas->setTipShape(t);
    QStringList v;
    for (int n : {t.angle, t.roundness, t.spacing, t.count, int(t.flipX), int(t.flipY), t.randomAngle, t.randomSpacing, t.randomCount, t.randomSize,
                  t.randomOffset, t.minOpacity, int(t.followDirection), t.mode, int(t.dual), t.dualKind, t.dualSize, t.dualSpacing, t.dualCount,
                  t.dualRoundness, t.dualAngle, int(t.dualFlipX), int(t.dualFlipY), t.dualRandomAngle, t.dualRandomSpacing, t.dualRandomCount,
                  t.dualRandomSize, t.dualRandomOffset})
        v << QString::number(n);
    settings().setValue(QStringLiteral("tools/tipShape"), v.join(QLatin1Char(',')));
}

void ToolProperties::editTipShape()
{
    // The bitmap brush's tip, changed as the boxes are (Cancel puts it back).
    Canvas::TipShape const was = m_canvas->tipShape();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("TipEditor"));
    dialog.setWindowTitle(tr("Bitmap Brush Tip"));
    QFormLayout* const form = new QFormLayout(&dialog);
    auto number = [&](QString const& name, int low, int high, int value, QString const& suffix) {
        QSpinBox* const box = new QSpinBox(&dialog);
        box->setObjectName(name);
        box->setRange(low, high);
        box->setValue(value);
        box->setSuffix(suffix);
        form->addRow(name, box);
        return box;
    };
    QSpinBox* const angle = number(tr("Angle"), -180, 180, was.angle, tr(" deg"));
    QSpinBox* const roundness = number(tr("Roundness"), 1, 100, was.roundness, QStringLiteral(" %"));
    QSpinBox* const spacing = number(tr("Spacing"), 0, 500, was.spacing, QStringLiteral(" %"));
    QSpinBox* const count = number(tr("Count"), 1, 20, was.count, QString());
    QCheckBox* const flipX = new QCheckBox(tr("Flip Brush Tip Horizontally"), &dialog);
    QCheckBox* const flipY = new QCheckBox(tr("Flip Brush Tip Vertically"), &dialog);
    flipX->setChecked(was.flipX);
    flipY->setChecked(was.flipY);
    form->addRow(flipX);
    form->addRow(flipY);
    form->addRow(new QLabel(tr("Randomness"), &dialog));
    QSpinBox* const randomAngle = number(tr("Random Angle"), 0, 180, was.randomAngle, tr(" deg"));
    QSpinBox* const randomSpacing = number(tr("Random Spacing"), 0, 100, was.randomSpacing, QStringLiteral(" %"));
    QSpinBox* const randomCount = number(tr("Random Count"), 0, 20, was.randomCount, QString());
    QSpinBox* const randomSize = number(tr("Random Size"), 0, 100, was.randomSize, QStringLiteral(" %"));
    QSpinBox* const randomOffset = number(tr("Random Offset"), 0, 200, was.randomOffset, QStringLiteral(" %"));
    QSpinBox* const minOpacity = number(tr("Minimum Opacity"), 0, 100, was.minOpacity, QStringLiteral(" %"));
    minOpacity->setToolTip(tr("How solid the paint is where the pen barely presses (100: pressure leaves it solid)."));
    QCheckBox* const follow = new QCheckBox(tr("Turn with the Stroke"), &dialog);
    follow->setObjectName(QStringLiteral("TipFollow"));
    follow->setChecked(was.followDirection);
    form->addRow(follow);
    QComboBox* const mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("TipMode"));
    mode->addItems({tr("Normal"), tr("Stencil (only on paint)"), tr("Behind"), tr("Erase")});
    mode->setCurrentIndex(was.mode);
    form->addRow(tr("Mode"), mode);
    // The dual tip.
    QCheckBox* const dual = new QCheckBox(tr("Dual Tip"), &dialog);
    dual->setObjectName(QStringLiteral("TipDual"));
    dual->setChecked(was.dual);
    form->addRow(dual);
    QComboBox* const dualKind = new QComboBox(&dialog);
    dualKind->addItems({tr("Round"), tr("Square"), tr("Chisel"), tr("Spray")});
    {
        QSettings s = settings();
        s.beginGroup(QStringLiteral("tools/bitmapTips"));
        for (QString const& name : s.childKeys())
            dualKind->addItem(name, name);
        s.endGroup();
    }
    QString const dualName = settings().value(QStringLiteral("tools/dualTipName")).toString();
    dualKind->setCurrentIndex(was.dualKind == Canvas::BitmapImage && dualKind->findData(dualName) >= 0 ? dualKind->findData(dualName) : std::clamp(was.dualKind, 0, 3));
    form->addRow(tr("Dual Tip"), dualKind);
    QSpinBox* const dualSize = number(tr("Dual Size"), 1, 400, was.dualSize, QStringLiteral(" %"));
    QSpinBox* const dualSpacing = number(tr("Dual Spacing"), 0, 500, was.dualSpacing, QStringLiteral(" %"));
    QSpinBox* const dualCount = number(tr("Dual Count"), 1, 20, was.dualCount, QString());
    QSpinBox* const dualRoundness = number(tr("Dual Roundness"), 1, 100, was.dualRoundness, QStringLiteral(" %"));
    QSpinBox* const dualAngle = number(tr("Dual Angle"), -180, 180, was.dualAngle, tr(" deg"));
    QCheckBox* const dualFlipX = new QCheckBox(tr("Flip Dual Tip Horizontally"), &dialog);
    QCheckBox* const dualFlipY = new QCheckBox(tr("Flip Dual Tip Vertically"), &dialog);
    dualFlipX->setChecked(was.dualFlipX);
    dualFlipY->setChecked(was.dualFlipY);
    form->addRow(dualFlipX);
    form->addRow(dualFlipY);
    QSpinBox* const dualRandomAngle = number(tr("Dual Random Angle"), 0, 180, was.dualRandomAngle, tr(" deg"));
    QSpinBox* const dualRandomSpacing = number(tr("Dual Random Spacing"), 0, 100, was.dualRandomSpacing, QStringLiteral(" %"));
    QSpinBox* const dualRandomCount = number(tr("Dual Random Count"), 0, 20, was.dualRandomCount, QString());
    QSpinBox* const dualRandomSize = number(tr("Dual Random Size"), 0, 100, was.dualRandomSize, QStringLiteral(" %"));
    QSpinBox* const dualRandomOffset = number(tr("Dual Random Offset"), 0, 200, was.dualRandomOffset, QStringLiteral(" %"));
    auto apply = [=, this] {
        Canvas::TipShape t;
        t.angle = angle->value();
        t.roundness = roundness->value();
        t.spacing = spacing->value();
        t.count = count->value();
        t.flipX = flipX->isChecked();
        t.flipY = flipY->isChecked();
        t.randomAngle = randomAngle->value();
        t.randomSpacing = randomSpacing->value();
        t.randomCount = randomCount->value();
        t.randomSize = randomSize->value();
        t.randomOffset = randomOffset->value();
        t.minOpacity = minOpacity->value();
        t.followDirection = follow->isChecked();
        t.mode = mode->currentIndex();
        t.dual = dual->isChecked();
        t.dualKind = dualKind->currentData().isValid() ? int(Canvas::BitmapImage) : dualKind->currentIndex();
        settings().setValue(QStringLiteral("tools/dualTipName"), dualKind->currentData().toString());
        m_canvas->setDualTipImage(QImage::fromData(settings().value(QStringLiteral("tools/bitmapTips/") + dualKind->currentData().toString()).toByteArray()));
        t.dualSize = dualSize->value();
        t.dualSpacing = dualSpacing->value();
        t.dualCount = dualCount->value();
        t.dualRoundness = dualRoundness->value();
        t.dualAngle = dualAngle->value();
        t.dualFlipX = dualFlipX->isChecked();
        t.dualFlipY = dualFlipY->isChecked();
        t.dualRandomAngle = dualRandomAngle->value();
        t.dualRandomSpacing = dualRandomSpacing->value();
        t.dualRandomCount = dualRandomCount->value();
        t.dualRandomSize = dualRandomSize->value();
        t.dualRandomOffset = dualRandomOffset->value();
        saveTipShape(t);
    };
    for (QSpinBox* const box : {dualSize, dualSpacing, dualCount, dualRoundness, dualAngle, dualRandomAngle, dualRandomSpacing, dualRandomCount, dualRandomSize, dualRandomOffset})
        connect(box, &QSpinBox::valueChanged, &dialog, apply);
    for (QCheckBox* const box : {follow, dual, dualFlipX, dualFlipY})
        connect(box, &QCheckBox::toggled, &dialog, apply);
    for (QComboBox* const box : {mode, dualKind})
        connect(box, &QComboBox::currentIndexChanged, &dialog, apply);
    for (QSpinBox* const box : {angle, roundness, spacing, count, randomAngle, randomSpacing, randomCount, randomSize, randomOffset, minOpacity})
        connect(box, &QSpinBox::valueChanged, &dialog, apply);
    for (QCheckBox* const box : {flipX, flipY})
        connect(box, &QCheckBox::toggled, &dialog, apply);
    QDialogButtonBox* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [=] {
        Canvas::TipShape const usual;
        angle->setValue(usual.angle);
        roundness->setValue(usual.roundness);
        spacing->setValue(usual.spacing);
        count->setValue(usual.count);
        flipX->setChecked(false);
        flipY->setChecked(false);
        for (QSpinBox* const box : {randomAngle, randomSpacing, randomCount, randomSize, randomOffset})
            box->setValue(0);
        minOpacity->setValue(usual.minOpacity);
    });
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        saveTipShape(was);
}

void ToolProperties::addSnapRows()
{
    // The master switch of snapping: the Select tool, the Contour Editor, the Envelope and the Perspective tool.
    m_snapMaster = new QCheckBox(tr("Snapping"), this);
    m_snapMaster->setObjectName(QStringLiteral("SnapMaster"));
    m_snapMaster->setToolTip(tr("Master switch for all snapping modes: off, nothing snaps - to the grid, to contours, to guides or into line."));
    m_snapMaster->setChecked(settings().value(QStringLiteral("tools/snapping"), true).toBool());
    m_canvas->setSnapping(m_snapMaster->isChecked());
    connect(m_snapMaster, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setSnapping(on);
        settings().setValue(QStringLiteral("tools/snapping"), on);
    });
    m_form->addRow(m_snapMaster);
    // The Select tool: no box and no handles round what is selected.
    m_hideManipulator = new QCheckBox(tr("Hide Manipulator Controls"), this);
    m_hideManipulator->setObjectName(QStringLiteral("SelectHideManipulator"));
    m_hideManipulator->setToolTip(tr("Hides the bounding box and manipulator controls when an element is selected."));
    m_hideManipulator->setChecked(settings().value(QStringLiteral("tools/hideManipulator"), false).toBool());
    m_canvas->setHideManipulator(m_hideManipulator->isChecked());
    connect(m_hideManipulator, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setHideManipulator(on);
        settings().setValue(QStringLiteral("tools/hideManipulator"), on);
    });
    m_form->addRow(m_hideManipulator);
    // The Contour Editor: a box round the points picked, to scale and turn them.
    m_ceControls = new QCheckBox(tr("Show Controls"), this);
    m_ceControls->setObjectName(QStringLiteral("ContourShowControls"));
    m_ceControls->setToolTip(tr("Use to scale, reposition and rotate the selected anchor points: a box round them, its corners and its knob."));
    m_ceControls->setChecked(settings().value(QStringLiteral("tools/contourControls"), false).toBool());
    m_canvas->setContourControls(m_ceControls->isChecked());
    if (QAction* const action = m_actions->action(QStringLiteral("ShowContourControls")))
        action->setChecked(m_ceControls->isChecked());
    connect(m_ceControls, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setContourControls(on);
        settings().setValue(QStringLiteral("tools/contourControls"), on);
        if (QAction* const action = m_actions->action(QStringLiteral("ShowContourControls")))
            action->setChecked(on);
    });
    m_form->addRow(m_ceControls);
    // The Envelope tool: the handles of its points.
    m_envAdvanced = new QCheckBox(tr("Show Advanced Controls"), this);
    m_envAdvanced->setObjectName(QStringLiteral("EnvelopeAdvanced"));
    m_envAdvanced->setToolTip(tr("The handles of the outline's points, to lead it another way through them."));
    m_envAdvanced->setChecked(settings().value(QStringLiteral("tools/envelopeAdvanced"), true).toBool());
    m_canvas->setEnvelopeAdvanced(m_envAdvanced->isChecked());
    connect(m_envAdvanced, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setEnvelopeAdvanced(on);
        settings().setValue(QStringLiteral("tools/envelopeAdvanced"), on);
    });
    m_form->addRow(m_envAdvanced);
    // The Cutter: its gestures, and the ends it leaves on the lines it cuts.
    m_cutterRow = new QWidget(this);
    {
        QFormLayout* const rows = new QFormLayout(m_cutterRow);
        rows->setContentsMargins(0, 0, 0, 0);
        QComboBox* const gesture = new QComboBox(m_cutterRow);
        gesture->setObjectName(QStringLiteral("CutterGesture"));
        gesture->addItems({tr("No Gesture"), tr("Single Line Cutting Gesture"), tr("Multiple Lines Cutting Gesture"), tr("Mouse Gesture Breaker Mode")});
        gesture->setToolTip(tr("Use Mouse Gesture: a quick straight stroke across lines (not round them).\n"
                               "Single Line: deletes the first line segment it crosses.\nMultiple Lines: deletes every line segment it crosses.\n"
                               "Breaker: cuts each pencil line it crosses into two individual lines."));
        gesture->setCurrentIndex(std::clamp(settings().value(QStringLiteral("tools/cutterGesture"), 0).toInt(), 0, 3));
        m_canvas->setCutterGesture(gesture->currentIndex());
        connect(gesture, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setCutterGesture(i);
            settings().setValue(QStringLiteral("tools/cutterGesture"), i);
        });
        QComboBox* const tip = new QComboBox(m_cutterRow);
        tip->setObjectName(QStringLiteral("CutterTip"));
        tip->addItems({tr("Round"), tr("Bevel")});
        tip->setToolTip(tr("How the ends of a pencil line cut by the Cutter are left: rounded, or cut square across."));
        tip->setCurrentIndex(std::clamp(settings().value(QStringLiteral("prefs/cutterTip"), 1).toInt(), 0, 1));
        m_canvas->setCutterTip(tip->currentIndex());
        connect(tip, &QComboBox::currentIndexChanged, this, [this](int i) {
            m_canvas->setCutterTip(i);
            settings().setValue(QStringLiteral("prefs/cutterTip"), i);
        });
        rows->addRow(tr("Gesture"), gesture);
        rows->addRow(tr("Cut Tips"), tip);
    }
    m_form->addRow(m_cutterRow);
    // Reposition All Drawings: a distance typed moves all of every drawing of the layer by it (then the box is back at 0).
    m_repositionRow = new QWidget(this);
    {
        QHBoxLayout* const row = new QHBoxLayout(m_repositionRow);
        row->setContentsMargins(0, 0, 0, 0);
        for (int axis = 0; axis < 2; ++axis) {
            QDoubleSpinBox* const box = new QDoubleSpinBox(m_repositionRow);
            box->setObjectName(axis == 0 ? QStringLiteral("RepositionX") : QStringLiteral("RepositionY"));
            box->setRange(-100000.0, 100000.0);
            box->setDecimals(2);
            box->setKeyboardTracking(false);
            box->setToolTip(axis == 0 ? tr("X Offset: reposition every drawing along the X axis by this much.")
                                      : tr("Y Offset: reposition every drawing along the Y axis by this much."));
            row->addWidget(new QLabel(axis == 0 ? tr("X Offset") : tr("Y Offset"), m_repositionRow));
            row->addWidget(box, 1);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, box, axis](double by) {
                if (by == 0.0)
                    return;
                m_canvas->moveSelectionBy(QPointF(axis == 0 ? by : 0.0, axis == 1 ? by : 0.0), tr("Reposition All Drawings"));
                QSignalBlocker const quiet(box);
                box->setValue(0.0);
            });
        }
    }
    m_form->addRow(m_repositionRow);
}
