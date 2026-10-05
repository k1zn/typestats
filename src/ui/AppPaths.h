#pragma once

#include <QString>

// Where the program keeps its own files: FingerZones.ini, ExStats.ini, the journals (.tsj).
// On Windows it is the folder of the program, as the original has it (a portable copy that the
// original can share), when that folder is writable; otherwise, and on other systems (an .app bundle,
// /usr/bin, an AppImage are read-only), the application data folder of the user.
namespace AppPaths {

QString dataDir();
// A file in dataDir(). The first time one is asked for, a file of that name next to the program
// (an older copy) is copied there.
QString file(const QString &name);
// Replaces dataDir() (tests); empty restores it.
void setDataDir(const QString &dir);

}
