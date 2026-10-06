#include "AboutDialog.h"

#include <QCoreApplication>
#include <QFrame>
#include <QLabel>

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
    // The original's colours with red and blue swapped: the blue of the remake.
    const QColor navy(48, 96, 170);

    auto *banner = new QFrame(this);
    banner->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    banner->setAutoFillBackground(true);
    QPalette bannerPalette = banner->palette();
    bannerPalette.setColor(QPalette::Window, QColor(160, 208, 255));
    banner->setPalette(bannerPalette);
    banner->setGeometry(0, 0, 508, 121);
    label(banner, QStringLiteral("T"), 32, -16, serif, 120, Qt::blue);
    label(banner, QStringLiteral("S"), 104, 35, serif, 80, QColor(64, 128, 255));
    label(banner, tr("Анализатор статистики"), 176, 32, serif, 21, navy);
    label(banner, tr("клавиатурного набора"), 256, 56, serif, 20, navy);
    QLabel *first = label(banner, tr("Первая версия вышла 27 ноября 2008 года"), 208, 3, sans, 11, navy);
    first->setAlignment(Qt::AlignRight);
    first->setGeometry(208, 3, 297, 13);
    QLabel *author = label(banner, tr("Игорь В. Филимонов"), 296, 104, sans, 11, navy);
    author->setAlignment(Qt::AlignRight);
    author->setGeometry(296, 104, 204, 13);

    // In place of the original's links: whose program this is.
    auto *credits = new QLabel(
        tr("<b>Typing statistics v%1</b><br>ремейк на Qt 6 для Windows, Linux и macOS<br><br>"
           "Оригинал (2008–2016):<br><a href=\"https://klavogonki.ru/u/#/147900/\">Игорь В. Филимонов</a><br><br>"
           "Ремейк (2026), версия %2:<br><a href=\"https://klavogonki.ru/u/#/600585/\">Эрик (kiZzn)</a>")
            .arg(QCoreApplication::applicationVersion(), QStringLiteral(TS_REMAKE_VERSION)),
        this);
    credits->setWordWrap(true);
    credits->setOpenExternalLinks(true);
    credits->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    credits->setGeometry(8, 128, 208, 121);

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
