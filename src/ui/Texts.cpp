#include "Texts.h"

#include <QCoreApplication>

namespace Texts {

static QString tr(const char *text)
{
    return QCoreApplication::translate("Texts", text);
}

StatsUnits units()
{
    StatsUnits u;
    u.ms = tr("мс");
    u.h = tr("ч");
    u.m = tr("м");
    u.s = tr("с");
    return u;
}

QStringList statsRowNames()
{
    return {tr("Символов"),          tr("Общее время"),       tr("min пауза"),
            tr("max пауза"),         tr("Средняя пауза"),     tr("Среднее время удержания"),
            tr("Скорость spm"),      tr("Скорость wpm"),      tr("Скорость нетто"),
            tr("Скорость брутто"),   tr("Скорость брутто+"),  tr("Скорость брутто*"),
            tr("Потери от исправлений"), tr("Аритмия"),       tr("Исправлений"),
            tr("Серий исправлений"), tr("max без исправлений")};
}

} // namespace Texts
