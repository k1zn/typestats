#include "MainWindow.h"

#include "AboutDialog.h"
#include "Presets.h"
#include "TextInputWindow.h"
#include "GraphPanels.h"
#include "GraphWidget.h"
#include "HistogramWindow.h"
#include "KlavogramWidget.h"
#include "ExtraStatsWindow.h"
#include "LiveStatsWindow.h"
#include "SettingsDialog.h"
#include "TextView.h"
#include "Texts.h"
#include "FilePropertiesDialog.h"
#include "FingerZonesDialog.h"
#include "core/Editing.h"
#include "core/Journal.h"
#include "core/KeyList.h"
#include "core/MainStats.h"
#include "core/NumberFormat.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QSystemTrayIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QToolButton>
#include <QToolTip>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

QString appTitle()
{
    return QStringLiteral("Typing statistics v") + QCoreApplication::applicationVersion();
}

// A report-style list with grid lines (TListView, vsReport + GridLines + RowSelect).
QTableWidget *reportList(const QStringList &headers, const QFont &font)
{
    auto *list = new QTableWidget(0, headers.size());
    list->setFont(font);
    list->setHorizontalHeaderLabels(headers);
    list->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    list->horizontalHeader()->setHighlightSections(false);
    list->verticalHeader()->hide();
    list->verticalHeader()->setMinimumSectionSize(1);
    list->verticalHeader()->setDefaultSectionSize(QFontMetrics(font).height() + 1);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setWordWrap(false);
    return list;
}

void setRows(QTableWidget *list, const QVector<QStringList> &rows)
{
    if (list->rowCount() != rows.size())
        list->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows[r].size(); ++c) {
            auto *item = new QTableWidgetItem(rows[r][c]);
            item->setToolTip(rows[r][c]);
            list->setItem(r, c, item);
        }
}

// Preset keys of the series' visibility, in the order of the legend.
const char *const kSeriesKeys[] = {"VgrCurSpeed", "VgrMedSpeed", "VgrClassicSpeed", "VgrPrivSpeed",
                                   "VgrCurRythm", "VgrMedRythm", "VhsPeriodMed",    "VhsPeriod"};

// The date and time as DateTimeToStr(Now) writes them: the system's short date and long time.
QString nowString()
{
    const QDateTime now = QDateTime::currentDateTime();
    return QLocale::system().toString(now.date(), QLocale::ShortFormat) + QLatin1Char(' ')
           + now.time().toString(QStringLiteral("H:mm:ss"));
}

QFrame *bevel(QWidget *parent, int x, int y, int w, int h, QFrame::Shape shape)
{
    auto *f = new QFrame(parent);
    f->setFrameStyle(int(shape) | int(QFrame::Sunken));
    f->setGeometry(x, y, w, h);
    return f;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_schemes(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("FingerZones.ini"))),
      m_journal(QCoreApplication::applicationDirPath())
{
    setWindowTitle(appTitle());
    setMinimumWidth(220);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(createToolBar());

    // Left: text, graph with its scroll bar, klavogram.
    m_text = new TextView;
    auto *graphPane = new QWidget;
    auto *graphLayout = new QVBoxLayout(graphPane);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    graphLayout->setSpacing(0);
    m_graph = new GraphWidget;
    m_graph->installEventFilter(this);
    m_graphScroll = new QScrollBar(Qt::Horizontal);
    graphLayout->addWidget(m_graph, 1);
    graphLayout->addWidget(m_graphScroll);
    m_klav = new KlavogramWidget;

    auto *left = new QSplitter(Qt::Vertical);
    left->setChildrenCollapsible(false);
    left->addWidget(m_text);
    left->addWidget(graphPane);
    left->addWidget(m_klav);
    left->setStretchFactor(0, 0);
    left->setStretchFactor(1, 1);
    left->setStretchFactor(2, 0);
    left->setSizes({120, 192, 200});
    left->setMinimumHeight(150);

    // Right: main statistics and the keys of the visible part of the klavogram.
    m_stats = reportList({tr("Параметр"), tr("Значение")}, font());
    m_stats->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stats->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stats->horizontalHeader()->resizeSection(1, 92);
    QFont mono(QStringLiteral("Courier New"));
    mono.setPixelSize(15);
    m_keys = reportList({tr("Пауза"), tr("Длительность"), tr("Клавиша")}, mono);
    m_keys->horizontalHeader()->resizeSection(0, 80);
    m_keys->horizontalHeader()->resizeSection(1, 80);
    m_keys->horizontalHeader()->setStretchLastSection(true);
    m_keys->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_stats);
    rightLayout->addWidget(m_keys, 1);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({652, 218});
    right->setMinimumWidth(100);
    m_leftSplit = left;
    m_mainSplit = split;
    root->addWidget(split, 1);

    m_damaged = new QLabel(tr("Внимание! Файл повреждён!"), this);
    m_damaged->setAlignment(Qt::AlignCenter);
    m_damaged->setAutoFillBackground(true);
    m_damaged->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    QFont warn = m_damaged->font();
    warn.setPixelSize(24);
    warn.setBold(true);
    m_damaged->setFont(warn);
    QPalette warnPalette = m_damaged->palette();
    warnPalette.setColor(QPalette::Window, QColor(192, 220, 192)); // clMoneyGreen
    warnPalette.setColor(QPalette::WindowText, Qt::red);
    m_damaged->setPalette(warnPalette);
    m_damaged->setGeometry(128, 224, 409, 49);
    m_damaged->hide();

    connect(m_text, &QTextEdit::selectionChanged, this, &MainWindow::selectionChanged);
    connect(m_text, &TextView::deleteRequested, this, &MainWindow::deleteSelection);
    connect(m_text, &TextView::markRequested, this, &MainWindow::mark);
    connect(m_text, &TextView::copyRequested, this, [this] { copy(0); });
    connect(m_text, &TextView::hovered, this, &MainWindow::textHovered);
    connect(m_text, &TextView::menuRequested, this, &MainWindow::showTextMenu);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, &MainWindow::undo);
    connect(m_klav, &KlavogramWidget::viewChanged, this, &MainWindow::klavogramMoved);
    createGraphPanels();

    m_live = new LiveStatsWindow(this);
    m_extra = new ExtraStatsWindow(this);
    connect(m_extra, &ExtraStatsWindow::shown, this, &MainWindow::updateExtraStats);
    connect(m_extra, &ExtraStatsWindow::elementSelected, this, &MainWindow::scrollKlavogramToElement);
    connect(m_extra, &ExtraStatsWindow::exportRequested, this, [this] { exportTable(extraTable(), false); });
    m_input = new TextInputWindow(this);
    connect(new QShortcut(QKeySequence(Qt::Key_F4), this), &QShortcut::activated, this, &MainWindow::showTextInput);
    m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
    auto *trayMenu = new QMenu(this);
    QAction *captureAction = trayMenu->addAction(tr("Вкл"));
    captureAction->setCheckable(true);
    captureAction->setChecked(true);
    connect(captureAction, &QAction::toggled, m_capture, &QCheckBox::setChecked);
    connect(m_capture, &QCheckBox::toggled, captureAction, &QAction::setChecked);
    trayMenu->addAction(tr("Оперативная статистика"), this, &MainWindow::showLiveStats);
    trayMenu->addAction(tr("Выход"), this, &QWidget::close);
    m_tray->setContextMenu(trayMenu);
    m_tray->setToolTip(QStringLiteral("Ts: ON"));
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            restoreFromTray();
    });
    m_hist = new HistogramWindow(this);
    connect(m_hist, &HistogramWindow::shown, this, &MainWindow::updateHistograms);
    connect(m_hist, &HistogramWindow::elementSelected, this, &MainWindow::scrollKlavogramToElement);
    connect(m_hist, &HistogramWindow::extraRowSelected, m_extra, &ExtraStatsWindow::selectRow);
    connect(m_hist, &HistogramWindow::extraRequested, this, &MainWindow::showExtraStats);
    connect(m_extra, &ExtraStatsWindow::rowsChanged, this, [this] { m_hist->setExtraRows(m_extra->rows()); });
    connect(&m_hook, &KeyboardHook::key, this, &MainWindow::keyEvent);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::tick);
    timer->start(100);

    resize(876, 579);
    loadSettings();
    applySettings();
}

