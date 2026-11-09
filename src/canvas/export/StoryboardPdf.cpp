// Eclipse - the storyboard printed to a PDF (see StoryboardPdf.h).
#include "canvas/export/StoryboardPdf.h"

#include "canvas/FrameRender.h"
#include "model/scene/Scene.h"

#include <QColor>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonDocument>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace StoryboardPdf {

QVector<Option> const& options()
{
    using T = Option::Type;
    static QVector<Option> const list = [] {
        QString const general = QStringLiteral("General"), page = QStringLiteral("Page"), header = QStringLiteral("Header and Footer"),
                      cover = QStringLiteral("Cover"), panel = QStringLiteral("Panel"), panelHeader = QStringLiteral("Panel Header"),
                      captions = QStringLiteral("Captions"), words = QStringLiteral("Words");
        QStringList const align = {QStringLiteral("Left"), QStringLiteral("Centre"), QStringLiteral("Right")};
        QStringList const valign = {QStringLiteral("Top"), QStringLiteral("Middle"), QStringLiteral("Bottom")};
        return QVector<Option>{
            {"name", general, "Profile name", "The name the profile is saved and chosen by.", T::Text, QStringLiteral("Custom")},
            {"description", general, "Profile description", "What the profile is for.", T::Text, QString()},
            {"icon", general, "Profile icon", "A picture shown beside the profile's name.", T::File, QString()},
            {"unit", general, "Units", "Select the unit of measurement for values used in the page layout.", T::Choice, 0,
             {QStringLiteral("Millimetres"), QStringLiteral("Inches"), QStringLiteral("Points")}},
            {"timecode", general, "Time display", "Select time display units displayed in exported PDF documents.", T::Choice, 0,
             {QStringLiteral("Frames"), QStringLiteral("Time code")}},
            {"font", general, "Default font", "Default font, used for all text in the PDF document.", T::Font, QStringLiteral("Arial")},
            {"fallbackFont", general, "Fallback font", "This determines the default font used for the caption text and titles, in the event that the display font cannot be loaded.", T::Font, QStringLiteral("Arial")},
            {"fontSize", general, "Default font size", "The size of the text, in points, where nothing else says.", T::Int, 9, {}, 4, 72},
            {"embedFonts", general, "Embed fonts", "Set this to embed fonts in the PDF file.", T::Bool, true},
            {"dpi", general, "Image resolution (DPI)", "Sets the panel image output resolution in DPI (dots per inch). Higher values usually produce sharper images, but larger files that take longer to make.", T::Int, 150, {}, 36, 600},

            {"pageSize", page, "Page format", "The size of the paper.", T::Choice, 0,
             {QStringLiteral("Letter"), QStringLiteral("Legal"), QStringLiteral("Tabloid"), QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("Custom Size")}},
            {"pageWidth", page, "Width", "Enter width to use for the page format.", T::Double, 215.9, {}, 10, 2000},
            {"pageHeight", page, "Height", "Enter height to use for the page format.", T::Double, 279.4, {}, 10, 2000},
            {"landscape", page, "Landscape", "The page turned on its side.", T::Bool, false},
            {"rotation", page, "Page rotation", "Sets the page rotation ( degree of clockwise rotation ).", T::Choice, 0,
             {QStringLiteral("0"), QStringLiteral("90"), QStringLiteral("180"), QStringLiteral("270")}},
            {"marginLeft", page, "Left margin", "Enter distance from the left page edge to set the left margin.", T::Double, 12.0, {}, 0, 500},
            {"marginRight", page, "Right margin", "Enter distance from the right page edge to set the right margin.", T::Double, 12.0, {}, 0, 500},
            {"marginTop", page, "Top margin", "Enter distance from the top page edge to set the top margin.", T::Double, 12.0, {}, 0, 500},
            {"marginBottom", page, "Bottom margin", "Enter distance from the bottom page edge to set the bottom margin.", T::Double, 12.0, {}, 0, 500},
            {"columns", page, "Columns", "Sets the number of panel columns displayed per page.", T::Int, 1, {}, 1, 12},
            {"rows", page, "Rows", "Sets the number of panel rows displayed per page.", T::Int, 3, {}, 1, 20},
            {"hSpacing", page, "Column spacing", "Sets the horizontal spacing between panel columns (in the unit type as specified on the general tab).", T::Double, 5.0, {}, 0, 500},
            {"vSpacing", page, "Row spacing", "Sets the vertical spacing between panel rows (in the unit type as specified on the general tab).", T::Double, 5.0, {}, 0, 500},
            {"fullPage", page, "Panel on a full page", "Prints panel on a full page.", T::Bool, false},
            {"emptyPlaceholders", page, "Empty panel placeholders", "Check this to display empty panel placeholders for the rest of the page ( used when there is not enough space on the page for this panel).", T::Bool, false},
            {"newPageShot", page, "Start shots on a new page", "Set this to start scenes on a new page.", T::Bool, false},
            {"newPageSequence", page, "Start sequences on a new page", "Set this to start sequences on a new page.", T::Bool, false},
            {"newPageAct", page, "Start acts on a new page", "Set this to start acts on a new page.", T::Bool, false},
            {"panelSeparator", page, "Line between panels", "Add a separator line between all panels. Compatible only in a single column layout.", T::Bool, false},
            {"shotSeparator", page, "Line between shots", "Add a separator line between all scenes. Compatible only in a single column layout.", T::Bool, false},
            {"mainTable", page, "Main table", "Use this to show or hide main table (background table on each page). Compatible only in a single column layout.", T::Bool, false},
            {"watermark", page, "Watermark", "Select an image to use as a watermark on all pages, except the cover.", T::File, QString()},

            {"header", header, "Page header", "Check this to display a header on each page (except the cover).", T::Bool, true},
            {"headerSize", header, "Header size", "Enter the size of the header displayed on each page ( except the cover).", T::Double, 14.0, {}, 0, 200},
            {"headerFont", header, "Header font", "Selects the header font.", T::Font, QString()},
            {"headerFontSize", header, "Header font size", "Selects the header font size.", T::Int, 10, {}, 4, 72},
            {"title", header, "Project title", "Check this to display the Project Title defined in the project's storyboard settings.", T::Bool, true},
            {"subtitle", header, "Project subtitle", "Check this to display the Project Subtitle defined in the project's storyboard settings.", T::Bool, true},
            {"pageNumber", header, "Page number", "Check this to display a page number in the header on each page ( except the cover).", T::Bool, true},
            {"totalPages", header, "Total pages", "Check this to display the total number of pages in the header.", T::Bool, true},
            {"pageNumberBox", header, "Box around page number", "Check this to draw a box around the page number.", T::Bool, false},
            {"startPage", header, "First page number", "Sets the starting page number in the header.", T::Int, 1, {}, 0, 100000},
            {"pageDuration", header, "Page duration", "Check this to display the page duration.", T::Bool, false},
            {"date", header, "Date", "Set this to display the date of the export.", T::Bool, false},
            {"fileHeader", header, "File name in header", "Set this to include the PDF filename on the header.", T::Bool, false},
            {"actPrefix", header, "Act as page prefix", "Check this to display the act name as the page number prefix in the header.", T::Bool, false},
            {"sequencePrefix", header, "Sequence as page prefix", "Check this to display the sequence name as the page number prefix in the header.", T::Bool, false},
            {"logo", header, "Header logo", "Browse to a logo image (BMP, JPG, or PNG) to display to the left of the header.", T::File, QString()},
            {"logoPosition", header, "Logo position", "Sets the position of the logo image in relation to the header.", T::Choice, 0, {QStringLiteral("Left"), QStringLiteral("Right")}},
            {"footer", header, "Page footer", "A footer at the bottom of each page.", T::Bool, false},
            {"footerSize", header, "Footer size", "Enter the size of the footer (in the units of measurement defined in the General section).", T::Double, 10.0, {}, 0, 200},
            {"footerFont", header, "Footer font", "Selects the default footer font.", T::Font, QString()},
            {"footerFontSize", header, "Footer font size", "Selects the default footer font size .", T::Int, 8, {}, 4, 72},
            {"footerLogo", header, "Footer logo", "Select a logo image (BMP, JPG, or PNG) to display in the footer.", T::File, QString()},
            {"fileFooter", header, "File name in footer", "Set this to include the PDF filename on the footer.", T::Bool, false},

            {"cover", cover, "Cover page", "Set this to insert a cover as the first page of the PDF.", T::Bool, false},
            {"titleFont", cover, "Title font", "Selects the Project Title font.", T::Font, QString()},
            {"titleFontSize", cover, "Title font size", "Selects the Project Title font size.", T::Int, 28, {}, 6, 144},
            {"fileCover", cover, "File name on cover", "Set this to include the PDF filename on the cover.", T::Bool, false},
            {"revisionCover", cover, "Revision note on cover", "Set this to include a revision note on the cover.", T::Bool, false},
            {"revisionNote", cover, "Revision note", "The revision note printed on the cover.", T::Text, QString()},
            {"coverWatermark", cover, "Cover watermark", "Select an image (PNG,BMP or JPG file format) to use as a watermark on the cover.", T::File, QString()},

            {"image", panel, "Panel image", "Check this to display the panel's visual content.", T::Bool, true},
            {"fitting", panel, "Image fitting", "Use this to select the desired fitting for the panel image.", T::Choice, 0,
             {QStringLiteral("Fit"), QStringLiteral("Stretch"), QStringLiteral("Crop")}},
            {"imageScale", panel, "Image scale (%)", "Size, in percentage, that the panel will appear within its frame.", T::Int, 100, {}, 10, 100},
            {"imageFrame", panel, "Frame around image", "Check this to display a rectangle around the panel image.", T::Bool, true},
            {"frameThickness", panel, "Frame thickness", "The thickness used for the camera frame. Default is 1.0.", T::Double, 1.0, {}, 0, 20},
            {"sameScale", panel, "Same scale", "Check this so that all panel images will be displayed at the same scale.", T::Bool, true},
            {"imageWidth", panel, "Image width", "Sets the custom image width (0 means best fit):", T::Double, 0.0, {}, 0, 2000},
            {"imageHeight", panel, "Image height", "Sets the custom image height (0 means best fit):", T::Double, 0.0, {}, 0, 2000},
            {"maxImageWidth", panel, "Maximum image width", "Sets the maximum image width (0 means No Limit):", T::Double, 0.0, {}, 0, 2000},
            {"maxImageHeight", panel, "Maximum image height", "Sets the maximum image height (0 means No Limit):", T::Double, 0.0, {}, 0, 2000},
            {"titleSafe", panel, "Title safe area", "Check this to display the Title Safe Area markers on the image.", T::Bool, false},
            {"actionSafe", panel, "Action safe area", "Check this to display the Action Safe Area markers on the image.", T::Bool, false},
            {"area43", panel, "4:3 area", "Check this to display the 4:3 Area markers on the image.", T::Bool, false},
            {"safe43", panel, "4:3 safe area", "Check this to display the 4:3 Safe Area markers on the image.", T::Bool, false},
            {"hashMarks", panel, "Hash marks", "Check this to display the hash marks on the image.", T::Bool, false},
            {"cameraMask", panel, "Camera mask", "Check this to display the Camera Mask on the image.", T::Bool, false},
            {"maskColour", panel, "Camera mask colour", "The Camera Mask Colour on the image.", T::Colour, QStringLiteral("#c0000000")},
            {"sketch", panel, "Empty sketch space", "Check this to export empty sketches, so that you can draw in your own sketches.", T::Bool, false},
            {"sketchPolicy", panel, "Sketch height policy", "Use this to select the sketch height policy.", T::Choice, 0,
             {QStringLiteral("Automatic Height"), QStringLiteral("Fixed Height"), QStringLiteral("Max Height")}},
            {"sketchHeight", panel, "Sketch height", "Use this to select the sketch height, if using the fixed height sketch policy.", T::Double, 30.0, {}, 0, 1000},

            {"panelHeader", panelHeader, "Panel header", "Check this to display each panel's Shot Name, Panel Number and Duration as a header.", T::Bool, true},
            {"headerVertical", panelHeader, "Vertical header", "Check this to display the panel header vertically", T::Bool, false},
            {"verticalHeaderSize", panelHeader, "Vertical header size", "Enter the size of the header displayed when panel header is vertical.", T::Double, 8.0, {}, 1, 200},
            {"headerSpacing", panelHeader, "Spacing to image", "Enter value for the spacing between panel image and header.", T::Double, 1.0, {}, 0, 100},
            {"headerAlign", panelHeader, "Horizontal alignment", "Sets horizontal alignment of information in panel header.", T::Choice, 0, align},
            {"headerVAlign", panelHeader, "Vertical alignment", "Sets vertical alignment of information in panel header.", T::Choice, 1, valign},
            {"cellsPerLine", panelHeader, "Cells per line", "Sets the maximum cells per line in the panel header.", T::Int, 6, {}, 1, 20},
            {"headerFrame", panelHeader, "Frame around header", "Check this to display a rectangle around the panel header on each page.", T::Bool, false},
            {"shotName", panelHeader, "Shot name", "Check this to display the scene name in the panel header.", T::Bool, true},
            {"panelName", panelHeader, "Panel name", "Check this to display the panel name in the panel header.", T::Bool, true},
            {"panelTotal", panelHeader, "Panel total", "Check this to display the total number of panels in the panel header.", T::Bool, true},
            {"panelDuration", panelHeader, "Panel duration", "Check this to display the Panel duration in the panel header.", T::Bool, true},
            {"shotDuration", panelHeader, "Shot duration", "Check this to display the Scene duration in the panel header.", T::Bool, false},
            {"sequenceName", panelHeader, "Sequence name", "Check this to display the sequence name in the panel header.", T::Bool, false},
            {"actName", panelHeader, "Act name", "Check this to display the act name in the panel header.", T::Bool, false},
            {"sequenceShot", panelHeader, "Sequence and shot together", "Check this to display the Sequence name and Scene name concatenated together in the panel header.", T::Bool, false},
            {"actShot", panelHeader, "Act and shot together", "Check this to display the Act name and Scene name concatenated together in the panel header.", T::Bool, false},
            {"continued", panelHeader, "Continued label", "Check this to display the Continued label when panel is not the first of the scene.", T::Bool, true},
            {"sharedHeader", panelHeader, "Shared header", "Check this so that all exposure of a panel will share their header.", T::Bool, true},
            {"transitions", panelHeader, "Transitions", "Check this to display transition information.", T::Bool, true},
            {"transitionsAsShot", panelHeader, "Transition as shot name", "Check this to display transition types as the scene name.", T::Bool, false},
            {"transitionsFull", panelHeader, "Transitions as full panels", "Check this to display transitions as a full panel.", T::Bool, false},
            {"headerCaptions", panelHeader, "Captions in header", "Caption(s) to display in the panel header...", T::Fields, QString()},
            {"headerCaptionNames", panelHeader, "Caption names in header", "Check this to display the name of selected caption in the panel header.", T::Bool, false},

            {"captions", captions, "Captions", "Opens the List Picker dialog box for selecting specific captions to export.", T::Fields, QString()},
            {"besideCaptions", captions, "Captions beside the image", "Opens the List Picker dialog box for selecting specific captions to export on the other side of the panel image. Only available with caption are beside the image frame.", T::Fields, QString()},
            {"captionPosition", captions, "Caption position", "Use this to select the position of captions relative to panel image.", T::Choice, 0,
             {QStringLiteral("Below the image"), QStringLiteral("Beside the image")}},
            {"captionOrientation", captions, "Caption distribution", "Use this to select the orientation of captions distribution.", T::Choice, 0,
             {QStringLiteral("Vertical"), QStringLiteral("Horizontal")}},
            {"captionColumn", captions, "Extra caption column width", "Sets the custom width for the extra caption column (0 means best fit):", T::Double, 0.0, {}, 0, 1000},
            {"alignCaptionTop", captions, "Align with image top", "Check this to align the top of captions with image frame. Only available with caption are beside the image frame.", T::Bool, true},
            {"captionNames", captions, "Caption field names", "Check this to show caption field name.", T::Bool, true},
            {"captionTitleFontSet", captions, "Own caption title font", "Check this to set a specific font for caption titles, otherwise the default caption font will be used.", T::Bool, false},
            {"captionTitleFont", captions, "Caption title font", "Use this to select the caption title font.", T::Font, QString()},
            {"captionTitleSize", captions, "Caption title size", "Use this to select the font size used for the caption titles.", T::Int, 8, {}, 4, 72},
            {"plainText", captions, "Plain text", "Set this to output captions in plain text (no rich text formatting).", T::Bool, false},
            {"captionFontSize", captions, "Caption font size", "This sets the default font size used for captions, when outputting in plain text format.", T::Int, 9, {}, 4, 72},
            {"captionScale", captions, "Caption text scale (%)", "Sets the scale, in percentage, of the text as it will appear in the captions of this panel.", T::Int, 100, {}, 10, 400},
            {"shrinkCaptions", captions, "Shrink to fit", "Shrinks the size of captions if they do not fit in the panel.", T::Bool, true},
            {"minFontSize", captions, "Smallest font size", "This sets the minimal font size used when shrinking captions.", T::Int, 5, {}, 2, 72},
            {"captionSpacing", captions, "Caption spacing", "Enter value for the spacing between caption of the same panel.", T::Double, 1.5, {}, 0, 100},
            {"captionFrame", captions, "Caption frame", "Select the caption frame style ( no frame, box, line left or line above ).", T::Choice, 0,
             {QStringLiteral("No Frame"), QStringLiteral("Box"), QStringLiteral("Line Left"), QStringLiteral("Line Above")}},
            {"keepCaptionSpace", captions, "Caption area frame", "Check this to display a frame around the caption area. When enabled, the space reserved for captions is preserved even when there are no captions.", T::Bool, false},
            {"writingSpace", captions, "Empty writing space", "Check this to display an empty space where you can write in your own captions, if needed.", T::Bool, false},
            {"removeEmptyLines", captions, "Remove empty lines", "Check this to remove empty lines after captions.", T::Bool, true},
            {"sameCaptionSize", captions, "Same caption size", "Checking this forces all caption boxes to be the same relative size.", T::Bool, false},
            {"distribute", captions, "Distribute across panels", "Adapts the box to the text and distributes it across the next panel if necessary.", T::Bool, false},
            {"revisionNotes", captions, "Revision notes", "Check this to include revision notes.", T::Bool, false},

            {"wordPanel", words, "Panel", "Sets a substitute word for \"Panel\".", T::Text, QStringLiteral("Panel")},
            {"wordNoPanel", words, "No Panel", "Sets a substitute word for \"No Panel\".", T::Text, QStringLiteral("No Panel")},
            {"wordShot", words, "Shot", "Sets a substitute word for \"Scene\".", T::Text, QStringLiteral("Shot")},
            {"wordSequence", words, "Sequence", "Sets a substitute word for \"Sequence\".", T::Text, QStringLiteral("Sequence")},
            {"wordAct", words, "Act", "Sets a substitute word for \"Act\".", T::Text, QStringLiteral("Act")},
            {"wordTransition", words, "Transition", "Sets a substitute word for \"Transition\".", T::Text, QStringLiteral("Transition")},
            {"wordContinue", words, "Continued", "Sets a substitute word for \"Continue\".", T::Text, QStringLiteral("Continued")},
            {"wordDuration", words, "Duration", "Sets a substitute word for \"Duration\".", T::Text, QStringLiteral("Duration")},
            {"wordPage", words, "Page", "Sets a substitute word for \"Page\".", T::Text, QStringLiteral("Page")},
            {"wordTitle", words, "Title", "Sets a substitute word for \"Title\".", T::Text, QStringLiteral("Title")},
            {"wordLayout", words, "Shot Layout", "Sets a substitute word for \"Scene Layout\".", T::Text, QStringLiteral("Shot Layout")},
        };
    }();
    return list;
}

