#pragma once

#include "IniFile.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <array>

struct TextModel;

// Assignment of keys (scan codes) to fingers, see re/finger_zones.md.
// Fingers 0..7: left little .. left index, right index .. right little; kNone = "other keys".
class FingerZones
{
public:
    static constexpr int kFingers = 8;
    static constexpr quint8 kNone = 8;

    FingerZones() { m_table.fill(kNone); }

    // The built-in scheme "Стандарт" (0x44a1fc); read-only.
    static FingerZones standard();
    // Finger0..Finger7 strings as in .tsf and FingerZones.ini (0x449fb4).
    static FingerZones fromStrings(const QStringList &fingers);

    // Hex string of one finger, the home key first (0x44b028).
    QString toString(int finger) const;
    QStringList toStrings() const;

    quint8 finger(quint32 scanOrFlags) const { return m_table[scanOrFlags & 0x7f]; }
    static bool isLeftHand(int finger) { return finger < 4; }
    // The finger's home-row key (0x44b1bc).
    bool isHome(int scan) const;
    const QVector<quint8> &keys(int finger) const { return m_keys[finger]; }

    // Tkbd editor (0x44a298): gives the key to finger (kNone = unassign); home puts it first.
    void assign(int scan, int finger, bool home);

    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool r) { m_readOnly = r; }

    // Same table and the same home key of every finger (0x44b2b0).
    bool operator==(const FingerZones &o) const;

private:
    void add(int finger, int scan);

    std::array<QVector<quint8>, kFingers> m_keys;
    std::array<quint8, 128> m_table;
    bool m_readOnly = false;
};

// Finger of every text element (DAT_005b1404): zones.finger(flags of the element's record).
QVector<quint8> fingerSeries(const TextModel &m, const FingerZones &zones);

// Named schemes: the built-in one plus the sections of FingerZones.ini.
class FingerZoneSchemes
{
public:
    explicit FingerZoneSchemes(const QString &iniPath = {}, const QString &standardName = QStringLiteral("Стандарт"));

    // "Стандарт" first, then the INI sections in file order (ComboBox4).
    QStringList names() const;
    const QString &standardName() const { return m_standardName; }
    bool contains(const QString &name) const;
    // 0x403160: the named scheme; unknown names give an empty scheme, as in the original.
    FingerZones zones(const QString &name) const;
    // 0x402ff0: stores a scheme (the built-in one is never stored). Saves the INI if it has a path.
    void store(const QString &name, const FingerZones &zones);
    void remove(const QString &name);

    // LoadTsf: the first scheme equal to z, or a new one named after the file ("_" appended while
    // the name is taken), stored. Returns the scheme name.
    QString adopt(const QString &nameInFile, const FingerZones &z);

private:
    QString m_path;
    QString m_standardName;
    IniFile m_ini;
};
