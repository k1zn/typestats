#include "Presets.h"

#include <QSettings>

namespace {

const QString kGroup = QStringLiteral("Presets");

// What is not part of a preset (FUN_00407140 writes these apart) and the port's own bookkeeping.
bool isGlobal(const QString &key)
{
    static const QStringList globals = {QStringLiteral("UserName"),   QStringLiteral("Profile"),
                                        QStringLiteral("Language"),   QStringLiteral("MainWinLeft"),
                                        QStringLiteral("MainWinTop"), QStringLiteral("WindowGeometry"),
                                        QStringLiteral("RegistryImported")};
    return globals.contains(key);
}

QString path(const QString &name, const QString &key)
{
    return kGroup + QLatin1Char('/') + name + QLatin1Char('/') + key;
}

} // namespace

namespace Presets {

QStringList names()
{
    QSettings s;
    s.beginGroup(kGroup);
    return s.childGroups();
}

QString current()
{
    return QSettings().value(QStringLiteral("Profile")).toString();
}

void setCurrent(const QString &name)
{
    QSettings().setValue(QStringLiteral("Profile"), name);
}

void store(const QString &name)
{
    if (name.isEmpty())
        return;
    QSettings s;
    s.remove(kGroup + QLatin1Char('/') + name);
    for (const QString &key : s.childKeys())
        if (!isGlobal(key))
            s.setValue(path(name, key), s.value(key));
}

bool load(const QString &name)
{
    QSettings s;
    s.beginGroup(kGroup + QLatin1Char('/') + name);
    const QStringList keys = s.childKeys();
    QVariantList values;
    for (const QString &key : keys)
        values << s.value(key);
    s.endGroup();
    for (int i = 0; i < keys.size(); ++i)
        if (!isGlobal(keys[i]))
            s.setValue(keys[i], values[i]);
    return !keys.isEmpty();
}

void remove(const QString &name)
{
    if (!name.isEmpty())
        QSettings().remove(kGroup + QLatin1Char('/') + name);
}

bool importFromOriginal()
{
#ifdef Q_OS_WIN
    QSettings s;
    if (s.value(QStringLiteral("RegistryImported")).toBool())
        return false;
    s.setValue(QStringLiteral("RegistryImported"), true);
    // The original: values of the unnamed preset and the global ones in the key itself, a subkey per preset.
    QSettings original(QStringLiteral("HKEY_CURRENT_USER\\Software\\TypingStatistics"), QSettings::NativeFormat);
    const QStringList keys = original.childKeys();
    if (keys.isEmpty())
        return false;
    for (const QString &key : keys)
        if (!s.contains(key))
            s.setValue(key, original.value(key));
    for (const QString &preset : original.childGroups()) {
        original.beginGroup(preset);
        for (const QString &key : original.childKeys())
            s.setValue(path(preset, key), original.value(key));
        original.endGroup();
    }
    return true;
#else
    return false;
#endif
}

} // namespace Presets