static Option const* optionOf(QString const& key)
{
    for (Option const& o : options())
        if (o.key == key)
            return &o;
    return nullptr;
}

QVariant Profile::value(QString const& key) const
{
    if (values.contains(key))
        return values.value(key).toVariant();
    Option const* o = optionOf(key);
    return o ? o->initial : QVariant();
}

void Profile::set(QString const& key, QVariant const& v)
{
    values.insert(key, QJsonValue::fromVariant(v));
}

QVector<Profile> builtInProfiles()
{
    auto make = [](QString const& name, QString const& description, QJsonObject const& values) {
        Profile p;
        p.values = values;
        p.values["name"] = name;
        p.values["description"] = description;
        p.values["builtIn"] = true;
        return p;
    };
    return {
        make(QStringLiteral("Three Panels, Captions Beside"), QStringLiteral("A column of three panels a page, their captions to the right."),
             {{"columns", 1}, {"rows", 3}, {"captionPosition", 1}, {"mainTable", true}}),
        make(QStringLiteral("Six Panels"), QStringLiteral("Two columns of three panels, captions under them."), {{"columns", 2}, {"rows", 3}}),
        make(QStringLiteral("One Panel a Page"), QStringLiteral("Each panel on a page of its own, landscape, for pitching."),
             {{"fullPage", true}, {"landscape", true}, {"cover", true}}),
        make(QStringLiteral("Thumbnails"), QStringLiteral("Twelve small panels a page, no captions."),
             {{"columns", 3}, {"rows", 4}, {"captions", QStringLiteral("-")}, {"panelDuration", false}}),
    };
}

