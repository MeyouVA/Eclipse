// Eclipse - the colour palette: the swatches of the scene.  Lines and paint are linked to a
// swatch, so editing a swatch recolours everything drawn with it, on every frame.
#pragma once

#include "EclipseExport.h"

#include <QWidget>

#include "model/scene/Scene.h"

class Document;

class QListWidget;
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
    void showPaletteMenu(QPoint const& pos);
    // Opens the gradient editor for a colour: one colour, a gradient in a line, or a round one.
    void editGradient(int id);
    // A new colour that is a gradient from black to white, to change from there.  Its id.
    int addGradientSwatch();
    // A new colour that is a texture: what has it shows this picture.  Its id (-1 for no picture).
    int addTextureSwatch(QImage const& texture, QString const& name = QString());

signals:
    void currentSwatchChanged(int id);

private:
    // The parts of the view, and what they show.
    QWidget* createPaletteHeader();
    QWidget* createColourBar();
    void showInHand();
    void showColourMenu(QPoint const& pos);
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
};