QToolButton *MainWindow::toolButton(QWidget *panel, int n, int x, int y, int h, const QString &hint)
{
    auto *b = new QToolButton(panel);
    b->setIcon(QIcon(QStringLiteral(":/icons/Form1_SpeedButton%1.png").arg(n)));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(hint);
    b->setFocusPolicy(Qt::NoFocus);
    b->setGeometry(x, y, 23, h);
    b->setEnabled(false); // enabled by whoever connects it
    return b;
}

QWidget *MainWindow::createToolBar()
{
    auto *bar = new QFrame;
    bar->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    bar->setFixedHeight(57);

    auto action = [&](int n, int x, int y, int h, const QString &hint, void (MainWindow::*slot)(), bool enabled = false) {
        QToolButton *b = toolButton(bar, n, x, y, h, hint);
        if (slot) {
            b->setEnabled(true);
            connect(b, &QToolButton::clicked, this, slot);
        }
        if (enabled)
            b->setEnabled(true);
        return b;
    };
    // Upper row.
    action(1, 6, 4, 23, tr("Очистить (LCtrl+LWin)"), &MainWindow::clear);
    action(6, 30, 4, 23, tr("Прочитать"), &MainWindow::open);
    m_saveButton = action(5, 54, 4, 23, tr("Сохранить"), &MainWindow::save);
    m_blockButton = action(27, 78, 4, 23, tr("Сохранить блок"), nullptr);
    connect(m_blockButton, &QToolButton::clicked, this, [this] { saveDocument(true); });
    action(26, 102, 4, 23, tr("Открыть журнал"), &MainWindow::openJournal);
    connect(action(7, 126, 4, 23, tr("Экспортировать в Excel"), nullptr, true), &QToolButton::clicked, this,
            [this] { exportTable(keyTable(), true); });
    action(12, 150, 4, 23, tr("Дополнительная статистика"), &MainWindow::showExtraStats);
    action(19, 174, 4, 23, tr("Статистические гистограммы"), &MainWindow::showHistograms);
    action(20, 198, 4, 23, tr("Видео"), nullptr);
    action(22, 222, 4, 23, tr("Настройки..."), &MainWindow::showSettings);
    action(4, 246, 4, 23, tr("Оперативная статистика"), &MainWindow::showLiveStats);
    m_helpButton = action(3, 270, 4, 23, tr("Справка"), &MainWindow::showHelpMenu);
    bevel(bar, 3, 29, 294, 2, QFrame::HLine);
    // Lower row.
    m_deleteButton = action(10, 6, 32, 22, tr("Удалить (Del)"), &MainWindow::deleteSelection);
    m_deleteButton->setEnabled(false);
    connect(action(11, 30, 32, 22, tr("Копировать (Ctrl+C)"), nullptr, true), &QToolButton::clicked, this, [this] { copy(0); });
    connect(action(13, 54, 32, 22, tr("Копировать без ошибок"), nullptr, true), &QToolButton::clicked, this, [this] { copy(1); });
    action(14, 78, 32, 22, tr("Отменить (Ctrl+Z)"), &MainWindow::undo);
    action(18, 102, 32, 22, tr("Удалить нетекстовые клавиши"), &MainWindow::removeNonText);
    action(15, 126, 32, 22, tr("Пометить (Ins)"), &MainWindow::mark);
    action(21, 150, 32, 22, tr("Свойства видео"), nullptr);
    action(16, 174, 32, 22, tr("Настройка оси Y графиков"), &MainWindow::toggleAxisPanel);
    action(17, 198, 32, 22, tr("Легенда"), &MainWindow::toggleLegend);
    action(23, 222, 32, 22, tr("Ввод текста (F4)"), &MainWindow::showTextInput);
    action(25, 246, 32, 22, tr("Преобразовать в текущую раскладку"), nullptr);
    QToolButton *quit = toolButton(bar, 24, 270, 32, 22, tr("Выход"));
    quit->setEnabled(true);
    connect(quit, &QToolButton::clicked, this, &QWidget::close);

    bevel(bar, 297, 2, 2, 53, QFrame::VLine);
    m_capture = new QCheckBox(tr("Вкл"), bar);
    m_capture->setToolTip(tr("Управление перехватом (F8+F9)"));
    m_capture->setChecked(true);
    m_capture->move(304, 1);
    m_onlyText = new QCheckBox(tr("Только текст"), bar);
    m_onlyText->setToolTip(tr("Режим отображения"));
    m_onlyText->setChecked(true);
    m_onlyText->move(304, 17);

    bevel(bar, 396, 2, 2, 53, QFrame::VLine);
    m_byPauses = new QCheckBox(tr("Разбивать по паузам"), bar);
    m_byPauses->setToolTip(tr("Режим \"Один текст\"/\"Много текстов\""));
    m_byPauses->move(402, 1);
    auto *pauseLabel = new QLabel(tr("Пауза разбиения"), bar);
    pauseLabel->move(403, 18);
    m_pause = new QSpinBox(bar);
    m_pause->setRange(200, 10000);
    m_pause->setSingleStep(100);
    m_pause->setValue(2000);
    m_pause->setKeyboardTracking(false);
    m_pause->setToolTip(tr("Превышение этого времени считается за паузу в наборе"));
    m_pause->setGeometry(403, 32, 62, 21);
    auto *ms = new QLabel(tr("мс"), bar);
    ms->move(470, 35);

    bevel(bar, 537, 2, 2, 53, QFrame::VLine);
    auto *presetLabel = new QLabel(tr("Пресет"), bar);
    presetLabel->move(546, 9);
    auto *fingerLabel = new QLabel(tr("Пальцы"), bar);
    fingerLabel->move(546, 33);
    m_presets = new QComboBox(bar);
    m_presets->setToolTip(tr("Пресеты настроек"));
    m_presets->setGeometry(592, 4, 152, 21);
    m_fingers = new QComboBox(bar);
    m_fingers->setToolTip(tr("Начальная позиция пальцев и расстановка по зонам"));
    m_fingers->setGeometry(592, 28, 152, 21);
    m_fingers->addItems(m_schemes.names());
    connect(m_fingers, &QComboBox::currentTextChanged, this, &MainWindow::zonesChanged);
    m_newPresetButton = toolButton(bar, 8, 744, 3, 23, tr("Создать пресет (Правой кнопкой - удалить)"));
    m_newPresetButton->setEnabled(true);
    m_newPresetButton->installEventFilter(this);
    connect(m_newPresetButton, &QToolButton::clicked, this, &MainWindow::createPreset);
    m_presets->addItems(Presets::names());
    m_presets->setCurrentIndex(m_presets->findText(Presets::current()));
    connect(m_presets, &QComboBox::textActivated, this, &MainWindow::selectPreset);
    m_newZonesButton = toolButton(bar, 9, 744, 27, 23, tr("Создать расстановку (Правой кнопкой - удалить)"));
    m_newZonesButton->setEnabled(true);
    m_newZonesButton->installEventFilter(this);
    connect(m_newZonesButton, &QToolButton::clicked, this, &MainWindow::createFingerZones);
    QToolButton *editZones = toolButton(bar, 2, 768, 27, 23, tr("Редактировать расстановку"));
    editZones->setEnabled(true);
    connect(editZones, &QToolButton::clicked, this, &MainWindow::editFingerZones);

    connect(m_capture, &QCheckBox::toggled, this, &MainWindow::captureToggled);
    connect(m_onlyText, &QCheckBox::toggled, this, &MainWindow::recalculate);
    connect(m_byPauses, &QCheckBox::toggled, this, &MainWindow::recalculate);
    connect(m_pause, &QSpinBox::valueChanged, this, &MainWindow::recalculate);
    return bar;
}