QString profilesFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absoluteFilePath(QStringLiteral("StoryboardPdfProfiles"));
}

static QString profileFile(QString const& name)
{
    QString safe = name.trimmed();
    for (QChar& c : safe)
        if (QStringLiteral("\\/:*?\"<>|").contains(c))
            c = u'_';
    return QDir(profilesFolder()).absoluteFilePath(safe + QStringLiteral(".json"));
}

QVector<Profile> profiles()
{
    QVector<Profile> out = builtInProfiles();
    QDir const dir(profilesFolder());
    for (QFileInfo const& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        Profile p;
        p.values = QJsonDocument::fromJson(file.readAll()).object();
        p.values.remove(QStringLiteral("builtIn"));
        if (p.name().isEmpty())
            p.set(QStringLiteral("name"), info.completeBaseName());
        out << p;
    }
    return out;
}

bool saveProfile(Profile const& profile, QString* error)
{
    if (profile.name().trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("A profile needs a name.");
        return false;
    }
    QDir().mkpath(profilesFolder());
    QFile file(profileFile(profile.name()));
    QJsonObject values = profile.values;
    values.remove(QStringLiteral("builtIn"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(values).toJson()) < 0) {
        if (error)
            *error = QStringLiteral("Could not create or overwrite file %1").arg(QDir::toNativeSeparators(file.fileName()));
        return false;
    }
    return true;
}

