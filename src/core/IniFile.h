#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Minimal INI file compatible with the original's TIniFile files (FingerZones.ini, ExStats.ini, *.lng):
// sections and keys are matched case-insensitively and keep their order, values are trimmed.
// Old files are ANSI (cp1251); a file that is valid UTF-8 is read as UTF-8. Written as UTF-8.
// QSettings is not used: it percent-encodes non-ASCII section names and does not read cp1251.
class IniFile
{
public:
    IniFile() = default;
    explicit IniFile(const QString &path) { load(path); }

    bool load(const QString &path);
    bool save(const QString &path) const;
    void parse(const QString &text);
    QString toString() const;

    QStringList sections() const;
    bool hasSection(const QString &section) const;
    QStringList keys(const QString &section) const;
    QString value(const QString &section, const QString &key, const QString &def = {}) const;
    void setValue(const QString &section, const QString &key, const QString &value);
    void removeSection(const QString &section);

    // Bytes of an INI/text file of the original: UTF-8 if valid (BOM skipped), else cp1251.
    static QString decode(const QByteArray &bytes);

private:
    struct Section
    {
        QString name;
        QList<QPair<QString, QString>> values;
    };
    const Section *find(const QString &name) const;
    QList<Section> m_sections;
};
