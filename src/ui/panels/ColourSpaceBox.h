// Eclipse - choosing a colour space from the colour configuration in use.
#pragma once

#include "EclipseExport.h"

#include <QComboBox>

namespace ColourSpaceBox {

// A list of the colour spaces; `first` (when not empty) heads it and stands for "none chosen" (an empty name).
QComboBox* make(QWidget* parent, QString const& objectName, QString const& current, QString const& first = {});
// Fills an existing list the same way, keeping it quiet.
void fill(QComboBox* box, QString const& current, QString const& first = {});
// The name chosen (empty for the first item when there is one).
QString chosen(QComboBox const* box);
// The colour spaces to choose from, by family where they have one.
QStringList names();

}  // namespace ColourSpaceBox
