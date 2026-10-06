#pragma once

#include <QColor>
#include <QFont>

// The look of the original on every system: its layout is in pixels for an 8 pt MS Sans Serif at 96 dpi.
// Plus a dark theme of the remake (the original has none), switched by the button in the corner of the toolbar.
namespace Look {

// Style, palette and font of the application, in the theme saved by the user (`DarkTheme`; at the first start the
// system's). Windows: the native style
// with the font of the original. Elsewhere: Fusion with a light palette of classic Windows colours (a dark desktop
// theme would leave the self-drawn white panes in a dark window) and a font of the same pixel size. The dark theme is
// Fusion with a dark palette everywhere (the native style of Windows has no dark variant).
void apply();
// Only the theme of apply(): the saved one; at the first start (no `DarkTheme` yet) the system's, which is saved.
void applySavedTheme();

// Switches the theme now and saves it (`save` = false: for this run only, `--theme`). The self-drawn panes take their colours from colors() when they paint; what
// keeps colours of its own (the text, panels with a palette) is refreshed by its owner (MainWindow::setDarkTheme).
void setDark(bool dark, bool save = true);
bool isDark();

// The colours of the self-drawn panes (klavogram, graph, histograms, legend, text styles). The light set is the
// original's.
struct Colors
{
    QColor pane;     // background of the graph, the klavogram and the histograms
    QColor paneMark; // the part of the graph seen on the klavogram, the measured span of the klavogram
    QColor axis;     // the axis band of the graph and the histograms
    QColor grid;     // dotted grid lines
    QColor ink;      // lines and labels on the panes
    QColor keyEdge;  // the edges of the keys on the klavogram and of the histogram bars
    QColor trackLine; // the track lines of the klavogram, its spaces and fragment starts
    QColor dimInk;   // secondary labels
    QColor rulerBox; // the ruler box of the graph
    QColor popup;    // the measurement box of the klavogram
    QColor legend;   // background of the legend
    QColor bar;      // histogram bars
    QColor erased;   // erased characters (text, text strip, klavogram frames)
    QColor injected; // injected keys and fragment separators
    QColor comment;  // comments in the text
    QColor visible;  // the text seen on the klavogram (background)
    QColor damaged, damagedText; // the "file is damaged" bar
    QColor speedBack, errorsBack; // live statistics
    QColor series[8];
    QColor held[5][2]; // klavogram keys by the number held at once: [level][one / several]
    QColor proofOk, proofPartial, proofBad; // the time stamps of the recording (the button of the toolbar)
};
const Colors &colors();

// A font of the original in points, as the pixels it has at 96 dpi: the same size on every system
// (macOS counts 72 logical dpi, so points there come out a quarter smaller).
int pointsToPixels(int points);
QFont pointFont(const QString &family, int points);

}
