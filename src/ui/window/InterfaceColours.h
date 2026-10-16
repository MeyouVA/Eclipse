#pragma once

#include <QColor>
#include <QList>
#include <QString>

// The colours the views paint with, each named, grouped as the Colours page of the Preferences shows them, and kept between
// runs (under prefs/colours/, so a preference set carries them and Restore All Defaults puts them back).
namespace InterfaceColours {

struct Entry {
    char const* key;
    char const* group;
    char const* label;
    QColor usual;
};

// Every colour, in the order the Colours page lists them.
QList<Entry> const& entries();
// The colour named key: the user's, or the usual one. (An unknown key is magenta, so it shows.)
QColor get(char const* key);
// The user's colour for key; an invalid colour puts the usual one back.
void set(QString const& key, QColor const& colour);
// Read them all again from the settings (after a preference set is loaded or the defaults restored).
void reload();

} // namespace InterfaceColours