void MainWindow::loadSettings()
{
    // The keys of the original's preset (FUN_0041ab08) with its effective defaults.
    const QSettings s;
    const QSignalBlocker b1(m_pause), b2(m_onlyText), b3(m_byPauses);
    m_pause->setValue(s.value(QStringLiteral("Pause"), 2000).toInt());
    m_onlyText->setChecked(s.value(QStringLiteral("TextOnly"), true).toBool());
    m_byPauses->setChecked(s.value(QStringLiteral("SplitOnEnter"), false).toBool());
    const QByteArray geometry = s.value(QStringLiteral("WindowGeometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else if (s.contains(QStringLiteral("MainWinWidth"))) {
        // Taken over from the original.
        resize(s.value(QStringLiteral("MainWinWidth")).toInt(), s.value(QStringLiteral("MainWinHeight"), height()).toInt());
        if (s.contains(QStringLiteral("MainWinLeft")))
            move(s.value(QStringLiteral("MainWinLeft")).toInt(), s.value(QStringLiteral("MainWinTop")).toInt());
    }
    // Heights of the text and the klavogram, width of the right panel; the graph takes the rest.
    const int total = m_leftSplit->sizes().value(0) + m_leftSplit->sizes().value(1) + m_leftSplit->sizes().value(2);
    const int textHeight = s.value(QStringLiteral("TextWinHeight"), 120).toInt();
    const int klavHeight = s.value(QStringLiteral("KlavWinHeight"), 200).toInt();
    m_leftSplit->setSizes({textHeight, std::max(20, total - textHeight - klavHeight), klavHeight});
    const int rightWidth = s.value(QStringLiteral("RightPanelWidth"), 218).toInt();
    m_mainSplit->setSizes({std::max(100, width() - rightWidth - m_mainSplit->handleWidth()), rightWidth});
    m_keys->horizontalHeader()->resizeSection(0, s.value(QStringLiteral("DlitCol1Width"), 80).toInt());
    m_keys->horizontalHeader()->resizeSection(1, s.value(QStringLiteral("DlitCol2Width"), 80).toInt());
    m_fingers->setCurrentIndex(std::max(0, m_fingers->findText(s.value(QStringLiteral("FingerZonesName")).toString())));

    m_live->loadSettings();
    m_extra->loadSettings();
    m_input->loadSettings();

    static const bool shownByDefault[GraphWidget::SeriesCount] = {false, true, false, false, false, true, true, false};
    for (int i = 0; i < GraphWidget::SeriesCount; ++i)
        if (i != GraphWidget::Pause)
            m_graph->setSeriesVisible(i, s.value(QLatin1String(kSeriesKeys[i]), shownByDefault[i]).toBool());
    if (s.value(QLatin1String(kSeriesKeys[GraphWidget::Pause]), false).toBool())
        m_graph->setSeriesVisible(GraphWidget::Pause, true);
    GraphWidget::AxisSettings axis;
    axis.speedMin = s.value(QStringLiteral("SpeedYmin"), 0).toInt();
    axis.speedMax = s.value(QStringLiteral("SpeedYmax"), -1).toInt();
    axis.rhythmMin = s.value(QStringLiteral("RythmYmin"), 0).toInt();
    axis.rhythmMax = s.value(QStringLiteral("RythmYmax"), 100).toInt();
    axis.autoRound = s.value(QStringLiteral("AutoRound"), true).toBool();
    m_axisPanel->setSettings(axis);
    m_graph->setAxisSettings(axis);
    m_axisPanel->setLockY(s.value(QStringLiteral("FixedY"), false).toBool());
    const int mode = s.value(QStringLiteral("PrimaryGraph"), 0).toInt();
    m_graph->setMode(mode >= 0 && mode <= 2 ? GraphWidget::Mode(mode) : GraphWidget::SpeedMode);
    m_legend->setMinimized(s.value(QStringLiteral("LegendMinimized"), false).toBool());
    m_legend->setVisible(s.value(QStringLiteral("LegendVisible"), true).toBool());
    if (s.contains(QStringLiteral("LegendWinLeft"))) {
        m_legend->move(s.value(QStringLiteral("LegendWinLeft")).toInt(), s.value(QStringLiteral("LegendWinTop")).toInt());
        m_legendPlaced = true;
    }
}

void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("Pause"), m_pause->value());
    s.setValue(QStringLiteral("TextOnly"), m_onlyText->isChecked());
    s.setValue(QStringLiteral("SplitOnEnter"), m_byPauses->isChecked());
    s.setValue(QStringLiteral("WindowGeometry"), saveGeometry());
    m_live->saveSettings();
    m_extra->saveSettings();
    m_input->saveSettings();
    s.setValue(QStringLiteral("TextWinHeight"), m_leftSplit->sizes().value(0));
    s.setValue(QStringLiteral("KlavWinHeight"), m_leftSplit->sizes().value(2));
    s.setValue(QStringLiteral("RightPanelWidth"), m_mainSplit->sizes().value(1));
    s.setValue(QStringLiteral("DlitCol1Width"), m_keys->horizontalHeader()->sectionSize(0));
    s.setValue(QStringLiteral("DlitCol2Width"), m_keys->horizontalHeader()->sectionSize(1));
    s.setValue(QStringLiteral("FingerZonesName"), m_fingers->currentText());

    for (int i = 0; i < GraphWidget::SeriesCount; ++i)
        s.setValue(QLatin1String(kSeriesKeys[i]), m_graph->seriesVisible(i));
    const GraphWidget::AxisSettings &axis = m_graph->axisSettings();
    s.setValue(QStringLiteral("SpeedYmin"), axis.speedMin);
    s.setValue(QStringLiteral("SpeedYmax"), axis.speedMax);
    s.setValue(QStringLiteral("RythmYmin"), axis.rhythmMin);
    s.setValue(QStringLiteral("RythmYmax"), axis.rhythmMax);
    s.setValue(QStringLiteral("AutoRound"), axis.autoRound);
    s.setValue(QStringLiteral("FixedY"), m_graph->lockY());
    s.setValue(QStringLiteral("PrimaryGraph"), int(m_graph->mode()));
    s.setValue(QStringLiteral("LegendMinimized"), m_legend->minimized());
    s.setValue(QStringLiteral("LegendVisible"), m_legend->isVisibleTo(this));
    s.setValue(QStringLiteral("LegendWinLeft"), m_legend->x());
    s.setValue(QStringLiteral("LegendWinTop"), m_legend->y());
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    dialog.save();
    if (dialog.languageChanged())
        QMessageBox::warning(this, tr("Предупреждение"), tr("Для смены языка перезапустите Ts"));
    applySettings();
}

