// Eclipse - the Node Library view: the kinds of layer and effect there are, to add from.
#include "ui/views/NodeLibraryView.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "model/document/Commands.h"
#include "model/scene/Scene.h"

namespace {

struct Entry {
    char const* name;
    char const* what;
    char const* about;
    char const* category;
};

static Entry const kLayers[] = {
    {QT_TR_NOOP("Drawing"), "InsertDrawing", QT_TR_NOOP("A layer of drawings"), QT_TR_NOOP("Image")},
    {QT_TR_NOOP("Peg"), "InsertPeg", QT_TR_NOOP("Moves the layers attached to it"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Camera"), "InsertCamera", QT_TR_NOOP("What the scene is seen through"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Colour-Card"), "InsertColourCard", QT_TR_NOOP("Fills the frame with one colour"), QT_TR_NOOP("Image")},
    {QT_TR_NOOP("Group"), "InsertGroup", QT_TR_NOOP("Holds other layers"), QT_TR_NOOP("Group")},
    {QT_TR_NOOP("Bone"), "InsertBone", QT_TR_NOOP("A peg with a length; what hangs on it turns with it"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Quadmap"), "InsertQuadmap", QT_TR_NOOP("Pulls what is attached to it by four corners"), QT_TR_NOOP("Deformation")},
    {QT_TR_NOOP("Sound"), "ImportSound", QT_TR_NOOP("A sound file, played from a frame on"), QT_TR_NOOP("Sound")},
    {QT_TR_NOOP("Parent Peg"), "InsertParentPeg", QT_TR_NOOP("A peg put over the current layer, which then hangs from it"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Clone"), "CloneLayer", QT_TR_NOOP("Shows the current layer's drawings, with timing of its own"), QT_TR_NOOP("Image")},
    {QT_TR_NOOP("Clone with Timing"), "CloneLayerTiming", QT_TR_NOOP("Shows the current layer's drawings with its timing too"), QT_TR_NOOP("Image")},
    {QT_TR_NOOP("Synced Drawing"), "AddSyncedLayer", QT_TR_NOOP("A drawing layer of its own that keeps the current layer's timing"), QT_TR_NOOP("Image")},
    {QT_TR_NOOP("Symbol"), "CreateEmptySymbol", QT_TR_NOOP("A symbol: layers kept together, shown by one layer"), QT_TR_NOOP("Group")},
    {QT_TR_NOOP("Envelope Deformer"), "CreateEnvelope", QT_TR_NOOP("An outline round what is selected that pulls the artwork about"), QT_TR_NOOP("Deformation")},
    {QT_TR_NOOP("Write"), "WriteOutputs", QT_TR_NOOP("A render kept with the scene: images or a movie, made with Render Write Nodes"), QT_TR_NOOP("Output")},
};
static Entry const kEffects[] = {
    {QT_TR_NOOP("Blur"), "effect:0", QT_TR_NOOP("Softens the layer"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Tint"), "effect:1", QT_TR_NOOP("Colours the layer towards one colour"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Cutter"), "effect:2", QT_TR_NOOP("Cuts the layer by the drawing of another"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Glow"), "effect:3", QT_TR_NOOP("A soft light round the layer"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Shadow"), "effect:4", QT_TR_NOOP("A shadow cast by the layer"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Particles"), "effect:5", QT_TR_NOOP("The layer's drawing thrown out again and again: sparks, rain, smoke"), QT_TR_NOOP("Particle")},
    {QT_TR_NOOP("Transparency"), "effect:6", QT_TR_NOOP("Makes the layer see-through"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Directional Blur"), "effect:7", QT_TR_NOOP("Smears the layer one way, as if it moved fast"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Hue-Saturation"), "effect:8", QT_TR_NOOP("Turns the colours round the wheel, richer or greyer, lighter or darker"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Brightness-Contrast"), "effect:9", QT_TR_NOOP("Makes the layer brighter or darker, with more or less contrast"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Grain"), "effect:10", QT_TR_NOOP("A film grain over the layer, new each frame"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Radial Blur"), "effect:11", QT_TR_NOOP("Smears the layer along the lines from its middle, as if it rushed at the camera"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Colour-Scale"), "effect:12", QT_TR_NOOP("Multiplies the layer's red, green and blue by a colour"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Negate"), "effect:13", QT_TR_NOOP("Turns the layer's colours to their opposites, as a film negative"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Median"), "effect:14", QT_TR_NOOP("Takes out specks and grain while keeping the edges sharp"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Ripple"), "effect:15", QT_TR_NOOP("Rocks the layer with moving waves, as if seen through water or heat"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Tone"), "effect:16", QT_TR_NOOP("Darkens the layer in a colour where the drawing of a matte layer lies over it, as a cartoon tone"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Highlight"), "effect:17", QT_TR_NOOP("Lightens the layer in a colour where the drawing of a matte layer lies over it"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Bloom"), "effect:18", QT_TR_NOOP("The bright parts of the layer glow out over it"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Pixelate"), "effect:19", QT_TR_NOOP("Turns the layer into blocks of colour"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Turbulence"), "effect:20", QT_TR_NOOP("Pushes the layer about with a moving noise, as heat haze or smoke"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Matte Generator"), "effect:21", QT_TR_NOOP("Makes the layer a soft matte round its drawing, for a cutter or a tone to take"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Black & White"), "effect:22", QT_TR_NOOP("Turns the layer's colours grey, as light as they were"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Matte Resize"), "effect:23", QT_TR_NOOP("Grows or shrinks what is painted on the layer, all round"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Matte Blur"), "effect:24", QT_TR_NOOP("Blurs the layer only where the drawing of a matte layer lies over it"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Gradient"), "effect:25", QT_TR_NOOP("Colours the layer from one colour to another, across it or from its middle out"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Channel Selector"), "effect:26", QT_TR_NOOP("One of the layer's channels (red, green, blue or opacity) as grey"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Crop"), "effect:27", QT_TR_NOOP("Keeps only what lies inside a rectangle"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Remove Transparency"), "effect:28", QT_TR_NOOP("Makes what is mostly opaque wholly so, and takes the rest away"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Matte Composite"), "effect:29", QT_TR_NOOP("Turns the layer into a white matte of itself"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Variable Blur"), "effect:31", QT_TR_NOOP("Blurs the layer as much as the drawing of a matte layer is light"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Motion Blur"), "effect:32", QT_TR_NOOP("A fading trail of the layer as it was on the frames before"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Colour Curves"), "effect:34", QT_TR_NOOP("Each colour level changed along a curve: for all three colours, or red, green and blue alone"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Focus"), "effect:35", QT_TR_NOOP("Depth of field: the layer blurred the further it stands from the depth in focus"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Apply Peg Transformation"), "effect:36", QT_TR_NOOP("The picture moved as another layer (a peg) moves"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Apply Camera"), "effect:41", QT_TR_NOOP("What comes in, seen through a second camera (or moving with it); in a graph, each layer in its own depth"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Scale Output"), "effect:37", QT_TR_NOOP("The picture made at a fraction of the resolution"), QT_TR_NOOP("Output")},
    {QT_TR_NOOP("Script"), "effect:39", QT_TR_NOOP("A JavaScript run on the picture each frame"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("External"), "effect:40", QT_TR_NOOP("A program run on the picture each frame (it reads {in} and writes {out})"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Blending"), "effect:30", QT_TR_NOOP("Lays the picture over the one at its second input as a blend mode says (in a graph wired by hand)"), QT_TR_NOOP("Combine")},
    {QT_TR_NOOP("Generic Composite"), "effect:42", QT_TR_NOOP("Puts the picture onto the one at its second input by a colour and an alpha operation (in a graph wired by hand)"), QT_TR_NOOP("Combine")},
    {QT_TR_NOOP("Image Switch"), "effect:47", QT_TR_NOOP("Passes on one of its inputs, chosen by a port index that can be animated (in a graph wired by hand)"), QT_TR_NOOP("Combine")},
    {QT_TR_NOOP("Layer Selector"), "effect:43", QT_TR_NOOP("Only some art layers of the layer that comes in: Line Art, Colour Art, Underlay, Overlay (in a graph wired by hand)"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Chroma Keying"), "effect:44", QT_TR_NOOP("Takes away a backing colour, as a green screen, and its spill from what stays"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("RGB Difference Keyer"), "effect:45", QT_TR_NOOP("Takes away what matches a clean picture of the background at its second input (in a graph wired by hand)"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Luminance Threshold"), "effect:46", QT_TR_NOOP("Keeps what is lighter than a threshold (or, inverted, darker) as a matte"), QT_TR_NOOP("Filter")},
    {QT_TR_NOOP("Visibility"), "effect:48", QT_TR_NOOP("Shows what comes in only in the Camera view, or only when rendered (in a graph wired by hand)"), QT_TR_NOOP("Misc")},
    {QT_TR_NOOP("Field Chart"), "effect:33", QT_TR_NOOP("A 12-field chart of the camera frame over the layer"), QT_TR_NOOP("Generator")},    {QT_TR_NOOP("Blur-Gaussian"), "effect:49", QT_TR_NOOP("A smooth blur, more across or more up and down as you choose"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Blur-Box"), "effect:50", QT_TR_NOOP("A fast blur in passes, across and up and down"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Blur-Radial-Zoom"), "effect:51", QT_TR_NOOP("A blur towards a centre (zoom), round it (radial) or both (spiral)"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Bokeh Blur"), "effect:52", QT_TR_NOOP("Blurs through the shape of a lens, the bright spots spreading most"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Anti-Flicker"), "effect:53", QT_TR_NOOP("A slight blur up and down, against flickering lines on television"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Unsharp Mask"), "effect:71", QT_TR_NOOP("Sharpens the picture: its difference from a blur added back"), QT_TR_NOOP("Blur")},
    {QT_TR_NOOP("Colour-Fade"), "effect:54", QT_TR_NOOP("Fades the colours towards one colour, in RGB, HSV or HSL"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Colour-Override"), "effect:55", QT_TR_NOOP("The layer that comes in drawn in other colours, colour by colour of the palette (in a graph wired by hand)"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Channel-Swap"), "effect:56", QT_TR_NOOP("Each channel taken from another (red from blue, and so on)"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Colour Levels"), "effect:57", QT_TR_NOOP("Input and output black and white, and gamma, for all colours or one channel"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Colour-Screen"), "effect:58", QT_TR_NOOP("Keeps only what lies within ranges of red, green and blue"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Contrast"), "effect:59", QT_TR_NOOP("Pushes the darks and the lights away from a mid point"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Gamma"), "effect:60", QT_TR_NOOP("Gamma correction, for the whole picture or each channel"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Colour Banding"), "effect:61", QT_TR_NOOP("Turns the picture into bands of up to four colours by how light it is"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Dither"), "effect:62", QT_TR_NOOP("A fine noise that hides the steps between colours"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Increase Opacity"), "effect:72", QT_TR_NOOP("Makes the picture more opaque by a factor"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Colour Space"), "effect:74", QT_TR_NOOP("Converts the picture from one colour space to another (Scene Settings: Colour)"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Look-Up Table"), "effect:75", QT_TR_NOOP("Changes the colours through a LUT file: .cube, .3dl, .spi1d, .spi3d, .csp, .clf, .cdl and more"), QT_TR_NOOP("Colour-Correct")},
    {QT_TR_NOOP("Shine"), "effect:63", QT_TR_NOOP("Rays of light coming from the bright parts, away from a light"), QT_TR_NOOP("Light")},
    {QT_TR_NOOP("Lens Flare"), "effect:64", QT_TR_NOOP("The rings and glow of a light shining into the lens"), QT_TR_NOOP("Light")},
    {QT_TR_NOOP("Burn-In"), "effect:65", QT_TR_NOOP("Text over the frame: its number, the scene's name, the time code"), QT_TR_NOOP("Generator")},
    {QT_TR_NOOP("Turbulent Noise"), "effect:66", QT_TR_NOOP("A cloud of noise that can change from frame to frame"), QT_TR_NOOP("Generator")},
    {QT_TR_NOOP("Grid"), "effect:67", QT_TR_NOOP("A grid of cells over the camera frame, numbered along the edges"), QT_TR_NOOP("Generator")},
    {QT_TR_NOOP("Refract"), "effect:68", QT_TR_NOOP("Bends the picture through the bumps of the picture at its second input, as through glass or heat (in a graph wired by hand)"), QT_TR_NOOP("Deformation")},
    {QT_TR_NOOP("Shake"), "effect:69", QT_TR_NOOP("Moves and turns the picture about at random, smoothly"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Quake"), "effect:70", QT_TR_NOOP("Jolts the picture about at random, each jolt held for some frames"), QT_TR_NOOP("Move")},
    {QT_TR_NOOP("Hold Timing"), "effect:76", QT_TR_NOOP("Holds the pictures and moves of what comes in for so many frames (on twos, threes ...)"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Hold Transformation Timing"), "effect:77", QT_TR_NOOP("Holds only the moves of what comes in for so many frames; the drawings change as they do"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Retime"), "effect:78", QT_TR_NOOP("Shows what comes in as at another frame: so many frames on, or the frame a target says"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Retime Transformation"), "effect:79", QT_TR_NOOP("Moves what comes in as at another frame, its drawings as they are"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Loop"), "effect:80", QT_TR_NOOP("Loops the pictures and moves of what comes in: frames Loop Start to Loop End over and over"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Loop Transformation"), "effect:81", QT_TR_NOOP("Loops only the moves of what comes in, to make cycles; the drawings change as they do"), QT_TR_NOOP("Timing")},
    {QT_TR_NOOP("Animate Pencil Texture"), "effect:73", QT_TR_NOOP("The grain of the pencil lines of the layer that comes in moved along them each frame (in a graph wired by hand)"), QT_TR_NOOP("Filter")},
};

}  // namespace

QVector<NodeLibraryView::Item> NodeLibraryView::items()
{
    QVector<Item> out;
    for (Entry const& e : kLayers)
        out.push_back({tr(e.name), QString::fromLatin1(e.what), tr(e.about), tr(e.category), false});
    for (Entry const& e : kEffects)
        out.push_back({tr(e.name), QString::fromLatin1(e.what), tr(e.about), tr(e.category), true});
    return out;
}

NodeLibraryView::NodeLibraryView(QWidget* parent) : QWidget(parent)
{
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("NodeLibrarySearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    // (A list whose items, dragged out, say what they are - to drop in the Node View.)
    class Dragged : public QListWidget {
    public:
        using QListWidget::QListWidget;

    protected:
        QMimeData* mimeData(QList<QListWidgetItem*> const& items) const override
        {
            QString const what = items.isEmpty() ? QString() : items.first()->data(Qt::UserRole).toString();
            if (what.isEmpty())
                return nullptr;
            QMimeData* const data = new QMimeData;
            data->setData(QLatin1String(NodeLibraryView::mimeType()), what.toUtf8());
            return data;
        }
    };
    m_list = new Dragged(this);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setObjectName(QStringLiteral("NodeLibraryList"));
    auto heading = [this](QString const& text) {
        QListWidgetItem* const item = new QListWidgetItem(text, m_list);
        item->setFlags(Qt::NoItemFlags);
        QFont bold = item->font();
        bold.setBold(true);
        item->setFont(bold);
        item->setForeground(QColor(170, 170, 170));
    };
    auto entry = [this](Item const& e) {
        QListWidgetItem* const item = new QListWidgetItem(QStringLiteral("    ") + e.name, m_list);
        item->setData(Qt::UserRole, e.what);
        item->setToolTip(e.about);
        item->setData(Qt::UserRole + 1, e.category);
    };
    heading(tr("Layers"));
    for (Item const& e : items())
        if (!e.effect)
            entry(e);
    heading(tr("Effects (added to the current layer)"));
    for (Item const& e : items())
        if (e.effect)
            entry(e);
    QPushButton* const addButton = new QPushButton(tr("Add"), this);
    addButton->setObjectName(QStringLiteral("NodeLibraryAdd"));
    addButton->setToolTip(tr("Adds what is chosen to the scene. A double click does the same."));
    QVBoxLayout* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    // The kinds there are, down the left: one chosen shows only what is of that kind.
    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("NodeLibraryCategories"));
    loadCategories();
    fillCategories(QString());
    m_categories->setFixedWidth(104);
    QHBoxLayout* const lists = new QHBoxLayout;
    lists->setSpacing(4);
    lists->addWidget(m_categories);
    lists->addWidget(m_list, 1);
    layout->addWidget(m_search);
    layout->addLayout(lists, 1);
    layout->addWidget(addButton, 0, Qt::AlignRight);
    connect(m_search, &QLineEdit::textChanged, this, &NodeLibraryView::filter);
    connect(m_categories, &QListWidget::currentRowChanged, this, [this] { filter(m_search->text()); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { add(item->data(Qt::UserRole).toString()); });
    connect(addButton, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* const item = m_list->currentItem())
            add(item->data(Qt::UserRole).toString());
    });
}

void NodeLibraryView::filter(QString const& text)
{
    // The headings stay; of the rest, those whose name or description has the words (and are of the category chosen).
    QString const category = currentCategory();
    bool const own = m_user.contains(category);
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* const item = m_list->item(i);
        if (item->data(Qt::UserRole).toString().isEmpty())
            continue;
        bool const worded = text.trimmed().isEmpty() || item->text().contains(text.trimmed(), Qt::CaseInsensitive)
                            || item->toolTip().contains(text.trimmed(), Qt::CaseInsensitive);
        bool const kind = m_categories->currentRow() <= 0
                          || (own ? m_user.value(category).contains(item->data(Qt::UserRole).toString()) : item->data(Qt::UserRole + 1).toString() == category);
        item->setHidden(!(worded && kind));
    }
}

void NodeLibraryView::loadCategories()
{
    QSettings const s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    m_user.clear();
    QJsonObject const stored = QJsonDocument::fromJson(s.value(QStringLiteral("nodeLibrary/categories")).toByteArray()).object();
    for (auto it = stored.begin(); it != stored.end(); ++it) {
        QStringList whats;
        for (QJsonValue const& v : it.value().toArray())
            whats << v.toString();
        m_user.insert(it.key(), whats);
    }
}

void NodeLibraryView::saveCategories() const
{
    QJsonObject out;
    for (auto it = m_user.cbegin(); it != m_user.cend(); ++it)
        out[it.key()] = QJsonArray::fromStringList(it.value());
    QSettings s(QStringLiteral("Eclipse"), QStringLiteral("Eclipse"));
    s.setValue(QStringLiteral("nodeLibrary/categories"), QJsonDocument(out).toJson(QJsonDocument::Compact));
}

void NodeLibraryView::fillCategories(QString const& current)
{
    // The kinds there are, then the user's own (in italics).
    QSignalBlocker const quiet(m_categories);
    m_categories->clear();
    m_categories->addItems({tr("All"), tr("Deformation"), tr("Filter"), tr("Group"), tr("Image"), tr("Move"), tr("Sound"), tr("Output"), tr("Particle"), tr("Generator"), tr("Combine"), tr("Misc"), tr("Blur"), tr("Colour-Correct"), tr("Light"), tr("Timing")});
    for (auto it = m_user.cbegin(); it != m_user.cend(); ++it) {
        QListWidgetItem* const item = new QListWidgetItem(it.key(), m_categories);
        QFont italic = item->font();
        italic.setItalic(true);
        item->setFont(italic);
        item->setData(Qt::UserRole, true);
    }
    QList<QListWidgetItem*> const found = current.isEmpty() ? QList<QListWidgetItem*>() : m_categories->findItems(current, Qt::MatchExactly);
    m_categories->setCurrentItem(found.isEmpty() ? m_categories->item(0) : found.first());
    if (m_search)
        filter(m_search->text());
}

QString NodeLibraryView::currentCategory() const
{
    return m_categories && m_categories->currentItem() ? m_categories->currentItem()->text() : QString();
}

QString NodeLibraryView::currentWhat() const
{
    return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
}

void NodeLibraryView::showCategory(QString const& name)
{
    fillCategories(name);
}

bool NodeLibraryView::addCategory(QString const& name)
{
    QString const wanted = name.trimmed();
    if (wanted.isEmpty() || m_user.contains(wanted) || !m_categories->findItems(wanted, Qt::MatchFixedString).isEmpty())
        return false;
    m_user.insert(wanted, {});
    saveCategories();
    fillCategories(wanted);
    return true;
}

bool NodeLibraryView::renameCategory(QString const& from, QString const& to)
{
    QString const wanted = to.trimmed();
    if (!m_user.contains(from) || wanted.isEmpty() || (wanted != from && (m_user.contains(wanted) || !m_categories->findItems(wanted, Qt::MatchFixedString).isEmpty())))
        return false;
    QStringList const whats = m_user.take(from);
    m_user.insert(wanted, whats);
    saveCategories();
    fillCategories(wanted);
    return true;
}

bool NodeLibraryView::removeCategory(QString const& name)
{
    if (!m_user.remove(name))
        return false;
    saveCategories();
    fillCategories(QString());
    return true;
}

bool NodeLibraryView::putInCategory(QString const& what, QString const& category)
{
    if (what.isEmpty() || !m_user.contains(category) || m_user[category].contains(what))
        return false;
    m_user[category].push_back(what);
    saveCategories();
    filter(m_search->text());
    return true;
}

bool NodeLibraryView::takeFromCategory(QString const& what, QString const& category)
{
    if (!m_user.contains(category) || !m_user[category].removeOne(what))
        return false;
    saveCategories();
    filter(m_search->text());
    return true;
}

void NodeLibraryView::onActionNewCategory()
{
    bool okay = false;
    QString const name = QInputDialog::getText(this, tr("New Category"), tr("Name of the category:"), QLineEdit::Normal, QString(), &okay);
    if (okay && !addCategory(name))
        QMessageBox::information(this, tr("New Category"), tr("There is a category of that name already, or the name is empty."));
}

void NodeLibraryView::onActionRenameCategory()
{
    QString const from = currentCategory();
    if (!m_user.contains(from)) {
        QMessageBox::information(this, tr("Rename Category"), tr("Only categories of your own can be renamed. Choose one in the list on the left."));
        return;
    }
    bool okay = false;
    QString const name = QInputDialog::getText(this, tr("Rename Category"), tr("New name:"), QLineEdit::Normal, from, &okay);
    if (okay && !renameCategory(from, name))
        QMessageBox::information(this, tr("Rename Category"), tr("There is a category of that name already, or the name is empty."));
}

void NodeLibraryView::onActionRemoveCategory()
{
    QString const name = currentCategory();
    if (!m_user.contains(name)) {
        QMessageBox::information(this, tr("Remove Category"), tr("Only categories of your own can be removed. Choose one in the list on the left."));
        return;
    }
    removeCategory(name);
}

void NodeLibraryView::onActionRefresh()
{
    loadCategories();
    fillCategories(currentCategory());
}

void NodeLibraryView::onActionRemoveNode()
{
    // Out of the category of the user's that is shown (the node itself stays in the library).
    if (!takeFromCategory(currentWhat(), currentCategory()))
        QMessageBox::information(this, tr("Remove Node"), tr("Choose a category of your own on the left, then the node to take out of it."));
}

void NodeLibraryView::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    if (QListWidgetItem* const item = m_list->itemAt(m_list->viewport()->mapFromGlobal(event->globalPos())); item && !item->data(Qt::UserRole).toString().isEmpty()) {
        m_list->setCurrentItem(item);
        QMenu* const into = menu.addMenu(tr("Add to Category"));
        QString const what = item->data(Qt::UserRole).toString();
        for (auto it = m_user.cbegin(); it != m_user.cend(); ++it) {
            QAction* const action = into->addAction(it.key());
            action->setEnabled(!it.value().contains(what));
            connect(action, &QAction::triggered, this, [this, what, name = it.key()] { putInCategory(what, name); });
        }
        into->setEnabled(!m_user.isEmpty());
        menu.addSeparator();
    }
    if (m_sharedMenu)
        if (QMenu* const shared = m_sharedMenu(&menu))
            for (QAction* action : shared->actions())
                menu.addAction(action);
    if (!menu.isEmpty())
        menu.exec(event->globalPos());
}

void NodeLibraryView::add(QString const& what)
{
    if (what.isEmpty())
        return;
    if (what.startsWith(QLatin1String("effect:")))
        emit effectRequested(what.mid(7).toInt());
    else
        emit actionRequested(what);
}
