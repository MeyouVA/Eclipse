// Eclipse - Preferences, the Animation page: the Transform tool, keyframes, control points, pivots and what new layers start with.
#include "ui/window/MainWindow.h"
#include "ui/io/PreferenceWidgets.h"

#include "actions/manager/ActionManager.h"
#include "canvas/Canvas.h"
#include "model/document/Commands.h"

namespace {

// Each setting of the page: its key, what it is called, its kind and its default.
struct Setting {
    char const* key;
    char const* label;
    char const* tip;
    enum Kind { Check, Whole, Real, Choice } kind;
    double fallback;
    double low = 0.0, high = 1.0, step = 1.0;
    char const* choices = nullptr;    // for a Choice: the items, separated by |
};

Setting const kSettings[] = {
    {nullptr, QT_TRANSLATE_NOOP("Preferences", "Transform Tool"), nullptr, Setting::Check, 0},
    {"spRotationLever", QT_TRANSLATE_NOOP("Preferences", "Use Rotation Lever with Transformation Tools"), QT_TRANSLATE_NOOP("Preferences", "A single lever above the box turns the layer, instead of dragging just outside a corner."), Setting::Check, 0},
    {"spAllowSkew", QT_TRANSLATE_NOOP("Preferences", "Transform Tool Allows Skewing"), QT_TRANSLATE_NOOP("Preferences", "Dragging the top or bottom edge of the box leans the layer over."), Setting::Check, 1},
    {"spDonutPivot", QT_TRANSLATE_NOOP("Preferences", "Use a Donut-Style Pivot in the Transform Tool"), QT_TRANSLATE_NOOP("Preferences", "The pivot is drawn with a hollow centre, so that what is under it shows."), Setting::Check, 0},
    {"spPivotDraggable", QT_TRANSLATE_NOOP("Preferences", "Allow Pivot Dragging in the Camera View"), QT_TRANSLATE_NOOP("Preferences", "The Transform tool's pivot can be moved by dragging it."), Setting::Check, 1},
    {"spLasso", QT_TRANSLATE_NOOP("Preferences", "Transform Tool Allows Lasso Selection"), QT_TRANSLATE_NOOP("Preferences", "Dragged in empty space, the Transform tool draws a lasso round the layers to pick (else a box)."), Setting::Check, 1},
    {"spShowOnEmpty", QT_TRANSLATE_NOOP("Preferences", "Display Transform Tool on Empty Selection"), QT_TRANSLATE_NOOP("Preferences", "The box and handles show even when the layer has nothing drawn on this frame."), Setting::Check, 1},
    {"spAllKeys", QT_TRANSLATE_NOOP("Preferences", "Set Keyframes on All Functions with the Transform Tool"), QT_TRANSLATE_NOOP("Preferences", "A keyframe made with the Transform tool holds every value of the layer, not only those changed."), Setting::Check, 1},
    {"spPointSize", QT_TRANSLATE_NOOP("Preferences", "Pivots and Control Points Size"), QT_TRANSLATE_NOOP("Preferences", "The size in pixels of pivots and control points."), Setting::Whole, 12, 6, 40},
    {"spToolSize", QT_TRANSLATE_NOOP("Preferences", "Transform Tools Size"), QT_TRANSLATE_NOOP("Preferences", "The base size in pixels of the circle of the Rotate, Scale and Skew tools."), Setting::Whole, 200, 40, 800},
    {"spRotationControl", QT_TRANSLATE_NOOP("Preferences", "Rotation Control Size"), QT_TRANSLATE_NOOP("Preferences", "The share of the tools size the rotation arc is drawn at."), Setting::Real, 0.5, 0.05, 2.0, 0.05},
    {"spNudge", QT_TRANSLATE_NOOP("Preferences", "Nudging Increment"), QT_TRANSLATE_NOOP("Preferences", "How far an arrow key moves a layer with the Transform tool, in screen pixels (ten times with Shift)."), Setting::Real, 1.0, 0.01, 100.0, 0.1},
    {"spAnimationMode", QT_TRANSLATE_NOOP("Preferences", "Default Animate Mode"), QT_TRANSLATE_NOOP("Preferences", "The Animate mode Eclipse starts with."), Setting::Choice, 0, 0, 0, 0, QT_TRANSLATE_NOOP("Preferences", "Animate Current Frame|Animate Onion Skin Range|Animate Off")},
    {nullptr, QT_TRANSLATE_NOOP("Preferences", "Keyframes and Control Points"), nullptr, Setting::Check, 0},
    {"snapKeyframe", QT_TRANSLATE_NOOP("Preferences", "Snap Keyframe"), QT_TRANSLATE_NOOP("Preferences", "Changing a stop-motion keyframe changes the next keyframe too, when the two held the same values."), Setting::Check, 0},
    {"controlPointsOnSelected", QT_TRANSLATE_NOOP("Preferences", "Show Control Points on Selected Layers"), QT_TRANSLATE_NOOP("Preferences", "The motion path and its control points show for the current layer whatever tool is in hand."), Setting::Check, 0},
    {"spTension", QT_TRANSLATE_NOOP("Preferences", "Control Point Tension"), QT_TRANSLATE_NOOP("Preferences", "The tension new keyframes and control points start with (-1 to 1)."), Setting::Real, 0.0, -1.0, 1.0, 0.1},
    {"spContinuity", QT_TRANSLATE_NOOP("Preferences", "Control Point Continuity"), QT_TRANSLATE_NOOP("Preferences", "The continuity new keyframes and control points start with (-1 to 1)."), Setting::Real, 0.0, -1.0, 1.0, 0.1},
    {"spBias", QT_TRANSLATE_NOOP("Preferences", "Control Point Bias"), QT_TRANSLATE_NOOP("Preferences", "The bias new keyframes and control points start with (-1 to 1)."), Setting::Real, 0.0, -1.0, 1.0, 0.1},
    {"enforceAngles", QT_TRANSLATE_NOOP("Preferences", "Rotation Angle Enforced by Peg"), QT_TRANSLATE_NOOP("Preferences", "A layer's Min/Max Angle hold on every frame, not only while the tools turn it."), Setting::Check, 0},
    {nullptr, QT_TRANSLATE_NOOP("Preferences", "New Layers"), nullptr, Setting::Check, 0},
    {"pegSeparatePosition", QT_TRANSLATE_NOOP("Preferences", "Default Separate Position for Pegs"), QT_TRANSLATE_NOOP("Preferences", "New pegs use separate mode for position: no path of control points."), Setting::Check, 0},
    {"elementSeparatePosition", QT_TRANSLATE_NOOP("Preferences", "Default Separate Position for Elements"), QT_TRANSLATE_NOOP("Preferences", "New drawing layers use separate mode for position."), Setting::Check, 0},
    {"pegSeparateScale", QT_TRANSLATE_NOOP("Preferences", "Default Separate Scale for Pegs"), QT_TRANSLATE_NOOP("Preferences", "New pegs scale width and height each on its own (else locked in proportion)."), Setting::Check, 1},
    {"elementSeparateScale", QT_TRANSLATE_NOOP("Preferences", "Default Separate Scale for Elements"), QT_TRANSLATE_NOOP("Preferences", "New drawing layers scale width and height each on its own."), Setting::Check, 1},
    {"elementAnimatable", QT_TRANSLATE_NOOP("Preferences", "Element \"Animate Using Animation Tools\" Default Value"), QT_TRANSLATE_NOOP("Preferences", "New drawing layers are moved by the Transform tool themselves (off: the peg above them is)."), Setting::Check, 1},
    {"embeddedPivot", QT_TRANSLATE_NOOP("Preferences", "Embedded Pivot"), QT_TRANSLATE_NOOP("Preferences", "What new drawing layers do with the pivots set on their drawings."), Setting::Choice, 1, 0, 0, 0,
     QT_TRANSLATE_NOOP("Preferences", "Don't Use Embedded Pivot|Apply Embedded Pivot on Drawing Layer|Apply Embedded Pivot on Parent Peg")},
};

QString keyOf(Setting const& setting) { return QStringLiteral("prefs/") + QLatin1String(setting.key); }

}  // namespace