bool removeProfile(QString const& name)
{
    return QFile::remove(profileFile(name));
}

// ---- laying out ----

QVector<QVector<Cell>> layOut(Scene const& scene, Profile const& profile)
{
    Storyboard const& board = scene.storyboard;
    bool const full = profile.flag(QStringLiteral("fullPage"));
    int const perPage = full ? 1 : std::max(1, profile.number(QStringLiteral("columns"))) * std::max(1, profile.number(QStringLiteral("rows")));
    bool const placeholders = profile.flag(QStringLiteral("emptyPlaceholders"));
    bool const transitionCells = profile.flag(QStringLiteral("transitions")) && profile.flag(QStringLiteral("transitionsFull"));
    QVector<QVector<Cell>> pages;
    QVector<Cell> page;
    auto breakPage = [&] {
        if (page.isEmpty())
            return;
        while (placeholders && page.size() < perPage)
            page << Cell{};
        pages << page;
        page.clear();
    };
    auto place = [&](Cell const& cell) {
        if (page.size() >= perPage)
            breakPage();
        page << cell;
    };
    for (int s = 0; s < board.shots.size(); ++s) {
        Storyboard::Shot const& shot = board.shots[s];
        if (s > 0) {
            Storyboard::Shot const& before = board.shots[s - 1];
            if (profile.flag(QStringLiteral("newPageShot")) || (profile.flag(QStringLiteral("newPageSequence")) && shot.sequence != before.sequence)
                || (profile.flag(QStringLiteral("newPageAct")) && shot.act != before.act))
                breakPage();
        }
        if (transitionCells && !shot.transition.isEmpty())
            place(Cell{s, -1});
        for (int p = 0; p < shot.panels.size(); ++p)
            place(Cell{s, p});
    }
    breakPage();
    return pages;
}

// ---- painting ----

namespace {

struct Painter {
    Scene const& scene;
    Profile const& profile;
    QString fileName;
    QPainter& p;
    Storyboard::Naming const& naming;
    PictureSource const& pictures;
    double scale = 1.0;     // device pixels a point

    double unit(QString const& key) const
    {
        // In the profile's units, to device pixels.
        double const v = profile.real(key);
        int const u = profile.number(QStringLiteral("unit"));
        double const points = u == 0 ? v * 72.0 / 25.4 : u == 1 ? v * 72.0 : v;
        return points * scale;
    }
    QFont font(QString const& key, QString const& sizeKey, bool bold = false) const
    {
        QString family = key.isEmpty() ? QString() : profile.text(key);
        if (family.isEmpty())
            family = profile.text(QStringLiteral("font"));
        if (family.isEmpty())
            family = profile.text(QStringLiteral("fallbackFont"));
        QFont f(family);
        f.setPointSizeF(std::max(2, sizeKey.isEmpty() ? profile.number(QStringLiteral("fontSize")) : profile.number(sizeKey)));
        f.setBold(bold);
        return f;
    }
    QString word(QString const& key) const { return profile.text(key); }
    QString time(int frames) const { return Storyboard::duration(frames, scene.fps, profile.number(QStringLiteral("timecode")) == 1); }

    void watermark(QString const& file, QRectF area) const
    {
        QImage const image(file);
        if (image.isNull())
            return;
        p.save();
        p.setOpacity(0.15);
        QSizeF const size = QSizeF(image.size()).scaled(area.size() * 0.8, Qt::KeepAspectRatio);
        p.drawImage(QRectF(area.center() - QPointF(size.width(), size.height()) / 2.0, size), image);
        p.restore();
    }

    void cover(QRectF area) const
    {
        Storyboard const& board = scene.storyboard;
        watermark(profile.text(QStringLiteral("coverWatermark")), area);
        p.setFont(font(QStringLiteral("titleFont"), QStringLiteral("titleFontSize"), true));
        QRectF const titleBox(area.left(), area.top() + area.height() * 0.3, area.width(), area.height() * 0.15);
        p.drawText(titleBox, Qt::AlignCenter | Qt::TextWordWrap, board.title.isEmpty() ? word(QStringLiteral("wordTitle")) : board.title);
        p.setFont(font(QString(), QString()));
        QStringList lines;
        if (!board.subtitle.isEmpty())
            lines << board.subtitle;
        if (profile.flag(QStringLiteral("fileCover")) && !fileName.isEmpty())
            lines << fileName;
        if (profile.flag(QStringLiteral("revisionCover")) && !profile.text(QStringLiteral("revisionNote")).isEmpty())
            lines << profile.text(QStringLiteral("revisionNote"));
        lines << QStringLiteral("%1 %2s, %3 %4s, %5").arg(board.shots.size()).arg(word(QStringLiteral("wordShot")).toLower()).arg(board.panelCount())
                     .arg(word(QStringLiteral("wordPanel")).toLower(), time(board.length()));
        p.drawText(QRectF(area.left(), titleBox.bottom(), area.width(), area.height() * 0.3), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, lines.join(u'\n'));
    }