void MainWindow::applySettings()
{
    // FUN_00429bbc; the rows of the statistics list follow MainOption* in updateStats().
    const QSettings s;
    const int textFont = std::clamp(s.value(QStringLiteral("TextFontSize"), 12).toInt(), 8, 24);
    m_text->setFontSize(textFont);
    m_input->setFontSize(textFont);
    m_tray->setVisible(s.value(QStringLiteral("MinimizeToTray"), false).toBool() && QSystemTrayIcon::isSystemTrayAvailable());
    m_klav->setFontSize(std::clamp(s.value(QStringLiteral("KlavogrFontSize"), 9).toInt(), 8, 24));
    m_keyDigits = std::clamp(s.value(QStringLiteral("DlitDigits"), 3).toInt(), 0, 3);
    m_live->setSpeedRange(s.value(QStringLiteral("opLoSpeed"), 200).toInt(), s.value(QStringLiteral("opHiSpeed"), 500).toInt());
    recalculate();
}

void MainWindow::showForm(const QString &name)
{
    if (name == QLatin1String("settings"))
        showSettings();
    else if (name == QLatin1String("extra"))
        showExtraStats();
    else if (name == QLatin1String("about"))
        AboutDialog(this).exec();
    else if (name == QLatin1String("input"))
        showTextInput();
    else if (name == QLatin1String("kbd"))
        editFingerZones();
    else if (name.startsWith(QLatin1String("hist"))) {
        showHistograms();
        if (name == QLatin1String("hist-fingers"))
            m_hist->setRoot(Histograms::Node::AllFingers);
        if (name == QLatin1String("hist-extra")) {
            showExtraStats();
            m_hist->setRoot(Histograms::Node::Extra);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    Presets::store(Presets::current()); // the preset follows what was changed while it was current
    e->accept();
    m_tray->hide();
    QApplication::quit(); // the tool windows do not keep the program running
}

void MainWindow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    // Minimized with the tray icon on: the window leaves the task bar.
    if (e->type() == QEvent::WindowStateChange && isMinimized() && m_tray && m_tray->isVisible())
        QTimer::singleShot(0, this, &QWidget::hide);
}

void MainWindow::restoreFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::captureToggled(bool on)
{
    m_tray->setToolTip(on ? QStringLiteral("Ts: ON") : QStringLiteral("Ts: OFF"));
    const QIcon icon = QApplication::windowIcon();
    m_tray->setIcon(on ? icon : QIcon(icon.pixmap(32, 32, QIcon::Disabled)));
    // Switched off: the keys still held get their releases.
    if (!on && Recorder::appendReleases(m_doc.records) > 0)
        recalculate();
    m_text->setFocus();
}

void MainWindow::showTextInput()
{
    m_input->show();
    m_input->raise();
    m_input->activateWindow();
}

void MainWindow::showHelpMenu()
{
    QMenu menu(this);
    menu.addAction(tr("О программе..."), this, [this] { AboutDialog(this).exec(); });
    menu.addAction(tr("Справка"), this, [] { QDesktopServices::openUrl(QUrl(QStringLiteral("http://fil.urikor.net"))); });
    menu.exec(m_helpButton->mapToGlobal(QPoint(0, m_helpButton->height())));
}

void MainWindow::selectPreset(const QString &name)
{
    // ComboBox1Select: the settings in use go to the preset they belong to, then the chosen one is read.
    saveSettings();
    Presets::store(Presets::current());
    Presets::load(name);
    Presets::setCurrent(name);
    loadSettings();
    applySettings();
}

void MainWindow::createPreset()
{
    // SpeedButton8Click: the settings in use under a new name.
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Создание нового пресета настроек"), tr("Название пресета"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty())
        return;
    saveSettings();
    Presets::store(name);
    Presets::setCurrent(name);
    if (m_presets->findText(name) < 0)
        m_presets->addItem(name);
    m_presets->setCurrentText(name);
}

void MainWindow::deletePreset()
{
    const QString name = Presets::current();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Удаление пресета"), tr("Вы действительно хотите удалить пресет?")) != QMessageBox::Yes)
        return;
    Presets::remove(name);
    m_presets->removeItem(m_presets->findText(name));
    if (m_presets->count() == 0) {
        Presets::setCurrent(QString());
        m_presets->setCurrentIndex(-1);
        return;
    }
    // The first of those left becomes current.
    m_presets->setCurrentIndex(0);
    Presets::setCurrent(m_presets->currentText());
    Presets::load(m_presets->currentText());
    loadSettings();
    applySettings();
}

void MainWindow::startCapture()
{
    m_hook.start();
}

void MainWindow::normalizeRecords()
{
    // Editing works on the records the text was built from (the original normalizes them in place).
    m_doc.records = m_model.records;
}

void MainWindow::selectionChanged()
{
    m_klav->scrollToPosition(m_text->selectionStart());
    updateStats();
    klavogramMoved();
    const bool selected = m_text->selectionLength() != 0;
    m_deleteButton->setEnabled(selected);
    m_blockButton->setEnabled(selected);
}

void MainWindow::deleteSelection()
{
    const auto [from, to] = Editing::recordRange(m_model, m_text->selectionStart(), m_text->selectionLength());
    if (to == 0)
        return;
    normalizeRecords();
    m_undo = m_doc.records;
    Editing::deleteRange(m_doc.records, from, to);
    recalculate();
}

void MainWindow::removeNonText()
{
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), m_byPauses->isChecked());
    normalizeRecords();
    m_undo = m_doc.records;
    Editing::removeNonText(m_doc.records, m_model.recordOfElement(b), m_model.recordOfElement(e));
    recalculate();
}

