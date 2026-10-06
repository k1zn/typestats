#include "FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#elif defined(Q_OS_MACOS)
#include <CoreServices/CoreServices.h>
#endif

namespace FileAssociation {

namespace {

[[maybe_unused]] bool samePath(const QString &a, const QString &b)
{
    const Qt::CaseSensitivity cs =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), cs) == 0;
}

QStringList envList(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return (v.isEmpty() ? fallback : v).split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

} // namespace

Places defaultPlaces()
{
    Places p;
    p.classes = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes");
    p.userChoice = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.tsf\\UserChoice");
    const QString home = QDir::homePath();
    p.dataHome = qEnvironmentVariable("XDG_DATA_HOME", home + QStringLiteral("/.local/share"));
    p.configHome = qEnvironmentVariable("XDG_CONFIG_HOME", home + QStringLiteral("/.config"));
    p.dataDirs = envList("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    p.configDirs = envList("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg"));
    p.desktops = envList("XDG_CURRENT_DESKTOP", QString());
    return p;
}

QString programPath()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

QString mimeDefinition()
{
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
        "  <mime-type type=\"application/x-typing-statistics\">\n"
        "    <comment>Typing statistics file</comment>\n"
        "    <comment xml:lang=\"ru\">Запись Typing statistics</comment>\n"
        "    <sub-class-of type=\"text/plain\"/>\n"
        "    <glob pattern=\"*.tsf\"/>\n"
        "  </mime-type>\n"
        "</mime-info>\n");
}

#if defined(Q_OS_WIN)

namespace {

const QString kProgId = QStringLiteral("TypingStatistics.tsf");

// The program of a command `"path" "%1"` (or `path "%1"`).
QString programOf(const QString &command)
{
    if (command.startsWith(QLatin1Char('"'))) {
        const qsizetype end = command.indexOf(QLatin1Char('"'), 1);
        return end > 0 ? command.mid(1, end - 1) : QString();
    }
    return command.section(QLatin1Char(' '), 0, 0);
}

} // namespace

State tsfState(const QString &program, const Places &places)
{
    // The choice made in "Open with" wins over the classes, and Windows protects it from programs.
    const QString chosen = QSettings(places.userChoice, QSettings::NativeFormat).value(QStringLiteral("ProgId")).toString();
    const bool chosenOurs = chosen == kProgId
                            || chosen.compare(QStringLiteral("Applications\\") + QFileInfo(program).fileName(), Qt::CaseInsensitive) == 0;
    if (!chosen.isEmpty() && !chosenOurs)
        return State::Overridden;

    QSettings classes(places.classes, QSettings::NativeFormat);
    const QString progId = classes.value(QStringLiteral(".tsf/.")).toString();
    if (progId.isEmpty())
        return chosenOurs ? State::Ours : State::None;
    if (progId != kProgId)
        return chosenOurs ? State::Ours : State::Other;
    const QString registered = programOf(classes.value(kProgId + QStringLiteral("/shell/open/command/.")).toString());
    if (samePath(registered, program))
        return State::Ours;
    return QFileInfo::exists(registered) ? State::Other : State::Moved;
}

bool associateTsf(const QString &program, const Places &places)
{
    const QString exe = QDir::toNativeSeparators(program);
    QSettings classes(places.classes, QSettings::NativeFormat);
    classes.setValue(QStringLiteral(".tsf/."), kProgId);
    classes.setValue(QStringLiteral(".tsf/OpenWithProgids/") + kProgId, QString());
    classes.setValue(kProgId + QStringLiteral("/."), QStringLiteral("Typing statistics file")); // as the original
    classes.setValue(kProgId + QStringLiteral("/DefaultIcon/."), QLatin1Char('"') + exe + QStringLiteral("\",0"));
    classes.setValue(kProgId + QStringLiteral("/shell/open/command/."), QLatin1Char('"') + exe + QStringLiteral("\" \"%1\""));
    classes.sync();
    if (classes.status() != QSettings::NoError)
        return false;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // Explorer takes the change and the icon
    return true;
}

#elif defined(Q_OS_MACOS)

// Launch Services: the role handler API is deprecated since macOS 12 but works; its replacement (NSWorkspace)
// is asynchronous Objective-C for the same thing.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

const CFStringRef kType = CFSTR("org.typingstatistics.tsf");

QString bundleId()
{
    CFStringRef id = CFBundleGetIdentifier(CFBundleGetMainBundle());
    return id ? QString::fromCFString(id) : QString();
}

} // namespace