    void header(QRectF box, int number, int total, QVector<Cell> const& cells) const
    {
        Storyboard const& board = scene.storyboard;
        p.setFont(font(QStringLiteral("headerFont"), QStringLiteral("headerFontSize")));
        QRectF text = box;
        QImage const logo(profile.text(QStringLiteral("logo")));
        if (!logo.isNull()) {
            QSizeF const size = QSizeF(logo.size()).scaled(QSizeF(box.width() / 4.0, box.height()), Qt::KeepAspectRatio);
            bool const right = profile.number(QStringLiteral("logoPosition")) == 1;
            QRectF const at(right ? box.right() - size.width() : box.left(), box.top() + (box.height() - size.height()) / 2.0, size.width(), size.height());
            p.drawImage(at, logo);
            if (right)
                text.setRight(at.left() - 4 * scale);
            else
                text.setLeft(at.right() + 4 * scale);
        }
        QStringList left;
        if (profile.flag(QStringLiteral("title")) && !board.title.isEmpty())
            left << board.title;
        if (profile.flag(QStringLiteral("subtitle")) && !board.subtitle.isEmpty())
            left << board.subtitle;
        if (profile.flag(QStringLiteral("fileHeader")) && !fileName.isEmpty())
            left << fileName;
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, left.join(QStringLiteral(" - ")));
        QStringList right;
        int frames = 0;
        for (Cell const& c : cells)
            if (c.shot >= 0 && c.panel >= 0)
                frames += board.shots[c.shot].panels[c.panel].length;
        if (profile.flag(QStringLiteral("date")))
            right << QDate::currentDate().toString(Qt::ISODate);
        if (profile.flag(QStringLiteral("pageDuration")))
            right << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordDuration")), time(frames));
        if (profile.flag(QStringLiteral("pageNumber"))) {
            QString prefix;
            if (!cells.isEmpty() && cells.first().shot >= 0) {
                Storyboard::Shot const& first = board.shots[cells.first().shot];
                if (profile.flag(QStringLiteral("actPrefix")) && !first.act.isEmpty())
                    prefix += first.act + u'-';
                if (profile.flag(QStringLiteral("sequencePrefix")) && !first.sequence.isEmpty())
                    prefix += first.sequence + u'-';
            }
            QString page = QStringLiteral("%1 %2%3").arg(word(QStringLiteral("wordPage")), prefix).arg(number);
            if (profile.flag(QStringLiteral("totalPages")))
                page += QStringLiteral(" / %1").arg(total);
            right << page;
        }
        QString const rightText = right.join(QStringLiteral("   "));
        QRectF const bound = p.boundingRect(text, Qt::AlignRight | Qt::AlignVCenter, rightText);
        p.drawText(text, Qt::AlignRight | Qt::AlignVCenter, rightText);
        if (profile.flag(QStringLiteral("pageNumberBox")) && profile.flag(QStringLiteral("pageNumber")))
            p.drawRect(bound.adjusted(-2 * scale, -1 * scale, 2 * scale, 1 * scale));
    }

