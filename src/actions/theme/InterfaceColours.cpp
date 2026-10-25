#include "actions/theme/InterfaceColours.h"

#include <QHash>
#include <QSettings>

namespace InterfaceColours {

QList<Entry> const& entries()
{
    static QList<Entry> const all = {
        {"canvasGrid", "Views", "Grid", QColor(30, 30, 30, 190)},
        {"planningCamera", "Scene Planning", "Camera Cone", QColor(111, 168, 232)},
        {"brushPreview", "Views", "Brush Preview Background", QColor(228, 228, 226)},
        {"smoothBrush", "Views", "Smoothing Tool Brush", QColor(255, 150, 40, 80)},
        {"transformTool", "Camera", "Transform Tool", QColor(255, 170, 40)},
        {"transformHandle", "Camera", "Handle", QColor(255, 255, 255)},
        {"pivotLight", "Camera", "Light Pivot", QColor(255, 170, 40)},
        {"pivotDark", "Camera", "Dark Pivot", QColor(20, 20, 20)},
        {"pegPivotLight", "Camera", "Light Peg Pivot", QColor(120, 220, 120)},
        {"pegPivotDark", "Camera", "Dark Peg Pivot", QColor(20, 20, 20)},
        {"cameraKeyframe", "Camera", "Keyframe", QColor(120, 200, 255)},
        {"controlPoint", "Camera", "Control Point", QColor(255, 190, 70)},
        {"rotationControl1", "Camera", "Rotation Control 1", QColor(240, 90, 90)},
        {"rotationControl2", "Camera", "Rotation Control 2", QColor(255, 200, 80)},
        {"scaleControl", "Camera", "Scale Control", QColor(90, 170, 250)},
        {"skewControl", "Camera", "Skew Control", QColor(170, 120, 240)},
        {"timelineBackground", "Timeline", "Background", QColor(62, 62, 62)},
        {"timelineCurrentLayer", "Timeline", "Current Layer", QColor(96, 96, 96)},
        {"timelineExposed", "Timeline", "Exposed Cell", QColor(122, 122, 122)},
        {"timelineCurrentFrame", "Timeline", "Current Frame", QColor(224, 36, 36)},
        {"timelineKeyframe", "Timeline", "Motion Keyframe", QColor(255, 190, 70)},
        {"timelineStopKeyframe", "Timeline", "Stop-Motion Keyframe", QColor(255, 190, 70)},
        {"timelineKeyMark", "Timeline", "Key Drawing Mark", QColor(232, 76, 61)},
        {"timelineBreakdownMark", "Timeline", "Breakdown Drawing Mark", QColor(80, 150, 240)},
        {"timelineInBetweenMark", "Timeline", "In-between Drawing Mark", QColor(90, 200, 110)},
        {"timelineOnionSkin", "Timeline", "Onion Skin Range", QColor(70, 150, 235)},
        {"timelineSelection", "Timeline", "Selection", QColor(42, 155, 176, 95)},
        {"xsheetBackground", "Exposure Sheet", "Background", QColor(72, 72, 72)},
        {"xsheetPaper", "Exposure Sheet", "Cells", QColor(88, 88, 88)},
        {"xsheetExposed", "Exposure Sheet", "Exposed Cell", QColor(122, 122, 122)},
        {"xsheetCurrentFrame", "Exposure Sheet", "Current Frame", QColor(224, 36, 36)},
        {"xsheetKeyframe", "Exposure Sheet", "Keyframe", QColor(255, 190, 70)},
        {"xsheetCurrentDrawing", "Exposure Sheet", "Current Drawing", QColor(235, 205, 95, 150)},
        {"xsheetLightTable", "Exposure Sheet", "Light Table", QColor(250, 236, 190)},
        {"xsheetOnionSkin", "Exposure Sheet", "Onion Skin", QColor(70, 150, 235)},
        {"xsheetFrameBeat", "Exposure Sheet", "Frame per Beat", QColor(140, 140, 140)},
        {"xsheetBeatBar", "Exposure Sheet", "Beat per Bar", QColor(225, 225, 225)},
        {"xsheetDrawingColumn", "Exposure Sheet", "Drawing Columns", QColor(72, 104, 150)},
        {"xsheetTimingColumn", "Exposure Sheet", "Timing Columns", QColor(104, 128, 76)},
        {"xsheetExpressionColumn", "Exposure Sheet", "Expression Columns", QColor(150, 84, 130)},
        {"xsheetFunctionColumn", "Exposure Sheet", "Function Columns", QColor(170, 124, 44)},
        {"xsheetAnnotationColumn", "Exposure Sheet", "Annotation Columns", QColor(226, 222, 204)},
        {"nodeBackground", "Node View", "Background", QColor(69, 69, 69)},
        {"nodeDrawing", "Node View", "Drawing Nodes", QColor(72, 92, 128)},
        {"nodePeg", "Node View", "Peg Nodes", QColor(150, 108, 30)},
        {"nodeCamera", "Node View", "Camera Nodes", QColor(60, 96, 140)},
        {"nodeSound", "Node View", "Sound Nodes", QColor(52, 120, 84)},
        {"nodeGroup", "Node View", "Group Nodes", QColor(40, 118, 122)},
        {"nodeEffect", "Node View", "Effect Nodes", QColor(128, 70, 110)},
        {"nodeComposite", "Node View", "Composite Nodes", QColor(96, 76, 128)},
        {"nodeDisplay", "Node View", "Display Nodes", QColor(70, 72, 78)},
        {"nodePegCable", "Node View", "Peg Cable", QColor(255, 190, 70)},
        {"nodeImageCable", "Node View", "Image Cable", QColor(111, 168, 232)},
        {"nodeSelected", "Node View", "Element Selected", QColor(255, 255, 255)},
        {"functionBackground", "Function Editors", "Background", QColor(58, 58, 58)},
        {"functionPlot", "Function Editors", "Graph", QColor(46, 46, 46)},
        {"functionGrid", "Function Editors", "Grid", QColor(64, 64, 64)},
        {"functionAxis", "Function Editors", "Zero Line", QColor(110, 110, 110)},
        {"functionCurrentFrame", "Function Editors", "Current Frame", QColor(224, 36, 36)},
        {"functionCurveX", "Function Editors", "X Curve", QColor(232, 96, 84)},
        {"functionCurveY", "Function Editors", "Y Curve", QColor(110, 200, 120)},
        {"functionCurveZ", "Function Editors", "Z Curve", QColor(90, 210, 200)},
        {"functionSelected", "Function Editors", "Point Selected", QColor(235, 60, 60)},
        {"xsheetBezierColumn", "Exposure Sheet", "Bezier Column Colour", QColor(214, 120, 70)},
        {"xsheetEaseColumn", "Exposure Sheet", "Ease Column Colour", QColor(120, 190, 110)},
        {"xsheetVeloColumn", "Exposure Sheet", "Velobased Column Colour", QColor(200, 110, 200)},
        {"xsheetPathColumn", "Exposure Sheet", "3D Path Column Colour", QColor(90, 180, 220)},
        {"functionColumn", "Function Editors", "Function Column Curve", QColor(240, 200, 90)},
        {"functionHandle", "Function Editors", "Handles", QColor(240, 210, 70)},
    };
    return all;
}

static void fill(QHash<QString, QColor>& colours)
{
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    for (Entry const& e : entries()) {
        QColor const own(s.value(QStringLiteral("prefs/colours/") + QLatin1String(e.key)).toString());
        colours.insert(QLatin1String(e.key), own.isValid() ? own : e.usual);
    }
}

static QHash<QString, QColor>& cache()
{
    static QHash<QString, QColor> colours = [] {
        QHash<QString, QColor> read;
        fill(read);
        return read;
    }();
    return colours;
}

QColor get(char const* key)
{
    return cache().value(QLatin1String(key), QColor(255, 0, 255));
}

void set(QString const& key, QColor const& colour)
{
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    if (colour.isValid())
        s.setValue(QStringLiteral("prefs/colours/") + key, colour.name(QColor::HexArgb));
    else
        s.remove(QStringLiteral("prefs/colours/") + key);
    reload();
}

void reload()
{
    fill(cache());
}

QColor markColour(int mark, QColor const& unmarked)
{
    // (1 key, 2 breakdown, 3 in-between, as Drawing::Mark has them.)
    return mark == 1 ? get("timelineKeyMark") : mark == 2 ? get("timelineBreakdownMark") : mark == 3 ? get("timelineInBetweenMark") : unmarked;
}

} // namespace InterfaceColours
