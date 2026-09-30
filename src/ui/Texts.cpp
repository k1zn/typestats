#include "Texts.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>

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

Histograms::Names histogramNames()
{
    Histograms::Names n;
    n.allKeys = tr("Все клавиши");
    n.key = tr("Клавиша");
    n.pairs = tr("Длительности сочетаний");
    n.allFingers = tr("Все пальцы");
    n.relations = {tr("Клавиша"), tr("Палец"), tr("Рука"), tr("Прочее")};
    n.relationTitles = {tr("Двойное нажатие на клавишу"), tr("Разные клавиши"), tr("Та же рука (другой палец)"),
                        tr("Другая рука")};
    n.fingersShort = {tr("ЛМ"), tr("ЛБ"), tr("ЛС"), tr("ЛУ"), tr("ПУ"), tr("ПС"), tr("ПБ"), tr("ПМ"), tr("Прочие")};
    n.fingers = {tr("Левый мизинец"),       tr("Левый безымянный"), tr("Левый средний"),
                 tr("Левый указательный"),  tr("Правый указательный"), tr("Правый средний"),
                 tr("Правый безымянный"),   tr("Правый мизинец"),   tr("Остальные клавиши")};
    n.extra = tr("Дополнительная статистика");
    return n;
}

QStringList languages()
{
    return {QStringLiteral("Russian"), QStringLiteral("English")};
}

QString currentLanguage()
{
    const QString stored = QSettings().value(QStringLiteral("Language")).toString();
    if (languages().contains(stored))
        return stored;
    return QLocale::system().language() == QLocale::Russian ? QStringLiteral("Russian") : QStringLiteral("English");
}

} // namespace Texts