State tsfState(const QString &, const Places &)
{
    const QString ours = bundleId();
    if (ours.isEmpty())
        return State::Unsupported; // not an .app: it cannot be a handler
    CFStringRef handler = LSCopyDefaultRoleHandlerForContentType(kType, kLSRolesAll);
    if (!handler)
        return State::None;
    const QString id = QString::fromCFString(handler);
    CFRelease(handler);
    return id.compare(ours, Qt::CaseInsensitive) == 0 ? State::Ours : State::Other;
}

bool associateTsf(const QString &, const Places &)
{
    const QString ours = bundleId();
    if (ours.isEmpty())
        return false;
    // The bundle's Info.plist (its type) is known to Launch Services once the program has run from it; say it again.
    if (CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle())) {
        LSRegisterURL(url, true);
        CFRelease(url);
    }
    CFStringRef id = ours.toCFString();
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(kType, kLSRolesAll, id);
    CFRelease(id);
    return status == noErr;
}

#pragma clang diagnostic pop

#else // Linux and other XDG systems

namespace {

const QString kMime = QStringLiteral("application/x-typing-statistics");
const QString kDesktopId = QStringLiteral("org.typingstatistics.TypingStatistics.desktop");
const QString kIconName = QStringLiteral("org.typingstatistics.TypingStatistics");

// The value of `key` in `group` of an INI-like XDG file (.desktop, mimeapps.list), or a null string.
QString xdgValue(const QString &path, const QString &group, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    bool in = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('[')))
            in = line == QLatin1Char('[') + group + QLatin1Char(']');
        else if (in && line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1);
    }
    return {};
}

