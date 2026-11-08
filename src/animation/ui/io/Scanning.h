// Eclipse - scanning drawings in: the settings a scan is made with and what is done to each page (its levels, a colour
// dropped, turned round, registered by its peg holes, vectorised or kept as a bitmap), and the Scan Drawing window that
// puts the pages into the cells of the current layer one after another.
#pragma once

#include "EclipseExport.h"

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QString>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class MainWindow;

struct ECLIPSE_UI_API ScanSettings {
    int dpi = 300;
    enum Colour { BlackAndWhite, Grey, FullColour };
    int colour = Grey;
    int fields = 12;                // Paper Size: so many fields across
    int pegSide = 0;                // top, bottom, left, right
    int whitePoint = 190;           // lighter than this is paper (1..255)
    int blackPoint = 70;            // darker than this is black
    double gamma = 1.05;
    QColor dropColour;              // made paper (invalid: none)
    int threshold = 100;            // black and white: darker than this is line
    bool opticalRegistration = true;
    enum Type { Bitmap, VectorLineArt, VectorColour };
    int type = VectorLineArt;       // Scan Type: what a scanned drawing is made into
    QString vectorizeLine;          // the Vectorization Parameters (empty: those kept for vectorising)
    bool flip = false;              // Flip Drawings: the page turned half round (scanned upside down)
    bool scanAndAdvance = false;
    bool overwrite = false;         // Overwrite Drawings: into the drawing on the cell, not a new one
    int hold = 1;                   // Hold Exposure: so many frames each
    bool pagePanning = false;       // Page Panning: kept at its own size (bigger than the frame), not fitted
    QString scannerName;

    static ScanSettings load();
    void save() const;
    // A page as these settings make it: levels (black point, white point, gamma), the drop colour made paper, the colour
    // mode, turned round, and (with optical registration) put straight by its peg holes.  `notes`: what could not be done.
    QImage process(QImage const& page, QString* notes = nullptr) const;
    // What a scanned drawing keeps of its scan (Show Scan Information).
    QString info(QImage const& page, QString const& file) const;
};

// Scanning Parameters: the settings, in a window.
class ECLIPSE_UI_API ScanConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScanConfigDialog(ScanSettings const& settings, QWidget* parent = nullptr);
    ScanSettings settings() const;

private:
    ScanSettings m_settings;
    std::function<void(ScanSettings&)> m_read;
};

// Scan Drawing: the drawing's name, Scan and Advance / Overwrite / Flip / Hold Exposure / Page Panning, and the button that
// scans the next drawing into the current cell.
class ECLIPSE_UI_API ScanDrawingDialog : public QDialog {
    Q_OBJECT
public:
    ScanDrawingDialog(MainWindow* window, QWidget* parent = nullptr);
    // Scan Drawing: a page from the scanner put in (false when none came, with the reason shown).
    bool scanOne();

private:
    MainWindow* m_window;
    ScanSettings m_settings;
    QLineEdit* m_name = nullptr;
    QCheckBox* m_advance = nullptr;
    QCheckBox* m_overwrite = nullptr;
    QCheckBox* m_flip = nullptr;
    QCheckBox* m_panning = nullptr;
    QSpinBox* m_hold = nullptr;
    QLabel* m_last = nullptr;
};