void MainWindow::preferencesAnimation(PreferenceWidgets& w, QSettings& s, QDialog& dialog)
{
    w.animationPage = new QGroupBox(tr("Animation"), &dialog);
    QFormLayout* const form = new QFormLayout(w.animationPage);
    for (Setting const& setting : kSettings) {
        QString const label = QCoreApplication::translate("Preferences", setting.label);
        if (!setting.key) {
            QLabel* const heading = new QLabel(QStringLiteral("<b>%1</b>").arg(label.toHtmlEscaped()), w.animationPage);
            form->addRow(heading);
            continue;
        }
        QWidget* field = nullptr;
        QVariant const value = s.value(keyOf(setting), setting.fallback);
        switch (setting.kind) {
        case Setting::Check: {
            QCheckBox* const box = new QCheckBox(label, w.animationPage);
            box->setChecked(value.toBool());
            form->addRow(box);
            field = box;
            break;
        }
        case Setting::Whole: {
            QSpinBox* const box = new QSpinBox(w.animationPage);
            box->setRange(int(setting.low), int(setting.high));
            box->setValue(value.toInt());
            form->addRow(label, box);
            field = box;
            break;
        }
        case Setting::Real: {
            QDoubleSpinBox* const box = new QDoubleSpinBox(w.animationPage);
            box->setRange(setting.low, setting.high);
            box->setSingleStep(setting.step);
            box->setDecimals(2);
            box->setValue(value.toDouble());
            form->addRow(label, box);
            field = box;
            break;
        }
        case Setting::Choice: {
            QComboBox* const box = new QComboBox(w.animationPage);
            box->addItems(QCoreApplication::translate("Preferences", setting.choices).split(QLatin1Char('|')));
            box->setCurrentIndex(std::clamp(value.toInt(), 0, box->count() - 1));
            form->addRow(label, box);
            field = box;
            break;
        }
        }
        field->setObjectName(QStringLiteral("Pref_") + QLatin1String(setting.key));
        if (setting.tip)
            field->setToolTip(QCoreApplication::translate("Preferences", setting.tip));
        w.animation.insert(QLatin1String(setting.key), field);
    }
}