    void footer(QRectF box) const
    {
        p.setFont(font(QStringLiteral("footerFont"), QStringLiteral("footerFontSize")));
        QImage const logo(profile.text(QStringLiteral("footerLogo")));
        QRectF text = box;
        if (!logo.isNull()) {
            QSizeF const size = QSizeF(logo.size()).scaled(QSizeF(box.width() / 4.0, box.height()), Qt::KeepAspectRatio);
            p.drawImage(QRectF(box.right() - size.width(), box.top(), size.width(), size.height()), logo);
            text.setRight(box.right() - size.width() - 4 * scale);
        }
        if (profile.flag(QStringLiteral("fileFooter")) && !fileName.isEmpty())
            p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, fileName);
    }

    QString panelHeaderText(Cell const& cell) const
    {
        Storyboard const& board = scene.storyboard;
        Storyboard::Shot const& shot = board.shots[cell.shot];
        QStringList parts;
        QString shotName = shot.name;
        if (profile.flag(QStringLiteral("transitions")) && profile.flag(QStringLiteral("transitionsAsShot")) && cell.panel == 0 && !shot.transition.isEmpty())
            shotName = shot.transition;
        if (profile.flag(QStringLiteral("actShot")) && !shot.act.isEmpty())
            shotName = shot.act + u'-' + shotName;
        if (profile.flag(QStringLiteral("sequenceShot")) && !shot.sequence.isEmpty())
            shotName = shot.sequence + u'-' + shotName;
        if (profile.flag(QStringLiteral("actName")) && !shot.act.isEmpty() && !profile.flag(QStringLiteral("actShot")))
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordAct")), shot.act);
        if (profile.flag(QStringLiteral("sequenceName")) && !shot.sequence.isEmpty() && !profile.flag(QStringLiteral("sequenceShot")))
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordSequence")), shot.sequence);
        if (profile.flag(QStringLiteral("shotName")))
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordShot")), shotName);
        if (cell.panel < 0) {
            parts << QStringLiteral("%1: %2 (%3)").arg(word(QStringLiteral("wordTransition")), shot.transition, time(shot.transitionLength));
            return parts.join(QStringLiteral("   "));
        }
        if (profile.flag(QStringLiteral("panelName"))) {
            Storyboard::Naming n = naming;
            n.showTotal = false;
            QString name = board.panelName(cell.shot, cell.panel, n);
            if (profile.flag(QStringLiteral("panelTotal")))
                name += QStringLiteral("/%1").arg(shot.panels.size());
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordPanel")), name);
        }
        if (profile.flag(QStringLiteral("panelDuration")))
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordDuration")), time(shot.panels[cell.panel].length));
        if (profile.flag(QStringLiteral("shotDuration")))
            parts << QStringLiteral("%1 %2: %3").arg(word(QStringLiteral("wordShot")), word(QStringLiteral("wordDuration")), time(shot.length()));
        if (profile.flag(QStringLiteral("continued")) && cell.panel > 0)
            parts << QStringLiteral("(%1)").arg(word(QStringLiteral("wordContinue")));
        if (profile.flag(QStringLiteral("transitions")) && !profile.flag(QStringLiteral("transitionsFull")) && cell.panel == 0 && !shot.transition.isEmpty())
            parts << QStringLiteral("%1: %2").arg(word(QStringLiteral("wordTransition")), shot.transition);
        QString const fields = profile.text(QStringLiteral("headerCaptions"));
        for (QString const& field : fields.split(u',', Qt::SkipEmptyParts)) {
            QString const text = board.caption(cell.shot, cell.panel, field.trimmed()).section(u'\n', 0, 0);
            if (!text.isEmpty())
                parts << (profile.flag(QStringLiteral("headerCaptionNames")) ? QStringLiteral("%1: %2").arg(field.trimmed(), text) : text);
        }
        // (So many to a line.)
        int const perLine = std::max(1, profile.number(QStringLiteral("cellsPerLine")));
        QStringList lines;
        for (int i = 0; i < parts.size(); i += perLine)
            lines << parts.mid(i, perLine).join(QStringLiteral("   "));
        return lines.join(u'\n');
    }

    // The captions a panel shows, (field, text) - those listed, or all when none are ("-": none).
    QVector<QPair<QString, QString>> captionsOf(Cell const& cell, QString const& key) const
    {
        QVector<QPair<QString, QString>> out;
        if (cell.panel < 0)
            return out;
        QString const list = profile.text(key).trimmed();
        if (list == QStringLiteral("-"))
            return out;
        Storyboard const& board = scene.storyboard;
        QStringList fields = list.isEmpty() ? board.captionFields : list.split(u',', Qt::SkipEmptyParts);
        if (key == QStringLiteral("captions") && list.isEmpty()) {
            // (Those printed beside the image are not printed again.)
            for (QString const& beside : profile.text(QStringLiteral("besideCaptions")).split(u',', Qt::SkipEmptyParts))
                fields.removeAll(beside.trimmed());
        }
        for (QString field : fields) {
            field = field.trimmed();
            QString text = board.caption(cell.shot, cell.panel, field);
            if (profile.flag(QStringLiteral("removeEmptyLines"))) {
                QStringList lines = text.split(u'\n');
                lines.removeIf([](QString const& l) { return l.trimmed().isEmpty(); });
                text = lines.join(u'\n');
            }
            if (!text.isEmpty() || profile.flag(QStringLiteral("keepCaptionSpace")))
                out << qMakePair(field, text);
        }
        return out;
    }

    void captionBox(QRectF area, QVector<QPair<QString, QString>> const& captions) const
    {
        if (area.width() <= 1 || area.height() <= 1)
            return;
        if (profile.flag(QStringLiteral("keepCaptionSpace")))
            p.drawRect(area);
        bool const across = profile.number(QStringLiteral("captionOrientation")) == 1;
        double const spacing = unit(QStringLiteral("captionSpacing"));
        int const n = int(captions.size()) + (profile.flag(QStringLiteral("writingSpace")) ? 1 : 0);
        if (n == 0)
            return;
        // Each caption's share: equal, or by how much it says.
        QVector<double> weights;
        for (auto const& c : captions)
            weights << (profile.flag(QStringLiteral("sameCaptionSize")) ? 1.0 : 1.0 + c.second.count(u'\n') + c.second.size() / 60.0);
        if (profile.flag(QStringLiteral("writingSpace")))
            weights << 1.0;
        double total = 0.0;
        for (double w : std::as_const(weights))
            total += w;
        double const room = (across ? area.width() : area.height()) - spacing * (n - 1);
        double at = across ? area.left() : area.top();
        int const frame = profile.number(QStringLiteral("captionFrame"));
        int const baseSize = std::max(2, profile.number(QStringLiteral("captionFontSize")) * profile.number(QStringLiteral("captionScale")) / 100);
        for (int i = 0; i < n; ++i) {
            double const share = room * weights[i] / total;
            QRectF const box = across ? QRectF(at, area.top(), share, area.height()) : QRectF(area.left(), at, area.width(), share);
            at += share + spacing;
            if (frame == 1)
                p.drawRect(box);
            else if (frame == 2)
                p.drawLine(box.topLeft(), box.bottomLeft());
            else if (frame == 3)
                p.drawLine(box.topLeft(), box.topRight());
            QRectF const inner = box.adjusted(2 * scale, 1 * scale, -2 * scale, -1 * scale);
            if (i >= captions.size()) {
                // Empty writing space: ruled lines.
                p.save();
                p.setPen(QPen(QColor(170, 170, 170), 0.5 * scale));
                for (double y = inner.top() + 12 * scale; y < inner.bottom(); y += 12 * scale)
                    p.drawLine(QPointF(inner.left(), y), QPointF(inner.right(), y));
                p.restore();
                continue;
            }
            QString const name = captions[i].first, text = captions[i].second;
            bool const names = profile.flag(QStringLiteral("captionNames"));
            bool const plain = profile.flag(QStringLiteral("plainText"));
            // Shrunk until it fits (no smaller than the least size).
            int size = baseSize;
            int const least = std::max(2, profile.number(QStringLiteral("minFontSize")));
            QFont body = font(QString(), QString());
            QFont title = profile.flag(QStringLiteral("captionTitleFontSet")) ? font(QStringLiteral("captionTitleFont"), QStringLiteral("captionTitleSize"), !plain)
                                                                              : font(QString(), QString(), !plain);
            QRectF textArea = inner;
            for (;;) {
                body.setPointSizeF(size);
                if (!profile.flag(QStringLiteral("captionTitleFontSet")))
                    title.setPointSizeF(size);
                double const titleHeight = names ? QFontMetricsF(title, p.device()).height() : 0.0;
                textArea = inner.adjusted(0, titleHeight, 0, 0);
                QRectF const needed = QFontMetricsF(body, p.device()).boundingRect(textArea, Qt::TextWordWrap, text);
                if (needed.height() <= textArea.height() || !profile.flag(QStringLiteral("shrinkCaptions")) || size <= least)
                    break;
                --size;
            }
            if (names) {
                p.setFont(title);
                p.drawText(inner, Qt::AlignLeft | Qt::AlignTop, name);
            }
            p.setFont(body);
            p.save();
            p.setClipRect(textArea);
            p.drawText(textArea, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
            p.restore();
        }
    }

    void image(QRectF box, int frame) const
    {
        QSizeF const sceneSize(scene.width, scene.height);
        int const fitting = profile.number(QStringLiteral("fitting"));
        double const share = std::clamp(profile.number(QStringLiteral("imageScale")), 10, 100) / 100.0;
        QRectF area = box;
        QSizeF size = fitting == 1 ? box.size() : sceneSize.scaled(box.size(), fitting == 2 ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        size *= share;
        area = QRectF(box.center() - QPointF(size.width(), size.height()) / 2.0, size);
        QRectF const shown = fitting == 2 ? area.intersected(box) : area;
        if (profile.flag(QStringLiteral("cameraMask")) && fitting == 0) {
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(profile.text(QStringLiteral("maskColour"))));
            QPainterPath outside;
            outside.addRect(box);
            QPainterPath inside;
            inside.addRect(area);
            p.drawPath(outside.subtracted(inside));
            p.restore();
        }
        if (profile.flag(QStringLiteral("image"))) {
            // Rendered at the profile's resolution.
            double const dpi = std::clamp(profile.number(QStringLiteral("dpi")), 36, 600);
            QSize const pixels = (area.size() / scale * dpi / 72.0).toSize().expandedTo(QSize(8, 8));
            QImage const picture = pictures ? pictures(frame, pixels)
                : FrameRender::frame(scene, std::clamp(frame, 0, scene.frameCount() - 1), pixels, false, scene.background);
            p.save();
            p.setClipRect(shown);
            p.drawImage(area, picture);
            p.restore();
        }
        p.save();
        p.setBrush(Qt::NoBrush);
        if (profile.flag(QStringLiteral("imageFrame")))
            p.setPen(QPen(Qt::black, std::max(0.1, profile.real(QStringLiteral("frameThickness"))) * scale)), p.drawRect(shown);
        p.setPen(QPen(QColor(220, 60, 60), 0.6 * scale, Qt::DashLine));
        auto inset = [&](QRectF r, double part) { return QRectF(r.center() - QPointF(r.width(), r.height()) * part / 2.0, r.size() * part); };
        QRectF const area43(area.center() - QPointF(area.height() * 4.0 / 3.0, area.height()) / 2.0, QSizeF(area.height() * 4.0 / 3.0, area.height()));
        if (profile.flag(QStringLiteral("actionSafe")))
            p.drawRect(inset(area, 0.9));
        if (profile.flag(QStringLiteral("titleSafe")))
            p.drawRect(inset(area, 0.8));
        if (profile.flag(QStringLiteral("area43")))
            p.drawRect(area43.intersected(area));
        if (profile.flag(QStringLiteral("safe43")))
            p.drawRect(inset(area43, 0.9).intersected(area));
        if (profile.flag(QStringLiteral("hashMarks"))) {
            p.setPen(QPen(Qt::black, 0.6 * scale));
            double const tick = std::min(area.width(), area.height()) * 0.04;
            for (double x : {area.center().x()})
                p.drawLine(QPointF(x, area.top()), QPointF(x, area.top() + tick)), p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() - tick));
            for (double y : {area.center().y()})
                p.drawLine(QPointF(area.left(), y), QPointF(area.left() + tick, y)), p.drawLine(QPointF(area.right(), y), QPointF(area.right() - tick, y));
        }
        p.restore();
    }

    void cell(QRectF box, Cell const& c) const
    {
        Storyboard const& board = scene.storyboard;
        if (c.shot < 0) {
            // An empty placeholder.
            p.save();
            p.setPen(QPen(QColor(180, 180, 180), 0.5 * scale, Qt::DashLine));
            p.drawRect(box);
            p.drawText(box, Qt::AlignCenter, word(QStringLiteral("wordNoPanel")));
            p.restore();
            return;
        }
        bool const beside = profile.number(QStringLiteral("captionPosition")) == 1;
        bool const vertical = profile.flag(QStringLiteral("headerVertical"));
        QRectF rest = box;
        if (profile.flag(QStringLiteral("panelHeader"))) {
            QFont const f = font(QString(), QString(), true);
            p.setFont(f);
            QString const text = panelHeaderText(c);
            QFontMetricsF const metrics(f, p.device());
            double const lines = text.count(u'\n') + 1;
            int const ha = profile.number(QStringLiteral("headerAlign")), va = profile.number(QStringLiteral("headerVAlign"));
            Qt::Alignment const align = Qt::Alignment(ha == 0 ? Qt::AlignLeft : ha == 1 ? Qt::AlignHCenter : Qt::AlignRight)
                                        | (va == 0 ? Qt::AlignTop : va == 1 ? Qt::AlignVCenter : Qt::AlignBottom);
            double const gap = unit(QStringLiteral("headerSpacing"));
            if (vertical) {
                double const w = std::max(metrics.height() * lines, unit(QStringLiteral("verticalHeaderSize")));
                QRectF const strip(rest.left(), rest.top(), w, rest.height());
                p.save();
                p.translate(strip.bottomLeft());
                p.rotate(-90);
                p.drawText(QRectF(0, 0, strip.height(), strip.width()), align, text);
                p.restore();
                if (profile.flag(QStringLiteral("headerFrame")))
                    p.drawRect(strip);
                rest.setLeft(strip.right() + gap);
            } else {
                QRectF const strip(rest.left(), rest.top(), rest.width(), metrics.height() * lines);
                p.drawText(strip, align, text);
                if (profile.flag(QStringLiteral("headerFrame")))
                    p.drawRect(strip);
                rest.setTop(strip.bottom() + gap);
            }
        }
        if (c.panel < 0) {
            // A transition as a panel of its own.
            Storyboard::Shot const& shot = board.shots[c.shot];
            p.save();
            p.setPen(QPen(Qt::black, 0.6 * scale));
            p.drawRect(rest);
            p.drawLine(rest.topLeft(), rest.bottomRight());
            p.drawLine(rest.bottomLeft(), rest.topRight());
            p.setFont(font(QString(), QString(), true));
            p.drawText(rest, Qt::AlignCenter, QStringLiteral("%1\n%2").arg(shot.transition, time(shot.transitionLength)));
            p.restore();
            return;
        }
        QVector<QPair<QString, QString>> const under = captionsOf(c, QStringLiteral("captions"));
        QVector<QPair<QString, QString>> side;
        if (beside && !profile.text(QStringLiteral("besideCaptions")).trimmed().isEmpty())
            side = captionsOf(c, QStringLiteral("besideCaptions"));
        // The picture's box: as the profile sizes it, else as big as leaves room for the captions.
        double const aspect = double(scene.width) / std::max(1, scene.height);
        QRectF picture = rest;
        double sideWidth = 0.0;
        if (beside) {
            sideWidth = profile.real(QStringLiteral("captionColumn")) > 0.0 ? unit(QStringLiteral("captionColumn")) : rest.width() * 0.45;
            picture.setWidth(rest.width() - sideWidth - 4 * scale);
            if (!side.isEmpty() && !under.isEmpty())
                picture.setHeight(rest.height() * 0.7);
        } else if (!under.isEmpty() || profile.flag(QStringLiteral("writingSpace")) || profile.flag(QStringLiteral("keepCaptionSpace"))) {
            picture.setHeight(std::min(rest.height() * 0.6, picture.width() / aspect));
        }
        if (profile.flag(QStringLiteral("sketch"))) {
            double const sketchShare = profile.number(QStringLiteral("sketchPolicy")) == 0 ? picture.height() * 0.4 : unit(QStringLiteral("sketchHeight"));
            picture.setHeight(std::max(10.0, picture.height() - sketchShare));
        }
        double w = picture.width(), h = picture.height();
        if (profile.real(QStringLiteral("imageWidth")) > 0.0)
            w = std::min(w, unit(QStringLiteral("imageWidth")));
        if (profile.real(QStringLiteral("imageHeight")) > 0.0)
            h = std::min(h, unit(QStringLiteral("imageHeight")));
        if (profile.real(QStringLiteral("maxImageWidth")) > 0.0)
            w = std::min(w, unit(QStringLiteral("maxImageWidth")));
        if (profile.real(QStringLiteral("maxImageHeight")) > 0.0)
            h = std::min(h, unit(QStringLiteral("maxImageHeight")));
        QSizeF const fitted = QSizeF(aspect, 1.0).scaled(QSizeF(w, h), Qt::KeepAspectRatio);
        QRectF const imageBox(picture.left(), picture.top(), profile.number(QStringLiteral("fitting")) == 0 ? fitted.width() : w,
                              profile.number(QStringLiteral("fitting")) == 0 ? fitted.height() : h);
        image(imageBox, board.panelStart(c.shot, c.panel));
        double below = imageBox.bottom() + 2 * scale;
        if (profile.flag(QStringLiteral("sketch"))) {
            double const sketchHeight = profile.number(QStringLiteral("sketchPolicy")) == 1 ? unit(QStringLiteral("sketchHeight"))
                                                                                           : std::min(rest.bottom() - below, profile.number(QStringLiteral("sketchPolicy")) == 2 ? unit(QStringLiteral("sketchHeight")) : imageBox.height() * 0.6);
            QRectF const sketch(imageBox.left(), below, imageBox.width(), std::max(0.0, sketchHeight));
            p.save();
            p.setPen(QPen(QColor(150, 150, 150), 0.5 * scale));
            p.drawRect(sketch);
            p.restore();
            below = sketch.bottom() + 2 * scale;
        }
        if (beside) {
            QRectF const column(rest.right() - sideWidth, profile.flag(QStringLiteral("alignCaptionTop")) ? imageBox.top() : rest.top(), sideWidth,
                                rest.bottom() - (profile.flag(QStringLiteral("alignCaptionTop")) ? imageBox.top() : rest.top()));
            captionBox(column, side.isEmpty() ? under : side);
            if (!side.isEmpty())
                captionBox(QRectF(rest.left(), below, imageBox.width(), rest.bottom() - below), under);
        } else {
            captionBox(QRectF(rest.left(), below, rest.width(), rest.bottom() - below), under);
        }
    }
};

}   // namespace

