#include "AboutDialog.h"

#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QUrl>

#include <functional>

namespace {

QLabel *label(QWidget *parent, const QString &text, int x, int y, const QString &family, int pixels, const QColor &color)
{
    auto *l = new QLabel(text, parent);
    QFont f(family);
    f.setPixelSize(pixels);
    l->setFont(f);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, color);
    l->setPalette(p);
    l->move(x, y);
    return l;
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("О программе"));
    setFixedSize(508, 252);
    const QString sans = font().family(), serif = QStringLiteral("Times New Roman");
    const QColor brown(170, 96, 48);

    auto *banner = new QFrame(this);
    banner->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    banner->setAutoFillBackground(true);
    QPalette bannerPalette = banner->palette();
    bannerPalette.setColor(QPalette::Window, QColor(255, 208, 160));
    banner->setPalette(bannerPalette);
    banner->setGeometry(0, 0, 508, 121);
    label(banner, QStringLiteral("T"), 32, -16, serif, 120, Qt::red);
    label(banner, QStringLiteral("S"), 104, 35, serif, 80, QColor(255, 128, 64));
    label(banner, tr("Анализатор статистики"), 176, 32, serif, 21, brown);
    label(banner, tr("клавиатурного набора"), 256, 56, serif, 20, brown);
    QLabel *first = label(banner, tr("Первая версия вышла 27 ноября 2008 года"), 208, 3, sans, 11, brown);
    first->setAlignment(Qt::AlignRight);
    first->setGeometry(208, 3, 297, 13);
    QLabel *author = label(banner, tr("Игорь В. Филимонов"), 296, 104, sans, 11, brown);
    author->setAlignment(Qt::AlignRight);
    author->setGeometry(296, 104, 204, 13);

    struct Line
    {
        QString text, link;
        std::function<void()> action;
    };
    auto open = [](const char *url) { return [url] { QDesktopServices::openUrl(QUrl(QLatin1String(url))); }; };
    auto say = [this](const QString &text, const QString &title) {
        return [this, text, title] { QMessageBox::information(this, title, text); };
    };
    const Line lines[] = {
        {tr("Заходите к нам на"), tr("сайт"), open("http://fil.urikor.net")},
        {tr("Заглядывайте на"), tr("форум"), open("http://urikor.net/phpBB2/viewforum.php?f=32")},
        {tr("Пишите"), tr("письма"), open("mailto:Fil95@yandex.ru")},
        {tr("Набирайте"), tr("вслепую"),
         say(tr("Надеюсь, Typing statistics Вам в этом поможет"), tr("Набирайте вслепую"))},
        {tr("Будьте"), tr("счастливы"), say(tr("Счастье складывается из мелочей..."), tr("Будьте счастливы!"))},
    };
    int y = 128;
    for (const Line &line : lines) {
        QLabel *left = label(this, line.text, 8, y, sans, 13, palette().color(QPalette::WindowText));
        left->setAlignment(Qt::AlignRight);
        left->setGeometry(8, y, 130, 21);
        QLabel *link = label(this, QStringLiteral("<a href=\"#\">%1</a>").arg(line.link), 144, y, sans, 13, Qt::blue);
        link->setCursor(Qt::PointingHandCursor);
        connect(link, &QLabel::linkActivated, this, line.action);
        y += 24;
    }

    auto *thanks = new QLabel(
        tr("Эта программа была придумана и реализована в результате дебатов на форуме urikor.net. Так что всем "
           "участвовавшим (и участвующим по сию пору) форумчанам - Спасибо! Без вас этой программы не было бы.\n"
           "Отдельное спасибо - Юрикору (за отличный сайт и предоставленный хостинг), Автандилине, Dron'у, Nestor'у, "
           "Валерию Марусяку за поддержку и советы."),
        this);
    thanks->setWordWrap(true);
    thanks->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    thanks->setFrameStyle(int(QFrame::StyledPanel) | int(QFrame::Sunken));
    thanks->setGeometry(224, 128, 281, 121);
}
