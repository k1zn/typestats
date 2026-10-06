#include "SettingsDialog.h"
#include "Hotkeys.h"

#include "Texts.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSystemTrayIcon>

namespace {

QCheckBox *checkBox(QWidget *parent, const QString &text, int x, int y, const char *key, bool byDefault)
{
    auto *box = new QCheckBox(text, parent);
    box->move(x, y);
    box->setChecked(QSettings().value(QLatin1String(key), byDefault).toBool());
    return box;
}

QSpinBox *spinBox(QWidget *parent, int x, int y, int min, int max, const char *key, int byDefault)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(min, max);
    spin->setAlignment(Qt::AlignCenter);
    spin->setGeometry(x, y, 41, 17);
    spin->setValue(QSettings().value(QLatin1String(key), byDefault).toInt());
    return spin;
}

QLineEdit *speedEdit(QWidget *parent, int y, const char *key, int byDefault)
{
    auto *edit = new QLineEdit(QSettings().value(QLatin1String(key), byDefault).toString(), parent);
    edit->setValidator(new QIntValidator(0, 99999, edit));
    edit->setGeometry(108, y, 45, 21);
    return edit;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Настройки"));
    setFixedSize(589, 333); // the original's 317, and a row for the time stamps

    auto *fonts = new QGroupBox(tr("Размер шрифта"), this);
    fonts->setGeometry(0, 0, 129, 65);
    (new QLabel(tr("Текст"), fonts))->move(8, 17);
    (new QLabel(tr("Клавограмма"), fonts))->move(8, 41);
    m_textFont = spinBox(fonts, 80, 16, 8, 24, "TextFontSize", 12);
    m_klavFont = spinBox(fonts, 80, 40, 8, 24, "KlavogrFontSize", 9);

    auto *live = new QGroupBox(tr("Оперативная статистика"), this);
    live->setGeometry(136, 0, 161, 65);
    (new QLabel(tr("Нижняя скорость "), live))->move(8, 16);
    (new QLabel(tr("Верхняя скорость "), live))->move(8, 40);
    m_loSpeed = speedEdit(live, 14, "opLoSpeed", 200);
    m_hiSpeed = speedEdit(live, 38, "opHiSpeed", 500);

    auto *hotkeys = new QGroupBox(tr("Глобальные горячие клавиши"), this);
    hotkeys->setGeometry(0, 64, 297, 57);
    m_globalClear = checkBox(hotkeys, tr("%1 (Очистить)").arg(Hotkeys::clear()), 8, 16, "GlobalClear", true);
    m_globalOnOff = checkBox(hotkeys, tr("%1 (Управление перехватом)").arg(Hotkeys::onOff()), 8, 32, "GlobalOnOff", true);

    auto *tags = new QGroupBox(tr("Копирование текста с тегами (для вставки в блог)"), this);
    tags->setGeometry(0, 120, 297, 73);
    m_copyColor = checkBox(tags, tr("Выделять исправления цветом"), 8, 16, "CopyBlock1", true);
    m_copyStrike = checkBox(tags, tr("Выделять исправления зачёркиванием"), 8, 32, "CopyBlock2", true);
    m_copyNext = checkBox(tags, tr("Выделять цветом следующий символ"), 8, 48, "CopyBlock3", true);

    m_askSave = checkBox(this, tr("Спрашивать о сохранении при выходе"), 8, 200, "AskSaveOnExit", true);
    m_tray = checkBox(this, tr("Сворачивать в трей"), 8, 216, "MinimizeToTray", false);
    if (!QSystemTrayIcon::isSystemTrayAvailable()) { // GNOME without the AppIndicator extension
        m_tray->setEnabled(false);
        m_tray->setToolTip(tr("Системный трей недоступен"));
    }
    m_journal = checkBox(this, tr("Вести журнал"), 8, 232, "JournalOn", false);
    m_autoComments = checkBox(this, tr("Автокомментарии"), 8, 248, "AutoComments", false);
    m_autoMinimize = checkBox(this, tr("Сворачивать при старте"), 8, 264, "AutoMinimize", false);
    // The port's own (re/stamps.md): only hashes go to the authorities.
    m_stamp = checkBox(this, tr("Заверять запись метками времени (нужен интернет)"), 8, 280, "StampRecording", false);
    m_stamp->setToolTip(tr("Метки ставят службы времени DigiCert, Sectigo, GlobalSign, Certum, SwissSign и "
                           "Microsoft: они подтверждают, что записи были набраны в это время и не менялись позже. "
                           "Наружу уходят только хэши, без вашего набранного текста."));

    (new QLabel(tr("Основная статистика"), this))->move(304, 0);
    m_mainStats = new QListWidget(this);
    m_mainStats->setGeometry(304, 16, 281, 225);
    const QSettings s;
    const QStringList names = Texts::statsRowNames();
    for (int i = 0; i < names.size(); ++i) {
        auto *item = new QListWidgetItem(names[i], m_mainStats);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(s.value(QStringLiteral("MainOption%1").arg(i), true).toBool() ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint(QSize(0, 13)); // ItemHeight: all the rows fit without scrolling
    }

    (new QLabel(tr("Точность времени в списке длительностей"), this))->move(304, 249);
    m_digits = spinBox(this, 528, 248, 0, 3, "DlitDigits", 3);

    (new QLabel(tr("Язык"), this))->move(304, 264);
    m_language = new QComboBox(this);
    m_language->setGeometry(304, 280, 281, 21);
    m_language->addItems(Texts::languages());
    m_openedLanguage = Texts::currentLanguage();
    m_language->setCurrentText(m_openedLanguage);

    auto *ok = new QPushButton(tr("Ok"), this);
    ok->setGeometry(8, 304, 81, 25);
    ok->setDefault(true);
    auto *cancel = new QPushButton(tr("Отмена"), this);
    cancel->setGeometry(96, 304, 83, 25);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

bool SettingsDialog::languageChanged() const
{
    return m_language->currentText() != m_openedLanguage;
}

void SettingsDialog::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("GlobalClear"), m_globalClear->isChecked());
    s.setValue(QStringLiteral("GlobalOnOff"), m_globalOnOff->isChecked());
    s.setValue(QStringLiteral("TextFontSize"), m_textFont->value());
    s.setValue(QStringLiteral("KlavogrFontSize"), m_klavFont->value());
    s.setValue(QStringLiteral("DlitDigits"), m_digits->value());
    // StrToIntDef: text that is not a number gives the default.
    bool ok = false;
    const int lo = m_loSpeed->text().toInt(&ok);
    s.setValue(QStringLiteral("opLoSpeed"), ok ? lo : 200);
    const int hi = m_hiSpeed->text().toInt(&ok);
    s.setValue(QStringLiteral("opHiSpeed"), ok ? hi : 600);
    for (int i = 0; i < m_mainStats->count(); ++i)
        s.setValue(QStringLiteral("MainOption%1").arg(i), m_mainStats->item(i)->checkState() == Qt::Checked);
    s.setValue(QStringLiteral("CopyBlock1"), m_copyColor->isChecked());
    s.setValue(QStringLiteral("CopyBlock2"), m_copyStrike->isChecked());
    s.setValue(QStringLiteral("CopyBlock3"), m_copyNext->isChecked());
    s.setValue(QStringLiteral("MinimizeToTray"), m_tray->isChecked());
    s.setValue(QStringLiteral("JournalOn"), m_journal->isChecked());
    s.setValue(QStringLiteral("AutoComments"), m_autoComments->isChecked());
    s.setValue(QStringLiteral("AutoMinimize"), m_autoMinimize->isChecked());
    s.setValue(QStringLiteral("AskSaveOnExit"), m_askSave->isChecked());
    s.setValue(QStringLiteral("StampRecording"), m_stamp->isChecked());
    if (languageChanged())
        s.setValue(QStringLiteral("Language"), m_language->currentText());
}
