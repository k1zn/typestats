#pragma once

#include <QStringList>

// Presets of settings (ComboBox1 of the main window), see re/settings.md. The working settings live at
// the root of QSettings under the original's key names; a preset is a stored copy of them.
namespace Presets {
QStringList names();
// The preset the working settings belong to ("Profile"); empty: none.
QString current();
void setCurrent(const QString &name);
// The working settings become the preset / the preset becomes the working settings.
void store(const QString &name);
bool load(const QString &name);
void remove(const QString &name);

// Takes over the settings and presets of the original from its registry key, once. Windows only.
bool importFromOriginal();
}
