#pragma once

#include <QString>
#include <QStringList>

// Opening .tsf files in the program by a double click. The original registered .tsf by itself, without asking
// (its strings at 0x58f6be...: HKCU\Software\Classes\.tsf -> tsFile, "Typing statistics file", Shell\Открыть\command
// `"exe" "%1"`, DefaultIcon); the remake asks at the first start (MainWindow::offerFileAssociation). Everything is per user, without an
// administrator:
// - Windows: HKCU\Software\Classes (.tsf -> TypingStatistics.tsf, its command and icon);
// - Linux: the MIME type application/x-typing-statistics (*.tsf) and the program's .desktop file in the user's
//   data directory when the system has none for this program (an AppImage), the default in mimeapps.list;
// - macOS: the type org.typingstatistics.tsf is declared by Info.plist; Launch Services makes the program its
//   default handler.
namespace FileAssociation {

enum class State {
    Ours,        // .tsf opens in this program
    Moved,       // it was this program at a path that no longer exists (the exe or AppImage was moved)
    Other,       // another program (the original, another copy of the remake...)
    None,        // nothing
    Overridden,  // Windows: the user's choice (UserChoice) names another program, only the user can change it
    Unsupported, // the program is not where it can be registered (macOS: not an .app bundle)
};

// Where the association lives; tests pass their own.
struct Places
{
    // Windows: HKEY_CURRENT_USER\Software\Classes and the UserChoice key of .tsf.
    QString classes, userChoice;
    // Linux: the XDG base directories and the current desktops (XDG_CURRENT_DESKTOP).
    QString dataHome, configHome;
    QStringList dataDirs, configDirs, desktops;
    bool runTools = true; // update-mime-database, update-desktop-database
};
Places defaultPlaces();

// The program to open the files with: the exe (on Linux the AppImage, when it runs as one).
QString programPath();

State tsfState(const QString &program = programPath(), const Places &places = defaultPlaces());
// Makes .tsf open in `program`; false when it could not be written.
bool associateTsf(const QString &program = programPath(), const Places &places = defaultPlaces());

// Linux: the shared-mime-info definition of the type (also installed from resources/linux).
QString mimeDefinition();

} // namespace FileAssociation
