#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace AppPaths {

namespace {

QString &overridden()
{
    static QString dir;
    return dir;
}

QString defaultDir()
{
#ifdef Q_OS_WIN
    const QString programDir = QCoreApplication::applicationDirPath();
    QTemporaryFile probe(QDir(programDir).filePath(QStringLiteral("ts-probe-XXXXXX")));
    if (probe.open())
        return programDir;
#endif
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

} // namespace

QString dataDir()
{
    if (!overridden().isEmpty())
        return overridden();
    static const QString dir = defaultDir();
    return dir;
}

QString file(const QString &name)
{
    const QString path = QDir(dataDir()).filePath(name);
    const QFileInfo old(QDir(QCoreApplication::applicationDirPath()).filePath(name));
    if (!QFileInfo::exists(path) && old.isFile() && old != QFileInfo(path) && QFile::copy(old.filePath(), path))
        QFile::setPermissions(path, QFile::permissions(path) | QFile::WriteOwner);
    return path;
}

void setDataDir(const QString &dir)
{
    overridden() = dir;
}

}