void MainWindow::undo()
{
    // One level: the records before the last deletion and the current ones change places.
    if (m_undo.isEmpty())
        return;
    normalizeRecords();
    m_doc.records.swap(m_undo);
    recalculate();
}

void MainWindow::editLabel(int record)
{
    normalizeRecords();
    askLabel(record);
}

void MainWindow::askLabel(int record)
{
    if (record < 0 || record >= m_doc.records.size())
        return;
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Текстовая метка"), tr("Текст метки"), QLineEdit::Normal,
                                               m_doc.records[record].comment, &ok);
    if (ok)
        m_doc.records[record].comment = text;
    const int scroll = m_text->verticalScrollBar()->value();
    recalculate();
    m_text->verticalScrollBar()->setValue(scroll);
}

void MainWindow::mark()
{
    const int start = m_text->selectionStart();
    const int from = m_model.recordAt(start), to = m_model.recordAt(start + m_text->selectionLength());
    if (from >= to) {
        editLabel(from); // no selection: the comment at the cursor
        return;
    }
    normalizeRecords();
    const int label = Editing::markRange(m_doc.records, from, to);
    if (label > 0) {
        askLabel(label);
        return;
    }
    const int scroll = m_text->verticalScrollBar()->value();
    recalculate();
    m_text->verticalScrollBar()->setValue(scroll);
}

void MainWindow::removeLabel(int record)
{
    normalizeRecords();
    Editing::removeLabel(m_doc.records, record);
    const int scroll = m_text->verticalScrollBar()->value();
    recalculate();
    m_text->verticalScrollBar()->setValue(scroll);
}

void MainWindow::copy(int kind)
{
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), m_byPauses->isChecked());
    QString text;
    if (kind == 2) {
        const QSettings s;
        Editing::TagOptions opt;
        opt.color = s.value(QStringLiteral("CopyBlock1"), true).toBool();
        opt.strike = s.value(QStringLiteral("CopyBlock2"), true).toBool();
        opt.colorNext = s.value(QStringLiteral("CopyBlock3"), true).toBool();
        text = Editing::copyTagged(m_model, b, e, opt);
    } else {
        text = Editing::copyText(m_model, b, e, kind == 1);
    }
    QApplication::clipboard()->setText(text.replace(QLatin1Char('\r'), QLatin1Char('\n')));
}