// Sets `key` in `group` of an XDG file, keeping the rest of it.
bool setXdgValue(const QString &path, const QString &group, const QString &key, const QString &value)
{
    QStringList lines;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    in.close();
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    const QString header = QLatin1Char('[') + group + QLatin1Char(']'), entry = key + QLatin1Char('=') + value;
    qsizetype start = -1;
    for (qsizetype i = 0; i < lines.size(); ++i)
        if (lines[i].trimmed() == header) {
            start = i;
            break;
        }
    if (start < 0) {
        if (!lines.isEmpty())
            lines << QString();
        lines << header << entry;
    } else {
        qsizetype end = start + 1, at = -1;
        for (; end < lines.size() && !lines[end].trimmed().startsWith(QLatin1Char('[')); ++end)
            if (lines[end].trimmed().startsWith(key + QLatin1Char('=')))
                at = end;
        if (at >= 0) {
            lines[at] = entry;
        } else {
            qsizetype last = end; // after the group's last line that is not empty
            while (last > start + 1 && lines[last - 1].trimmed().isEmpty())
                --last;
            lines.insert(last, entry);
        }
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    return out.commit();
}

// mimeapps.list in the order of the XDG MIME applications specification.
QStringList mimeappsLists(const Places &p)
{
    QStringList dirs{p.configHome};
    dirs << p.configDirs;
    QStringList files;
    for (const QString &dir : dirs) {
        for (const QString &desktop : p.desktops)
            files << dir + QLatin1Char('/') + desktop.toLower() + QStringLiteral("-mimeapps.list");
        files << dir + QStringLiteral("/mimeapps.list");
    }
    files << p.dataHome + QStringLiteral("/applications/mimeapps.list");
    for (const QString &dir : p.dataDirs)
        files << dir + QStringLiteral("/applications/mimeapps.list");
    return files;
}

// The program's desktop file that the system takes: the user's first, then the system's.
QString desktopFile(const Places &p)
{
    QStringList dirs{p.dataHome};
    dirs << p.dataDirs;
    for (const QString &dir : dirs) {
        const QString path = dir + QStringLiteral("/applications/") + kDesktopId;
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

// The program of an Exec line: its first argument, unquoted, found in PATH when it has no directory.
QString execProgram(const QString &exec)
{
    QString program;
    if (exec.startsWith(QLatin1Char('"'))) {
        for (qsizetype i = 1; i < exec.size() && exec[i] != QLatin1Char('"'); ++i) {
            if (exec[i] == QLatin1Char('\\') && i + 1 < exec.size())
                ++i;
            program += exec[i];
        }
    } else {
        program = exec.section(QLatin1Char(' '), 0, 0);
    }
    if (!program.isEmpty() && !program.contains(QLatin1Char('/')))
        program = QStandardPaths::findExecutable(program);
    return program;
}

// An Exec line for `program` with a file argument (the quoting of the Desktop Entry specification).
QString quotedExec(const QString &program)
{
    QString q;
    for (QChar c : program) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
            q += QLatin1Char('\\');
        q += c;
    }
    return QLatin1Char('"') + q + QStringLiteral("\" %f");
}

bool mimeKnown(const Places &p)
{
    QStringList dirs{p.dataHome};
    dirs << p.dataDirs;
    for (const QString &dir : dirs)
        if (QFileInfo::exists(dir + QStringLiteral("/mime/packages/") + kIconName + QStringLiteral(".xml")))
            return true;
    return false;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

QString registeredProgram(const Places &p)
{
    const QString desktop = desktopFile(p);
    return desktop.isEmpty() ? QString()
                             : execProgram(xdgValue(desktop, QStringLiteral("Desktop Entry"), QStringLiteral("Exec")));
}

bool sameProgram(const QString &a, const QString &b)
{
    return samePath(QFileInfo(a).canonicalFilePath(), QFileInfo(b).canonicalFilePath());
}

} // namespace

State tsfState(const QString &program, const Places &places)
{
    QString handler;
    for (const QString &list : mimeappsLists(places)) {
        handler = xdgValue(list, QStringLiteral("Default Applications"), kMime).section(QLatin1Char(';'), 0, 0);
        if (!handler.isEmpty())
            break;
    }
    if (handler.isEmpty())
        return State::None;
    if (handler != kDesktopId)
        return State::Other;
    const QString registered = registeredProgram(places);
    if (registered.isEmpty() || !QFileInfo::exists(registered) || !mimeKnown(places))
        return State::Moved; // ours, but something is gone: repaired without asking
    return sameProgram(registered, program) ? State::Ours : State::Other;
}

bool associateTsf(const QString &program, const Places &places)
{
    // The type: installed with the package, or the user's own.
    if (!mimeKnown(places)) {
        const QString mimeDir = places.dataHome + QStringLiteral("/mime");
        if (!writeFile(mimeDir + QStringLiteral("/packages/") + kIconName + QStringLiteral(".xml"), mimeDefinition().toUtf8()))
            return false;
        if (places.runTools)
            QProcess::execute(QStringLiteral("update-mime-database"), {mimeDir});
    }
    // The program: the system's desktop file when it starts this program and knows the type, otherwise the
    // user's own (an AppImage, another copy).
    const QString desktop = desktopFile(places);
    if (!sameProgram(registeredProgram(places), program)
        || xdgValue(desktop, QStringLiteral("Desktop Entry"), QStringLiteral("MimeType")).isEmpty()) {
        const QString applications = places.dataHome + QStringLiteral("/applications");
        const QString entry = QStringLiteral("[Desktop Entry]\n"
                                             "Type=Application\n"
                                             "Name=Typing statistics\n"
                                             "GenericName=Typing analyzer\n"
                                             "GenericName[ru]=Анализатор набора\n"
                                             "Exec=%1\n"
                                             "Icon=%2\n"
                                             "Terminal=false\n"
                                             "Categories=Utility;Education;\n"
                                             "MimeType=%3;\n")
                                  .arg(quotedExec(program), kIconName, kMime);
        if (!writeFile(applications + QLatin1Char('/') + kDesktopId, entry.toUtf8()))
            return false;
        // An AppImage carries its icon: the user's icon theme gets a copy.
        const QString appDir = qEnvironmentVariable("APPDIR");
        const QString icon = QStringLiteral("/icons/hicolor/256x256/apps/") + kIconName + QStringLiteral(".png");
        if (!appDir.isEmpty() && QFileInfo::exists(appDir + QStringLiteral("/usr/share") + icon)
            && !QFileInfo::exists(places.dataHome + icon)) {
            QDir().mkpath(QFileInfo(places.dataHome + icon).absolutePath());
            QFile::copy(appDir + QStringLiteral("/usr/share") + icon, places.dataHome + icon);
        }
        if (places.runTools)
            QProcess::execute(QStringLiteral("update-desktop-database"), {applications});
    }
    const QString list = places.configHome + QStringLiteral("/mimeapps.list");
    return setXdgValue(list, QStringLiteral("Default Applications"), kMime, kDesktopId)
           && setXdgValue(list, QStringLiteral("Added Associations"), kMime, kDesktopId + QLatin1Char(';'));
}

#endif

} // namespace FileAssociation
