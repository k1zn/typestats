#include "Retranslate.h"

#include <QEvent>
#include <QList>
#include <QWidget>

namespace {

const char kName[] = "ts-retranslate";

class Retranslator : public QObject
{
public:
    explicit Retranslator(QWidget *owner) : QObject(owner)
    {
        setObjectName(QLatin1String(kName));
        owner->installEventFilter(this);
    }
    QList<std::function<void()>> sets;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == parent() && e->type() == QEvent::LanguageChange)
            for (const std::function<void()> &set : std::as_const(sets))
                set();
        return false;
    }
};

} // namespace

void translated(QWidget *owner, std::function<void()> set)
{
    set();
    auto *r = static_cast<Retranslator *>(owner->findChild<QObject *>(QLatin1String(kName), Qt::FindDirectChildrenOnly));
    if (!r)
        r = new Retranslator(owner);
    r->sets.append(std::move(set));
}
