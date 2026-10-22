#pragma once

#include <QColor>
#include <QList>
#include <QString>

namespace InterfaceColours {

// The colour a drawing's mark is shown in (Drawing::Mark: key, breakdown, in-between; anything else: the default).
QColor markColour(int mark, QColor const& unmarked);

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