void MainWindow::textHovered(int textPos, const QPoint &globalPos)
{
    // Memo4MouseMove: the pause at a fragment separator, the label of marked text.
    m_labelRecord = -1;
    QString hint;
    if (textPos >= 0 && textPos < m_model.text.size() && m_model.size() > 0) {
        if (m_model.text.at(textPos) == QChar(0x2021)) {
            const int e = m_model.elementAt(textPos);
            if (e > 0 && e < m_model.size() && !m_model.klav.isEmpty()) {
                const int last = int(m_model.klav.size()) - 1;
                const qint64 gap = m_model.klav[std::min(m_model.klavOfElement(e), last)].t
                                   - m_model.klav[std::min(m_model.klavOfElement(e - 1), last)].t;
                hint = formatFixed(double(0.001L * gap), 3, QLocale()) + QLatin1Char(' ') + Texts::units().ms;
            }
        } else {
            const int r = m_model.recordAt(textPos);
            if (r < m_model.records.size()) {
                const int start = Editing::labelStart(m_model.records, r);
                if (start >= 0) {
                    m_labelRecord = start;
                    hint = m_model.records[start].comment;
                } else if (!m_model.records[r].comment.isEmpty()) {
                    m_labelRecord = r;
                }
            }
        }
    }
    if (hint.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(globalPos, hint, m_text);
}

void MainWindow::showTextMenu(const QPoint &globalPos)
{
    const int label = m_labelRecord;
    QMenu menu(this);
    if (m_text->selectionLength() != 0) {
        menu.addAction(tr("Удалить"), this, &MainWindow::deleteSelection);
        menu.addAction(tr("Пометить"), this, &MainWindow::mark);
    }
    if (label >= 0) {
        menu.addAction(tr("Редактировать метку"), this, [this, label] { editLabel(label); });
        menu.addAction(tr("Удалить метку"), this, [this, label] { removeLabel(label); });
    }
    menu.addAction(tr("Копировать"), this, [this] { copy(0); });
    menu.addAction(tr("Копировать без ошибок"), this, [this] { copy(1); });
    menu.addAction(tr("Копировать с тегами"), this, [this] { copy(2); });
    menu.exec(globalPos);
}

void MainWindow::keyEvent(const HookEvent &e)
{
    const QSettings settings;
    RecorderSettings s;
    s.capture = m_capture->isChecked();
    s.globalOnOff = settings.value(QStringLiteral("GlobalOnOff"), true).toBool();
    s.globalClear = settings.value(QStringLiteral("GlobalClear"), true).toBool();
    s.autoComments = settings.value(QStringLiteral("AutoComments"), false).toBool();
    s.splitMs = m_pause->value();
    s.byPauses = m_byPauses->isChecked();
    s.liveVisible = m_live->isVisible();
    Recorder::Context c;
    // Typing into the program's own windows is not recorded - except the text input window, which is
    // there to be typed into (its Esc and F2 are commands, though).
    const QWidget *active = QApplication::activeWindow();
    const quint32 vk = (e.flags >> 16) & 0xFF;
    c.ownWindow = active != nullptr && (active != m_input || vk == 0x1B || vk == 0x71);
    c.foregroundWindow = KeyboardHook::foregroundWindow();
    c.comment = [] {
        // Date and time as the system writes them, then the title of the window typed into.
        const QLocale system = QLocale::system();
        const QDateTime now = QDateTime::currentDateTime();
        return system.toString(now.date(), QLocale::ShortFormat) + QLatin1Char(' ')
               + now.time().toString(QStringLiteral("H:mm:ss")) + QLatin1Char(' ') + KeyboardHook::foregroundTitle();
    };

    const Recorder::Outcome out = m_recorder.handle(e, s, c, m_doc.records);
    if (out.setCapture)
        m_capture->setChecked(*out.setCapture);
    if (out.clear)
        QTimer::singleShot(0, this, &MainWindow::clear); // not inside the hook
    if (out.toggleLive)
        m_live->setVisible(!m_live->isVisible());
    if (out.liveChanged)
        m_livePending = true;
    if (out.liveReset)
        m_liveTicks = 10; // shown at the next tick
    if (out.recorded) {
        if (settings.value(QStringLiteral("JournalOn"), false).toBool())
            m_journal.append(m_doc.records.last());
        m_needRecalc = true;
        m_lastKey.start();
    }
}

void MainWindow::tick()
{
    // The text is rebuilt when the user comes back to the window (Timer1Timer).
    if (m_needRecalc && (isActiveWindow() || (m_input->isActiveWindow() && m_lastKey.elapsed() > 800))) {
        m_needRecalc = false;
        recalculate();
        m_text->moveCursor(QTextCursor::End);
    }
    // The running statistics are shown at most twice a second.
    if (m_livePending && ++m_liveTicks >= 5) {
        m_livePending = false;
        m_liveTicks = 0;
        m_live->setStats(m_recorder.live());
    }
}

void MainWindow::showLiveStats()
{
    m_live->setVisible(!m_live->isVisible());
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateKeyList(); // the list holds another number of rows
    m_legend->keepInside();
    m_axisPanel->keepInside();
}

void MainWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_legendPlaced) {
        // Over the top left corner of the graph, next to the axis.
        m_legendPlaced = true;
        m_legend->move(32, m_graph->mapTo(this, QPoint(0, 0)).y());
    }
    m_legend->keepInside();
}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
    // The wheel over the graph moves its scroll bar by a small step.
    if (o == m_graph && e->type() == QEvent::Wheel) {
        const int delta = static_cast<QWheelEvent *>(e)->angleDelta().y();
        if (delta)
            m_graphScroll->setValue(m_graphScroll->value() + (delta > 0 ? -1 : 1) * m_graphScroll->singleStep());
        return true;
    }
    if (o == m_newPresetButton && e->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(e)->button() == Qt::RightButton) {
        deletePreset();
        return true;
    }
    // The right button on "create a layout" deletes the current one.
    if (o == m_newZonesButton && e->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(e)->button() == Qt::RightButton) {
        deleteFingerZones();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

TableExport::Table MainWindow::keyTable() const
{
    // SpeedButton7Click: every press of the recording - key, pause before it, how long it was held.
    const QLocale loc;
    const QString ms = QStringLiteral(", ") + Texts::units().ms;
    TableExport::Table table;
    table.header = {tr("Клавиша"), tr("Пауза") + ms, tr("Длительность") + ms};
    auto number = [&loc](const QString &text) {
        bool ok = false;
        const double v = loc.toDouble(text, &ok);
        return ok ? QVariant(v) : QVariant();
    };
    const auto all = KeyList::rows(m_model.klav, loc);
    for (const KeyListRow &row : all)
        table.rows.append({row.key, number(row.pause), number(row.duration)});
    return table;
}

TableExport::Table MainWindow::extraTable() const
{
    const bool averages = m_extra->averages();
    TableExport::Table table;
    table.header = {tr("Текст"), tr("Скорость")};
    if (averages)
        table.header << tr("Кол-во");
    for (const ExtraStats::Row &row : m_extra->rows()) {
        QVariantList cells{row.text, double(qRound(double(row.speed) * 100.0)) / 100.0};
        if (averages)
            cells << row.value;
        table.rows.append(cells);
    }
    return table;
}

void MainWindow::exportTable(const TableExport::Table &table, bool chart)
{
    if (table.rows.isEmpty())
        return;
    QString filter;
    QString path = QFileDialog::getSaveFileName(this, {}, QFileInfo(m_path).path(),
                                                QStringLiteral("Excel (*.xlsx);;CSV (*.csv)"), &filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += filter.contains(QLatin1String("csv")) ? QStringLiteral(".csv") : QStringLiteral(".xlsx");
    if (!TableExport::write(path, table, chart, QLocale())) {
        QMessageBox::warning(this, appTitle(), tr("Не удалось сохранить файл %1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path)); // the original shows the table in Excel at once
}

void MainWindow::zonesChanged()
{
    m_klav->setZones(m_schemes.zones(m_fingers->currentText()));
    updateExtraStats();
    updateHistograms();
}

void MainWindow::editFingerZones()
{
    // SpeedButton2Click: the editor works on the current layout, which is stored afterwards.
    const QString name = m_fingers->currentText();
    FingerZonesDialog dialog(name, m_schemes.zones(name), this);
    dialog.exec();
    if (dialog.zones().readOnly())
        return;
    m_schemes.store(name, dialog.zones());
    zonesChanged();
}

void MainWindow::createFingerZones()
{
    // SpeedButton9Click: the current layout under a new name.
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Создание расстановки пальцев"), tr("Название расстановки"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty() || m_fingers->findText(name) >= 0)
        return;
    FingerZones zones = m_schemes.zones(m_fingers->currentText());
    zones.setReadOnly(false);
    m_schemes.store(name, zones);
    m_fingers->addItem(name);
    m_fingers->setCurrentText(name);
}

void MainWindow::deleteFingerZones()
{
    if (m_fingers->currentIndex() <= 0) {
        QMessageBox::warning(this, tr("Ошибка удаления расстановки"), tr("Стандартную расстановку удалить нельзя"));
        return;
    }
    if (QMessageBox::question(this, tr("Удаление расстановки пальцев"), tr("Вы действительно хотите удалить расстановку?"))
        != QMessageBox::Yes)
        return;
    m_schemes.remove(m_fingers->currentText());
    m_fingers->removeItem(m_fingers->currentIndex());
    m_fingers->setCurrentIndex(0);
}

void MainWindow::createGraphPanels()
{
    m_legend = new LegendPanel(m_graph, this);
    m_axisPanel = new AxisPanel(this);
    m_axisPanel->move(121, 121);

    connect(m_graph, &GraphWidget::viewChanged, this, &MainWindow::graphMoved);
    connect(m_graph, &GraphWidget::axisMenuRequested, this, &MainWindow::showAxisMenu);
    connect(m_graph, &GraphWidget::autoLimitsChanged, this, [this] { m_axisPanel->setAutoLimits(m_graph->autoLimits()); });
    connect(m_graph, &GraphWidget::elementClicked, this, &MainWindow::scrollKlavogramToElement);
    connect(m_graph, &GraphWidget::klavogramSpanRequested, this, [this](int element) {
        // The klavogram is zoomed to span from its first element to the one under the mouse.
        const float from = float(0.001L * drawTimeOfElement(m_graph->klavogramFrom()));
        const float span = float(0.001L * (drawTimeOfElement(element) - 100)) - from;
        m_klav->setZoom(span > 0.01f ? float(m_klav->width()) / span : 300.0f);
        klavogramMoved();
    });
    connect(m_graphScroll, &QScrollBar::valueChanged, this, [this](int value) {
        m_graph->setScrollValue(value);
        graphMoved();
    });
    connect(m_axisPanel, &AxisPanel::settingsChanged, this, [this] {
        m_graph->setAxisSettings(m_axisPanel->settings());
        klavogramMoved();
    });
    connect(m_axisPanel, &AxisPanel::lockYChanged, m_graph, &GraphWidget::setLockY);
}

void MainWindow::scrollKlavogramToElement(int element)
{
    // The klavogram starts at the element (FUN_004050cc).
    m_klav->setScrollMs(float(0.001L * (drawTimeOfElement(element) - 100)));
    klavogramMoved();
}

void MainWindow::toggleAxisPanel()
{
    m_axisPanel->setVisible(!m_axisPanel->isVisible());
    m_axisPanel->raise();
    m_axisPanel->keepInside();
}

void MainWindow::toggleLegend()
{
    m_legend->setVisible(!m_legend->isVisible());
    m_legend->raise();
    m_legend->keepInside();
}

void MainWindow::showAxisMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Настройка оси Y"), this, &MainWindow::toggleAxisPanel);
    if (!m_legend->isVisible())
        menu.addAction(tr("Показать легенду"), this, &MainWindow::toggleLegend);
    menu.addSeparator();
    const QString modes[] = {tr("Скорость"), tr("Ритмичность"), tr("Гистограмма")};
    for (int i = 0; i < 3; ++i) {
        QAction *a = menu.addAction(modes[i], this, [this, i] { m_graph->setMode(GraphWidget::Mode(i)); });
        a->setCheckable(true);
        a->setChecked(m_graph->mode() == i);
    }
    menu.exec(globalPos);
}

qint64 MainWindow::drawTimeOfElement(int element) const
{
    if (m_model.klav.isEmpty())
        return 0;
    const int k = m_model.klavOfElement(element);
    return k < m_model.klav.size() ? m_model.klav[k].tDraw : m_model.klav.last().tDraw;
}

void MainWindow::syncGraphScrollBar()
{
    const GraphWidget::ScrollParams s = m_graph->scrollParams();
    const QSignalBlocker blocker(m_graphScroll);
    m_graphScroll->setRange(s.min, std::max(s.min, s.max));
    m_graphScroll->setSingleStep(s.singleStep);
    m_graphScroll->setPageStep(s.pageStep);
    m_graphScroll->setValue(s.value);
}

void MainWindow::graphMoved()
{
    // The part shown on the klavogram is kept in sight: the klavogram follows the graph.
    if (m_graph->pullKlavogramRange())
        m_klav->setScrollMs(float(0.001L * drawTimeOfElement(m_graph->klavogramFrom())));
    klavogramMoved();
}

void MainWindow::klavogramMoved()
{
    // The part of the text that is on the klavogram is highlighted there and on the graph (FUN_00424e64).
    const auto [first, last] = m_klav->visibleRecords();
    const int from = m_model.elementOfKlav(first), to = m_model.elementOfKlav(last);
    m_text->setVisibleRange(m_model.positionOfElement(from), m_model.positionOfElement(to));
    m_graph->setKlavogramRange(from, to);
    syncGraphScrollBar();
    updateKeyList();
}

RecalcOptions MainWindow::options() const
{
    RecalcOptions opt;
    opt.splitMs = m_pause->value();
    opt.onlyText = m_onlyText->isChecked();
    opt.byPauses = m_byPauses->isChecked();
    return opt;
}

void MainWindow::recalculate()
{
    m_model = Recalc::run(m_doc.records, options());
    m_text->setModel(m_model);
    m_klav->setModel(&m_model);
    m_graph->setModel(&m_model);
    m_saveButton->setEnabled(!m_doc.records.isEmpty());
    updateStats();
    klavogramMoved();
}

void MainWindow::updateStats()
{
    const RecalcOptions opt = options();
    const QLocale loc;
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), opt.byPauses);
    const QStringList names = Texts::statsRowNames();
    const QStringList values = m_model.size() ? Stats::format(Stats::compute(m_model, b, e, opt.splitMs, opt.byPauses), loc, Texts::units())
                                              : QStringList();
    const QSettings s;
    QVector<QStringList> rows;
    for (int i = 0; i < names.size(); ++i)
        if (s.value(QStringLiteral("MainOption%1").arg(i), true).toBool())
            rows.append({names[i], values.value(i)});
    setRows(m_stats, rows);
    // The list is as tall as its rows plus one (FUN_00429bbc); the key list gets the rest.
    const int height = m_stats->horizontalHeader()->height() + 2 * m_stats->frameWidth()
                       + int(rows.size() + 1) * m_stats->verticalHeader()->defaultSectionSize();
    if (m_stats->height() != height) {
        m_stats->setFixedHeight(height);
        updateKeyList();
    }
    updateExtraStats();
    updateHistograms();
}

void MainWindow::updateHistograms()
{
    if (!m_hist || !m_hist->isVisible())
        return;
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), m_byPauses->isChecked());
    Histograms::Source source;
    source.model = &m_model;
    std::tie(source.recBegin, source.recEnd) = Histograms::recordRange(m_model, b, e);
    source.splitUs = quint32(m_pause->value()) * 1000;
    source.zones = m_schemes.zones(m_fingers->currentText());
    source.label = Histograms::labelsFromRecords(m_model.records);
    source.names = Texts::histogramNames();
    m_hist->setSource(source);
}

