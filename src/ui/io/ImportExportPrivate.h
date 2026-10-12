// Eclipse - what importing and exporting have in common.
#pragma once

#include "ui/window/MainWindow.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QInputDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include "audio/sound/Sound.h"
#include "model/document/Commands.h"
#include "model/geometry/Vectorize.h"
#include "ui/canvas/Canvas.h"
#include "ui/panels/LibraryView.h"
#include "ui/io/MovieExport.h"
#include "ui/panels/PalettePanel.h"
#include "ui/timeline/Timeline.h"

inline QString const kImageFilter =
    QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.tga *.webp *.svg);;All Files (*)");