bool writeWithFrames(Scene const& scene, Profile const& profile, QString const& path, QString const& sceneFile,
                     Storyboard::Naming const& naming, PictureSource const& pictures, QString* error, int* pageCount)
{
    if (scene.storyboard.isEmpty()) {
        if (error)
            *error = QStringLiteral("The scene has no storyboard: make shots in the Storyboard view, or import an animatic.");
        return false;
    }
    QVector<QVector<Cell>> const pages = layOut(scene, profile);
    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("Eclipse"));
    writer.setTitle(scene.storyboard.title);
    int const dpi = 300;
    writer.setResolution(dpi);
    int const sizeChoice = profile.number(QStringLiteral("pageSize"));
    QPageSize const pageSize = sizeChoice == 1 ? QPageSize(QPageSize::Legal) : sizeChoice == 2 ? QPageSize(QPageSize::Tabloid)
                               : sizeChoice == 3 ? QPageSize(QPageSize::A4) : sizeChoice == 4 ? QPageSize(QPageSize::A3)
                               : sizeChoice == 5 ? QPageSize(QSizeF(profile.real(QStringLiteral("pageWidth")), profile.real(QStringLiteral("pageHeight"))),
                                                             profile.number(QStringLiteral("unit")) == 1 ? QPageSize::Inch : profile.number(QStringLiteral("unit")) == 2 ? QPageSize::Point : QPageSize::Millimeter)
                                                 : QPageSize(QPageSize::Letter);
    int const rotation = profile.number(QStringLiteral("rotation")) * 90;
    bool const across = profile.flag(QStringLiteral("landscape")) != (rotation == 90 || rotation == 270);
    writer.setPageSize(pageSize);
    writer.setPageOrientation(across ? QPageLayout::Landscape : QPageLayout::Portrait);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&writer)) {
        if (error)
            *error = QStringLiteral("Could not create or overwrite file %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    Painter paint{scene, profile, sceneFile.isEmpty() ? QString() : QFileInfo(sceneFile).fileName(), p, naming, pictures};
    paint.scale = dpi / 72.0;
    QRectF const sheet(0, 0, writer.width(), writer.height());
    auto pageArea = [&]() {
        // A page turned a quarter is laid out on its other side, then turned.
        p.resetTransform();
        QRectF area = sheet;
        if (rotation) {
            p.translate(sheet.center());
            p.rotate(rotation);
            if (rotation == 90 || rotation == 270)
                area = QRectF(0, 0, sheet.height(), sheet.width());
            p.translate(-area.center());
        }
        return area.adjusted(paint.unit(QStringLiteral("marginLeft")), paint.unit(QStringLiteral("marginTop")), -paint.unit(QStringLiteral("marginRight")),
                             -paint.unit(QStringLiteral("marginBottom")));
    };
    bool first = true;
    auto newPage = [&] {
        if (!first)
            writer.newPage();
        first = false;
    };
    if (profile.flag(QStringLiteral("cover"))) {
        newPage();
        paint.cover(pageArea());
    }
    int const columns = profile.flag(QStringLiteral("fullPage")) ? 1 : std::max(1, profile.number(QStringLiteral("columns")));
    int const rows = profile.flag(QStringLiteral("fullPage")) ? 1 : std::max(1, profile.number(QStringLiteral("rows")));
    int const start = profile.number(QStringLiteral("startPage"));
    for (int n = 0; n < pages.size(); ++n) {
        newPage();
        QRectF area = pageArea();
        paint.watermark(profile.text(QStringLiteral("watermark")), area);
        p.setPen(QPen(Qt::black, 0.5 * paint.scale));
        if (profile.flag(QStringLiteral("header"))) {
            QRectF const box(area.left(), area.top(), area.width(), paint.unit(QStringLiteral("headerSize")));
            paint.header(box, start + n, start + int(pages.size()) - 1 + (start == 0 ? 1 : 0), pages[n]);
            area.setTop(box.bottom() + 2 * paint.scale);
        }
        if (profile.flag(QStringLiteral("footer"))) {
            QRectF const box(area.left(), area.bottom() - paint.unit(QStringLiteral("footerSize")), area.width(), paint.unit(QStringLiteral("footerSize")));
            paint.footer(box);
            area.setBottom(box.top() - 2 * paint.scale);
        }
        double const hGap = paint.unit(QStringLiteral("hSpacing")), vGap = paint.unit(QStringLiteral("vSpacing"));
        double const cw = (area.width() - hGap * (columns - 1)) / columns, ch = (area.height() - vGap * (rows - 1)) / rows;
        if (profile.flag(QStringLiteral("mainTable")) && columns == 1)
            p.drawRect(area);
        for (int i = 0; i < pages[n].size(); ++i) {
            int const r = i / columns, c = i % columns;
            QRectF const box(area.left() + c * (cw + hGap), area.top() + r * (ch + vGap), cw, ch);
            paint.cell(box.adjusted(2 * paint.scale, 2 * paint.scale, -2 * paint.scale, -2 * paint.scale), pages[n][i]);
            // Lines between panels, or only between shots (one column).
            if (columns == 1 && i + 1 < pages[n].size()) {
                bool const newShot = pages[n][i + 1].shot != pages[n][i].shot;
                if (profile.flag(QStringLiteral("panelSeparator")) || (newShot && profile.flag(QStringLiteral("shotSeparator")))
                    || profile.flag(QStringLiteral("mainTable"))) {
                    double const y = box.bottom() + vGap / 2.0;
                    p.save();
                    p.setPen(QPen(Qt::black, (newShot ? 1.2 : 0.5) * paint.scale));
                    p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
                    p.restore();
                }
            }
        }
    }
    p.end();
    if (pageCount)
        *pageCount = int(pages.size()) + (profile.flag(QStringLiteral("cover")) ? 1 : 0);
    return true;
}

}
