#pragma once

class QWidget;

// The red mark of recording ("Ts: ON") on the program's button: Windows - an overlay icon in the corner of the task
// bar's button (ITaskbarList3, set again when Explorer makes the button anew); macOS - a badge on the Dock's icon;
// elsewhere - the window's icon with the mark (the task bars of X11 show it; Wayland takes the icon of the .desktop
// file and shows none).
namespace TaskbarMarker {

void set(QWidget *window, bool on);
bool isSet(const QWidget *window); // what was asked last (tests)

} // namespace TaskbarMarker