void MainWindow::saveAnimationPreferences(PreferenceWidgets& w, QSettings& s)
{
    for (Setting const& setting : kSettings) {
        QWidget* const field = setting.key ? w.animation.value(QLatin1String(setting.key)) : nullptr;
        if (auto* const box = qobject_cast<QCheckBox*>(field))
            s.setValue(keyOf(setting), box->isChecked());
        else if (auto* const whole = qobject_cast<QSpinBox*>(field))
            s.setValue(keyOf(setting), whole->value());
        else if (auto* const real = qobject_cast<QDoubleSpinBox*>(field))
            s.setValue(keyOf(setting), real->value());
        else if (auto* const choice = qobject_cast<QComboBox*>(field))
            s.setValue(keyOf(setting), choice->currentIndex());
    }
}

void MainWindow::applyAnimationPreferences(QSettings const& s, bool starting)
{
    auto value = [&s](char const* key) {
        for (Setting const& setting : kSettings)
            if (setting.key && qstrcmp(setting.key, key) == 0)
                return s.value(keyOf(setting), setting.fallback);
        return QVariant();
    };
    Canvas::TransformOptions o = m_canvas->transformOptions();
    o.rotationLever = value("spRotationLever").toBool();
    o.allowSkew = value("spAllowSkew").toBool();
    o.donutPivot = value("spDonutPivot").toBool();
    o.pivotDraggable = value("spPivotDraggable").toBool();
    o.lasso = value("spLasso").toBool();
    o.showOnEmpty = value("spShowOnEmpty").toBool();
    o.allKeys = value("spAllKeys").toBool();
    o.pointSize = value("spPointSize").toInt();
    o.toolSize = value("spToolSize").toInt();
    o.rotationControl = value("spRotationControl").toDouble();
    o.nudge = value("spNudge").toDouble();
    o.snapKeyframe = value("snapKeyframe").toBool();
    o.controlPointsOnSelected = value("controlPointsOnSelected").toBool();
    m_canvas->setTransformOptions(o);
    m_actions->setChecked(QStringLiteral("SnapKeyframe"), o.snapKeyframe);
    Keyframe::defaultTension = value("spTension").toDouble();
    Keyframe::defaultContinuity = value("spContinuity").toDouble();
    Keyframe::defaultBias = value("spBias").toDouble();
    Layer::enforceAngleLimits = value("enforceAngles").toBool();
    Layer::newLayers.pegSeparatePosition = value("pegSeparatePosition").toBool();
    Layer::newLayers.elementSeparatePosition = value("elementSeparatePosition").toBool();
    Layer::newLayers.pegSeparateScale = value("pegSeparateScale").toBool();
    Layer::newLayers.elementSeparateScale = value("elementSeparateScale").toBool();
    Layer::newLayers.animatable = value("elementAnimatable").toBool();
    Layer::newLayers.embeddedPivot = std::clamp(value("embeddedPivot").toInt(), 0, 2);
    m_actions->setChecked(QStringLiteral("StopMotionKeyframeMode"), m_document->stopMotionKeys());
    if (starting) {
        // (The mode Eclipse starts with: the Animate buttons change it afterwards.)
        static char const* const kModes[] = {"AnimateCurrentFrame", "AnimateOnionRange", "AnimateOff"};
        int const mode = std::clamp(value("spAnimationMode").toInt(), 0, 2);
        if (QAction* const action = m_actions->action(QLatin1String(kModes[mode])))
            action->trigger();
    }
}
