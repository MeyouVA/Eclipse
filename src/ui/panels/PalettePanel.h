// Eclipse - the colour palette: the swatches of the scene.  Lines and paint are linked to a
// swatch, so editing a swatch recolours everything drawn with it, on every frame.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include <functional>

#include "model/scene/Scene.h"

class Document;

class QListWidget;
class QMenu;
class QListWidgetItem;

class ECLIPSE_UI_API PalettePanel : public QWidget {
    Q_OBJECT

public:
    explicit PalettePanel(Document* document, QWidget* parent = nullptr);

    int currentSwatch() const { return m_current; }
    void setCurrentSwatch(int id);
    // The palette whose colours are listed (a place in Scene::paletteNames).
    int currentPalette() const { return m_palette; }
    void setCurrentPalette(int palette);
    // The colour in hand moved up or down its palette, or into another palette (which is then shown).
    bool moveCurrentSwatch(int by);
    bool moveCurrentSwatchTo(int palette);
    int toolKind() const { return m_kind; }
    void setToolKind(int kind);
    int colourOf(int kind) const { return kind >= 0 && kind < 3 ? m_colours[kind] : -1; }
    bool coloursLinked() const { return m_linked; }
    void setColoursLinked(bool on);
    // The colours as a grid of patches (their names in tooltips), or as a list with names.
    bool swatchGrid() const { return m_grid; }
    void setSwatchGrid(bool on);
    bool exportPalette(QString const& path, QString* error = nullptr) const;
    bool importPalette(QString const& path, QString* error = nullptr);
    bool linkPalette(QString const& path, QString* error = nullptr);
    void unlinkPalette();
    // A palette of its own with the same colours as the one shown (to change without touching that one).  Its place among the palettes.
    int duplicatePalette();
    // Opens the gradient editor for a colour: one colour, a gradient in a line, or a round one.
    void editGradient(int id);
    // A new colour that is a gradient from black to white, to change from there.  Its id.
    int addGradientSwatch();
    // A new colour that is a texture: what has it shows this picture.  Its id (-1 for no picture).
    int addTextureSwatch(QImage const& texture, QString const& name = QString());

    // The layer worked on (its element palettes are listed), and where the selected layers are asked for.
    void setCurrentLayer(int layer);
    int currentLayer() const { return m_layer; }
    void setSelectedLayers(std::function<QVector<int>()> source) { m_selectedLayers = std::move(source); }
    // The colours selected in the list (the current one first).
    QVector<int> selectedSwatches() const;
    // The colours put aside by Copy (shared by every Colour view).
    static QVector<Swatch> copiedColours();
    // The colours as rows with their values (R, G, B, A) beside their names.
    bool showsValues() const { return m_values; }
    // The palette list over the colours shown or hidden.
    bool showsPaletteList() const;

public slots:
    // Edit menu, while the Colour view is in use.
    void cut();
    void copy();
    void paste();
    void deleteSelection();
    // Colours menu.
    void onActionCopyColourId();
    void onActionPasteColourValues();
    void onActionPasteAsNewColours();
    void onActionPasteAsClones();
    void onActionNewColour();
    void onActionNewDefaultColour();
    void onActionEditColour();
    void onActionNewTexture();
    void onActionEditTexture();
    void onActionScaleDownTexture();
    void onActionRecolourDrawings();
    void onActionProtectColour();
    void onActionSwatchMode(bool on);
    void onActionDisplayColourValues(bool on);
    void onActionRecoverColours();
    void onActionNewGradient();
    void onActionEditGradient();
    void onActionMoveColourUp();
    void onActionMoveColourDown();
    void onShowMoveToPalette(QMenu* menu);
    // Palettes menu.
    void onActionNewPalette();
    void onActionNewElementPalette();
    void onActionNewPaletteFromSelected();
    void onActionLinkPalette();
    void onActionUpdatePaletteFromFile();
    void onActionUnlinkPalette();
    void onActionColourEditor();
    void onActionImportPalette();
    void onActionExportPalette();
    void onActionRemovePalette();
    void onActionRemoveAndDeletePalette();
    void onActionRenamePalette();
    void onActionDuplicatePalette();
    void onActionClonePalette();
    void onActionMergePalettes();
    void onActionMovePaletteUp();
    void onActionMovePaletteDown();
    void onActionOptimizePalettes();
    void onActionTintPanel();
    void onActionPaletteOverrides();
    void onActionShowPaletteList(bool on);
    // What the menus show checked.
    void onShowColoursMenu(QMenu* menu);

signals:
    void currentSwatchChanged(int id);
    // The Colour Editor asked for (MainWindow shows it).
    void colourEditorWanted();
    // One of the view's menus (menu.palettes, menu.colours) wanted at a place on the screen.
    void menuWanted(QString const& id, QPoint const& globalPos);

private:
    // The parts of the view, and what they show.
    QWidget* createPaletteHeader();
    QWidget* createColourBar();
    void showInHand();
    void rebuild();
    void addSwatch();
    void removeSwatch();
    void addPalette();
    void removePalette();
    void renamePalette(QListWidgetItem* item);
    void editColor(QListWidgetItem* item);
    void itemRenamed(QListWidgetItem* item);

    Document* m_document;
    QListWidget* m_list;
    QListWidget* m_palettes;        // the palettes of the scene
    class QLabel* m_inHand;         // the colour that is painted with
    int m_current = -1;
    int m_palette = 0;
    bool m_grid = false;
    int m_kind = 0;
    int m_colours[3] = {-1, -1, -1};
    bool m_linked = true;
    class QToolButton* m_kindButtons[3] = {nullptr, nullptr, nullptr};
    void showKinds();
    bool m_rebuilding = false;
    int m_layer = -1;
    bool m_values = false;
    std::function<QVector<int>()> m_selectedLayers;
    QWidget* m_paletteHeader = nullptr;
    // Colours taken out; when drawings use them, asked first whether to recolour those, unpaint them or keep their look.
    void deleteColours(QVector<int> const& ids, QString const& text);
    QVector<int> layersInScope(int scope) const;
    int pickPalette(QString const& title, QString const& label, bool excludeCurrent);
};