void MainWindow::showHistograms()
{
    m_hist->show();
    m_hist->raise();
    m_hist->activateWindow();
}

void MainWindow::updateExtraStats()
{
    // The extra statistics are about the same part of the text as the main ones.
    if (!m_extra || !m_extra->isVisible())
        return;
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), m_byPauses->isChecked());
    m_extra->setSource(&m_model, fingerSeries(m_model, m_schemes.zones(m_fingers->currentText())), b, e);
}

void MainWindow::showExtraStats()
{
    m_extra->show();
    m_extra->raise();
    m_extra->activateWindow();
}

void MainWindow::updateKeyList()
{
    if (!m_keys)
        return;
    const auto [from, to] = m_klav->visibleSpanUs();
    // As many rows as fit in the list.
    const int limit = std::max(1, m_keys->viewport()->height() / m_keys->verticalHeader()->defaultSectionSize());
    QVector<QStringList> rows;
    for (const KeyListRow &row : KeyList::rows(m_model.klav, QLocale(), from, to, limit, m_keyDigits))
        rows.append({row.pause, row.duration, row.key});
    setRows(m_keys, rows);
}

void MainWindow::setDocument(const TsfDocument &doc, const QString &title, bool damaged)
{
    m_doc = doc;
    setWindowTitle(title.isEmpty() ? appTitle() : appTitle() + QStringLiteral(" - ") + title);
    m_damaged->setVisible(damaged);
    m_damaged->raise();
    recalculate();
}

