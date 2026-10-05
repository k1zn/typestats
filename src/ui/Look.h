#pragma once

#include <QFont>

// The look of the original on every system: its layout is in pixels for an 8 pt MS Sans Serif at 96 dpi.
namespace Look {

// Style, palette and font of the application. Windows: the native style with the font of the original.
// Elsewhere: Fusion with a light palette of classic Windows colours (a dark desktop theme would
// leave the self-drawn white panes in a dark window) and a font of the same pixel size.
void apply();

// A font of the original in points, as the pixels it has at 96 dpi: the same size on every system
// (macOS counts 72 logical dpi, so points there come out a quarter smaller).
int pointsToPixels(int points);
QFont pointFont(const QString &family, int points);

}