bool MainWindow::openFile(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (Journal::isJournal(path)) {
        TsfDocument doc;
        m_journal.close(); // the journal being written may be the one to read
        if (!Journal::read(path, doc.records)) {
            QMessageBox::warning(this, appTitle(), tr("Не удалось открыть файл %1").arg(path));
            return false;
        }
        m_path.clear(); // a journal is saved as a new .tsf
        m_loaded = false;
        m_undo.clear();
        m_clean = true;
        setDocument(doc, tr("Журнал %1").arg(name), false);
        return true;
    }
    TsfDocument doc;
    const Tsf::ReadError err = Tsf::read(path, doc);
    if (err == Tsf::ReadError::CannotOpen) {
        QMessageBox::warning(this, appTitle(), tr("Не удалось открыть файл %1").arg(path));
        return false;
    }
    if (err == Tsf::ReadError::NewerVersion)
        QMessageBox::warning(this, appTitle(), tr("Этот файл создан в более поздней версии программы."));
    m_path = path;
    m_loaded = true;
    m_undo.clear();
    m_clean = doc.signed_ && doc.signatureValid;
    if (!doc.fingerZonesName.isEmpty()) {
        // The finger layout of the file: an equal one that is already known, or a new one (LoadTsf).
        const QString scheme = m_schemes.adopt(doc.fingerZonesName, FingerZones::fromStrings(doc.fingers));
        if (m_fingers->findText(scheme) < 0)
            m_fingers->addItem(scheme);
        m_fingers->setCurrentText(scheme);
    }
    setDocument(doc, name, doc.signed_ && !doc.signatureValid);
    return true;
}

void MainWindow::open()
{
    const QString path = QFileDialog::getOpenFileName(
        this, {}, QFileInfo(m_path).path(),
        QStringLiteral("Typing statistics files (*.tsf);;Typing statistics journals (*.tsj)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::openJournal()
{
    // This month's journal, as the original's button does.
    const QString path = JournalWriter(QCoreApplication::applicationDirPath()).path();
    if (QFileInfo::exists(path))
        openFile(path);
    else
        QMessageBox::information(this, appTitle(), tr("Журнала за этот месяц нет: %1").arg(path));
}

void MainWindow::save()
{
    saveDocument(false);
}

void MainWindow::saveDocument(bool block)
{
    // SaveTsf: the properties first. A loaded file keeps its author and date; a new recording and a
    // block get the author from the settings and the current time.
    QSettings settings;
    const bool fresh = !m_loaded || block;
    FilePropertiesDialog properties(this);
    if (fresh)
        properties.setProperties(settings.value(QStringLiteral("UserName")).toString(), nowString(), QString(), false);
    else
        properties.setProperties(m_doc.author, m_doc.date, m_doc.comment, true);
    if (properties.exec() != QDialog::Accepted)
        return;
    QString path = QFileDialog::getSaveFileName(this, {}, m_path, QStringLiteral("Typing statistics files (*.tsf)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".tsf");

    TsfDocument doc = m_doc;
    doc.author = properties.author();
    doc.date = properties.date();
    doc.comment = properties.description();
    if (!m_loaded && doc.author != settings.value(QStringLiteral("UserName")).toString())
        settings.setValue(QStringLiteral("UserName"), doc.author);
    if (block) {
        // The records of the selection; nothing selected - all of them.
        const auto [from, to] = Editing::recordRange(m_model, m_text->selectionStart(), m_text->selectionLength());
        doc.records = to == 0 ? m_model.records : m_model.records.mid(from, to - from);
        doc.attachedVideo.clear();
    }
    // The finger layout goes into the file unless it is the built-in one.
    doc.fingerZonesName.clear();
    doc.fingers.clear();
    if (m_fingers->currentIndex() > 0) {
        doc.fingerZonesName = m_fingers->currentText();
        doc.fingers = m_schemes.zones(m_fingers->currentText()).toStrings();
    }
    // A recording made here or loaded with a valid signature is signed (g_fileClean).
    if (!Tsf::write(path, doc, m_clean)) {
        QMessageBox::warning(this, appTitle(), tr("Не удалось сохранить файл %1").arg(path));
        return;
    }
    if (block)
        return;
    const KeyRecords records = m_doc.records;
    m_doc = doc;
    m_doc.records = records;
    m_path = path;
    m_loaded = true;
    setWindowTitle(appTitle() + QStringLiteral(" - ") + QFileInfo(path).fileName());
}

void MainWindow::clear()
{
    m_path.clear();
    m_clean = true;
    m_loaded = false;
    m_undo.clear();
    m_needRecalc = false;
    m_capture->setChecked(true);
    m_input->clear();
    setDocument({}, {}, false);
}
