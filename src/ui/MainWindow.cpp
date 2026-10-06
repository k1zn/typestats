#include "MainWindow.h"

#include "AboutDialog.h"
#include "Presets.h"
#include "StampRecorder.h"
#include "TextInputWindow.h"
#include "GraphPanels.h"
#include "GraphWidget.h"
#include "HistogramWindow.h"
#include "KlavogramWidget.h"
#include "ExtraStatsWindow.h"
#include "AppPaths.h"
#include "Hotkeys.h"
#include "LiveStatsWindow.h"
#include "Look.h"
#include "SettingsDialog.h"
#include "TextView.h"
#include "Texts.h"
#include "FilePropertiesDialog.h"
#include "FingerZonesDialog.h"
#include "core/Editing.h"
#include "core/Ext80.h"
#include "core/Journal.h"
#include "core/KeyList.h"
#include "core/MainStats.h"
#include "core/NumberFormat.h"
#include "platform/FileAssociation.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QSystemTrayIcon>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QToolButton>
#include <QToolTip>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

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
            // A new item repaints the list: the key list is set on every step of scrolling.
            if (const QTableWidgetItem *old = list->item(r, c); old && old->text() == rows[r][c])
                continue;
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
    : QWidget(parent), m_schemes(AppPaths::file(QStringLiteral("FingerZones.ini"))), m_journal(AppPaths::dataDir())
{
    m_doc.platform = currentKeyPlatform(); // a recording made here
    m_stamps = new StampRecorder(this);
    m_stamps->attach(&m_doc);
    connect(m_stamps, &StampRecorder::changed, this, [this] {
        m_unsaved = true;
        updateProof();
    });
    updateTitle();
    setMinimumWidth(220);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(createToolBar());

    // Left: text, graph with its scroll bar, klavogram.
    m_text = new TextView;
    auto *graphPane = new QWidget;
    m_graphPane = graphPane;
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
    connect(left, &QSplitter::splitterMoved, this, &MainWindow::graphPaneResized);
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
    connect(&m_hook, &KeyboardHook::failed, this, &MainWindow::hookFailed);
    connect(&m_hook, &KeyboardHook::started, this, &MainWindow::hookStarted);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::tick);
    timer->start(100);

    resize(876, 579);
    updateThemeColors();
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
    action(1, 6, 4, 23, tr("Очистить (%1)").arg(Hotkeys::clear()), &MainWindow::clear);
    action(6, 30, 4, 23, tr("Прочитать"), &MainWindow::open);
    m_saveButton = action(5, 54, 4, 23, tr("Сохранить"), &MainWindow::save);
    m_blockButton = action(27, 78, 4, 23, tr("Сохранить блок"), nullptr);
    connect(m_blockButton, &QToolButton::clicked, this, [this] { saveDocument(true); });
    action(26, 102, 4, 23, tr("Открыть журнал"), &MainWindow::openJournal);
    connect(action(7, 126, 4, 23, tr("Экспортировать в Excel"), nullptr, true), &QToolButton::clicked, this,
            [this] { exportTable(keyTable(), true); });
    action(12, 150, 4, 23, tr("Дополнительная статистика"), &MainWindow::showExtraStats);
    action(19, 174, 4, 23, tr("Статистические гистограммы"), &MainWindow::showHistograms);
    action(20, 198, 4, 23, tr("Видео"), nullptr); // the attached video is not in the port: always disabled
    action(22, 222, 4, 23, tr("Настройки..."), &MainWindow::showSettings);
    action(4, 246, 4, 23, tr("Оперативная статистика"), &MainWindow::showLiveStats);
    m_helpButton = action(3, 270, 4, 23, tr("О программе"), &MainWindow::showHelpMenu);
    bevel(bar, 3, 29, 294, 2, QFrame::HLine);
    // Lower row.
    m_deleteButton = action(10, 6, 32, 22, tr("Удалить (%1)").arg(Hotkeys::deleteKey()), &MainWindow::deleteSelection);
    m_deleteButton->setEnabled(false);
    connect(action(11, 30, 32, 22, tr("Копировать (%1)").arg(Hotkeys::copy()), nullptr, true), &QToolButton::clicked, this, [this] { copy(0); });
    connect(action(13, 54, 32, 22, tr("Копировать без ошибок"), nullptr, true), &QToolButton::clicked, this, [this] { copy(1); });
    action(14, 78, 32, 22, tr("Отменить (%1)").arg(Hotkeys::undo()), &MainWindow::undo);
    action(18, 102, 32, 22, tr("Удалить нетекстовые клавиши"), &MainWindow::removeNonText);
    action(15, 126, 32, 22, tr("Пометить (%1)").arg(Hotkeys::mark()), &MainWindow::mark);
    action(21, 150, 32, 22, tr("Свойства видео"), nullptr);
    m_axisButton = action(16, 174, 32, 22, tr("Настройка оси Y графиков"), &MainWindow::showAxisPanel);
    m_legendButton = action(17, 198, 32, 22, tr("Легенда"), &MainWindow::showLegend);
    action(23, 222, 32, 22, tr("Ввод текста (%1)").arg(Hotkeys::textInput()), &MainWindow::showTextInput);
    action(25, 246, 32, 22, tr("Преобразовать в текущую раскладку"), &MainWindow::convertLayout);
    QToolButton *quit = toolButton(bar, 24, 270, 32, 22, tr("Выход"));
    quit->setEnabled(true);
    connect(quit, &QToolButton::clicked, this, &QWidget::close);

    bevel(bar, 297, 2, 2, 53, QFrame::VLine);
    m_capture = new QCheckBox(tr("Вкл"), bar);
    m_capture->setToolTip(tr("Управление перехватом (%1)").arg(Hotkeys::onOff()));
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

    // The empty right corner: the theme (kept there by eventFilter when the window is resized).
    m_themeButton = new QToolButton(bar);
    m_themeButton->setIconSize(QSize(16, 16));
    m_themeButton->setFocusPolicy(Qt::NoFocus);
    m_themeButton->setFixedSize(23, 23);
    m_themeButton->setCheckable(true);
    m_themeButton->setChecked(Look::isDark());
    connect(m_themeButton, &QToolButton::toggled, this, &MainWindow::setDarkTheme);
    // Left of it: the time stamps of the recording, when there are any or they are being taken.
    m_proofButton = new QToolButton(bar);
    m_proofButton->setFocusPolicy(Qt::NoFocus);
    m_proofButton->setAutoRaise(true);
    m_proofButton->setFixedHeight(23);
    m_proofButton->hide();
    connect(m_proofButton, &QToolButton::clicked, this, &MainWindow::showProof);
    bar->installEventFilter(this);
    return bar;
}

namespace {

// A crescent (the dark theme is off) or a sun (on).
QIcon themeIcon(bool dark)
{
    QIcon icon;
    for (int size : {16, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 16.0, size / 16.0);
        if (!dark) {
            QPainterPath moon, bite;
            moon.addEllipse(QRectF(2, 2, 12, 12));
            bite.addEllipse(QRectF(6.5, 0, 11, 11));
            p.fillPath(moon.subtracted(bite), QColor(72, 84, 150));
        } else {
            const QColor sun(255, 186, 32);
            const QPointF center(8, 8);
            p.setPen(Qt::NoPen);
            p.setBrush(sun);
            p.drawEllipse(center, 3.4, 3.4);
            p.setPen(QPen(sun, 1.5, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 8; ++i) {
                const double a = i * M_PI / 4;
                const QPointF d(std::cos(a), std::sin(a));
                p.drawLine(center + d * 5.2, center + d * 7.2);
            }
        }
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace

void MainWindow::setDarkTheme(bool on)
{
    Look::setDark(on);
    updateThemeColors();
}

void MainWindow::updateThemeColors()
{
    const bool dark = Look::isDark();
    m_themeButton->setIcon(themeIcon(dark));
    m_themeButton->setToolTip(dark ? tr("Светлая тема") : tr("Тёмная тема"));
    QPalette warnPalette = m_damaged->palette();
    warnPalette.setColor(QPalette::Window, Look::colors().damaged);
    warnPalette.setColor(QPalette::WindowText, Look::colors().damagedText);
    m_damaged->setPalette(warnPalette);
    m_legend->updateColors();
    m_live->updateColors();
    if (m_model.text.isEmpty())
        return;
    // The colours of the text styles are in its document: it is built again, with the selection and the scroll.
    const int anchor = m_text->textCursor().anchor(), position = m_text->textCursor().position(); // numbers: a cursor
    const int scroll = m_text->verticalScrollBar()->value();                                        // follows the edits
    m_text->setModel(m_model);
    {
        const QSignalBlocker b(m_text);
        QTextCursor c(m_text->document());
        c.setPosition(anchor);
        c.setPosition(position, QTextCursor::KeepAnchor);
        m_text->setTextCursor(c);
    }
    m_text->verticalScrollBar()->setValue(scroll);
    klavogramMoved(); // the highlight of the part on the klavogram
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
    graphPaneResized();

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
    m_legendOpen = s.value(QStringLiteral("LegendVisible"), true).toBool();
    m_legend->setVisible(m_legendOpen && !m_graphFolded);
    updatePanelButtons();
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
    s.setValue(QStringLiteral("LegendVisible"), m_legendOpen);
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
    m_globalOnOff = s.value(QStringLiteral("GlobalOnOff"), true).toBool();
    m_globalClear = s.value(QStringLiteral("GlobalClear"), true).toBool();
    m_autoComments = s.value(QStringLiteral("AutoComments"), false).toBool();
    m_journalOn = s.value(QStringLiteral("JournalOn"), false).toBool();
    const bool stamping = s.value(QStringLiteral("StampRecording"), false).toBool();
    if (stamping != m_stamps->enabled()) {
        m_stamps->setEnabled(stamping);
        updateProof();
    }
    m_mainOptions.resize(MainStats::RowCount);
    for (int i = 0; i < MainStats::RowCount; ++i)
        m_mainOptions[i] = s.value(QStringLiteral("MainOption%1").arg(i), true).toBool();
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
    if (!askToSave()) {
        e->ignore();
        return;
    }
    saveSettings();
    Presets::store(Presets::current()); // the preset follows what was changed while it was current
    e->accept();
    m_tray->hide();
    QApplication::quit(); // the tool windows do not keep the program running
}

bool MainWindow::askToSave()
{
    // Not in the original: it closed without asking.
    QSettings settings;
    if (!m_unsaved || m_doc.records.isEmpty() || !settings.value(QStringLiteral("AskSaveOnExit"), true).toBool())
        return true;
    if (isHidden())
        restoreFromTray();
    QMessageBox box(QMessageBox::Question, appTitle(), tr("Записи не сохранены. Сохранить их перед выходом?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    auto *never = new QCheckBox(tr("Больше не спрашивать"), &box);
    box.setCheckBox(never);
    const int answer = box.exec();
    if (answer == QMessageBox::Cancel)
        return false;
    if (never->isChecked())
        settings.setValue(QStringLiteral("AskSaveOnExit"), false);
    return answer == QMessageBox::Discard || saveDocument(false);
}

void MainWindow::updateProof()
{
    // Once per turn of the event loop: a stamp, an edit and a document may come together.
    if (m_proofPending)
        return;
    m_proofPending = true;
    QTimer::singleShot(0, this, [this] {
        m_proofPending = false;
        if (m_doc.stamps.isEmpty() && !m_stamps->enabled()) {
            m_proofButton->hide();
            return;
        }
        const Stamps::Report r = Stamps::verify(Recalc::normalized(m_doc.records), m_doc.stamps, m_doc.stampCertificates);
        // Short: the corner has room for little; the words are in the hint and the details.
        QString text, hint;
        QColor color = Look::colors().dimInk;
        const qint64 percent = r.records ? qint64(r.confirmed) * 100 / r.records : 0;
        switch (r.status) {
        case Stamps::Report::Status::None:
            text = QStringLiteral("⏱");
            hint = tr("Запись будет заверена метками времени");
            break;
        case Stamps::Report::Status::Confirmed:
            text = QStringLiteral("✓ 100 %");
            hint = tr("Запись заверена метками времени");
            color = Look::colors().proofOk;
            break;
        case Stamps::Report::Status::Partial:
            text = QStringLiteral("✓ %1 %").arg(percent);
            hint = tr("Время подтверждено для %1 % записей").arg(percent);
            color = Look::colors().proofPartial;
            break;
        case Stamps::Report::Status::Broken:
            text = QStringLiteral("✗");
            hint = tr("Метки времени не сходятся с записью");
            color = Look::colors().proofBad;
            break;
        }
        if (!m_stamps->lastError().isEmpty()) {
            text += QStringLiteral(" !");
            hint += u'\n' + tr("Метку времени получить не удалось: %1").arg(m_stamps->lastError());
        }
        m_proofButton->setText(text);
        m_proofButton->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: bold; }").arg(color.name()));
        m_proofButton->setToolTip(hint + u'\n' + tr("Подробнее — по щелчку"));
        m_proofButton->adjustSize();
        m_proofButton->move(m_themeButton->x() - m_proofButton->width() - 4, 4);
        m_proofButton->show();
    });
}

void MainWindow::showProof()
{
    const Stamps::Report r = Stamps::verify(Recalc::normalized(m_doc.records), m_doc.stamps, m_doc.stampCertificates);
    const QLocale loc;
    const auto percent = [&](int part) { return r.records ? qint64(part) * 100 / r.records : 0; };
    QStringList lines;
    if (r.stamps == 0) {
        lines << tr("Меток времени пока нет: они ставятся во время набора.");
    } else {
        lines << tr("Меток времени: %1 (%2)").arg(r.stamps).arg(r.authorities.join(QStringLiteral(", ")));
        if (r.firstMs)
            lines << tr("Набрано: %1 — %2")
                         .arg(loc.toString(QDateTime::fromMSecsSinceEpoch(r.firstMs), QLocale::ShortFormat),
                              loc.toString(QDateTime::fromMSecsSinceEpoch(r.lastMs).time(), QLocale::ShortFormat));
        lines << tr("Записей: %1, под метками: %2").arg(r.records).arg(r.stamped);
        lines << tr("Время подтверждено: %1 (%2 %), расхождение до %3 с")
                     .arg(r.confirmed)
                     .arg(percent(r.confirmed))
                     .arg(loc.toString(r.driftMs / 1000.0, 'f', 1));
        if (r.voided)
            lines << tr("Изменено после записи: участков — %1").arg(r.voided);
        if (r.bad)
            lines << tr("Не сходятся с записью или подписью: меток — %1").arg(r.bad);
    }
    lines << tr("Искусственных нажатий (от программ): %1").arg(r.injected);
    if (!m_stamps->lastError().isEmpty())
        lines << QString() << tr("Последняя ошибка: %1").arg(m_stamps->lastError());
    QMessageBox box(QMessageBox::Information, appTitle(), lines.join(u'\n'), QMessageBox::Ok, this);
    box.setInformativeText(
        tr("Метки ставят службы времени DigiCert, Sectigo и GlobalSign: они подтверждают, что записи были набраны в "
           "это время и потом не менялись. Наружу уходят только хэши, не нажатия. Включается в настройках."));
    box.exec();
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

void MainWindow::setTitle(const QString &document)
{
    m_titleDocument = document;
    updateTitle();
}

void MainWindow::updateTitle()
{
    // Qt has no application window of its own: the task bar shows the title of this one.
    QString title = (!m_capture || m_capture->isChecked() ? QStringLiteral("Ts: ON - ") : QStringLiteral("Ts: OFF - "))
                    + appTitle();
    if (!m_titleDocument.isEmpty())
        title += QStringLiteral(" - ") + m_titleDocument;
    setWindowTitle(title);
}

void MainWindow::hookFailed(const QString &reason, const QString &command)
{
    // Nothing can be recorded: "Ts: OFF", and the reason once (the hook may start by itself later).
    m_hookError = reason;
    m_hookCommand = command;
    m_capture->setChecked(false);
    showHookError();
}

void MainWindow::hookStarted()
{
    if (m_hookError.isEmpty())
        return;
    m_hookError.clear();
    m_capture->setChecked(true);
    if (m_hookErrorBox)
        m_hookErrorBox->close();
}

void MainWindow::showHookError()
{
    if (m_hookErrorBox) {
        m_hookErrorBox->raise();
        return;
    }
    if (m_hookCommand.isEmpty()) {
        auto *box = new QMessageBox(QMessageBox::Warning, appTitle(), m_hookError, QMessageBox::Ok, this);
        box->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_hookErrorBox = box;
    } else {
        // The command in one line and a button that copies it; the window stays until the hook starts.
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(appTitle());
        auto *icon = new QLabel(dialog);
        const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, dialog);
        icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation, nullptr, dialog).pixmap(iconSize));
        auto *text = new QLabel(m_hookError, dialog);
        text->setWordWrap(true);
        auto *command = new QLineEdit(m_hookCommand, dialog);
        command->setReadOnly(true);
        command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        command->setCursorPosition(0);
        command->setMinimumWidth(480);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        auto *copy = buttons->addButton(tr("Скопировать команду"), QDialogButtonBox::ActionRole);
        copy->setDefault(true);
        connect(copy, &QPushButton::clicked, dialog, [this, copy] {
            QGuiApplication::clipboard()->setText(m_hookCommand);
            copy->setText(tr("Скопировано"));
        });
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        auto *grid = new QGridLayout(dialog);
        grid->addWidget(icon, 0, 0, Qt::AlignTop);
        grid->addWidget(text, 0, 1);
        grid->addWidget(command, 1, 1);
        grid->addWidget(buttons, 2, 0, 1, 2);
        grid->setHorizontalSpacing(12);
        m_hookErrorBox = dialog;
    }
    m_hookErrorBox->setAttribute(Qt::WA_DeleteOnClose);
    m_hookErrorBox->open();
}

void MainWindow::captureToggled(bool on)
{
    if (on && !m_hookError.isEmpty()) {
        // Switched on while the hook does not work: it stays off.
        const QSignalBlocker b(m_capture);
        m_capture->setChecked(false);
        showHookError();
        return;
    }
    m_tray->setToolTip(on ? QStringLiteral("Ts: ON") : QStringLiteral("Ts: OFF"));
    updateTitle();
    const QIcon icon = QApplication::windowIcon();
    m_tray->setIcon(on ? icon : QIcon(icon.pixmap(32, 32, QIcon::Disabled)));
    // Switched off: the keys still held get their releases.
    if (!on && !m_opening && Recorder::appendReleases(m_doc.records) > 0) {
        m_unsaved = true;
        recalculate();
    }
    if (!m_opening)
        m_stamps->captureChanged(on);
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
    // The original's menu also had "Справка", its help site (fil.urikor.net), which is long gone: only "About" is left.
    AboutDialog(this).exec();
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

void MainWindow::offerFileAssociation()
{
    using FileAssociation::State;
    const State state = FileAssociation::tsfState();
    if (state == State::Unsupported || state == State::Ours)
        return;
    if (state == State::Moved) { // it was this program: the new place, without asking
        FileAssociation::associateTsf();
        return;
    }
    QSettings settings;
    if (settings.value(QStringLiteral("TsfAssociationAsked"), false).toBool())
        return;
    settings.setValue(QStringLiteral("TsfAssociationAsked"), true);
    QString text = tr("Открывать записи набора (файлы .tsf) в этой программе двойным щелчком?");
    if (state != State::None)
        text += QStringLiteral("\n\n") + tr("Сейчас они открываются в другой программе.");
    if (QMessageBox::question(this, appTitle(), text) != QMessageBox::Yes)
        return;
    if (!FileAssociation::associateTsf()) {
        QMessageBox::warning(this, appTitle(), tr("Не удалось назначить программу для файлов .tsf."));
        return;
    }
    if (FileAssociation::tsfState() == State::Overridden)
        QMessageBox::information(
            this, appTitle(),
            tr("Система запомнила для файлов .tsf другую программу, и сменить её может только пользователь: щёлкните "
               "по файлу .tsf правой кнопкой, «Открыть с помощью» → «Выбрать другое приложение», выберите Typing "
               "statistics и отметьте «Всегда использовать это приложение»."));
}

void MainWindow::normalizeRecords()
{
    // Editing works on the records the text was built from (the original normalizes them in place).
    m_doc.records = m_model.records;
    keepRoomForRecording();
    m_stamps->attach(&m_doc); // the same normalized records, the chain built anew
}

void MainWindow::beginEdit()
{
    normalizeRecords();
    for (int i = 0; i < m_doc.records.size(); ++i)
        m_doc.records[i].tag = quint32(i + 1);
    m_undo = m_doc.records;
    m_undoStamps = m_doc.stamps;
}

void MainWindow::endEdit()
{
    // The stamps follow their records; those whose records changed are voided (re/stamps.md).
    Stamps::follow(m_doc.stamps, m_doc.records);
    m_stamps->attach(&m_doc);
    m_unsaved = true;
    recalculate();
    updateProof();
}

void MainWindow::keepRoomForRecording()
{
    // Recording goes on into this vector, on the hook's time: room for a while, so that the next key
    // does not reallocate the whole recording (milliseconds at half a million records). The room
    // survives the copies of editing: a copy of a vector with reserved room keeps it.
    m_doc.records.reserve(m_doc.records.size() + std::max<qsizetype>(m_doc.records.size() / 2, 4096));
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
    beginEdit();
    Editing::deleteRange(m_doc.records, from, to);
    endEdit();
}

void MainWindow::removeNonText()
{
    const auto [b, e] = Stats::range(m_model, m_text->selectionStart(), m_text->selectionLength(), m_byPauses->isChecked());
    beginEdit();
    Editing::removeNonText(m_doc.records, m_model.recordOfElement(b), m_model.recordOfElement(e));
    endEdit();
}

void MainWindow::convertLayout()
{
    // ConvCurLayout 0x429e80: the selection as if it was typed in the layout active now.
    const auto [from, to] = Editing::recordRange(m_model, m_text->selectionStart(), m_text->selectionLength());
    if (to == 0)
        return;
    beginEdit(); // the original's record range keeps the copy for "Отменить"
    Editing::convertLayout(m_doc.records, from, to, m_toUnicode, KeyboardHook::capsLock());
    KeyboardHook::clearDeadKey();
    endEdit();
}

void MainWindow::undo()
{
    // One level: the records before the last deletion and the current ones change places.
    if (m_undo.isEmpty())
        return;
    normalizeRecords();
    m_doc.records.swap(m_undo);
    m_doc.stamps.swap(m_undoStamps);
    m_unsaved = true;
    keepRoomForRecording();
    m_stamps->attach(&m_doc);
    recalculate();
    updateProof();
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
    if (ok) {
        m_doc.records[record].comment = text;
        m_unsaved = true;
    }
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
    m_unsaved = true;
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
    m_unsaved = true;
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
                hint = formatFixed(double(kExtMilli * gap), 3, QLocale()) + QLatin1Char(' ') + Texts::units().ms;
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
    RecorderSettings s;
    s.capture = m_capture->isChecked();
    s.globalOnOff = m_globalOnOff;
    s.globalClear = m_globalClear;
    s.autoComments = m_autoComments;
    s.splitMs = m_pause->value();
    s.byPauses = m_byPauses->isChecked();
    s.liveVisible = m_live->isVisible();
    Recorder::Context c;
    // Typing into the program's own windows is not recorded - except the text input window, which is
    // there to be typed into (its Esc and F2 are commands, though).
    // The window is the one of the moment of the key when the hook tells it (Windows: the hook thread does
    // not wait for us, and the user may have switched windows since).
    const quint32 vk = (e.flags >> 16) & 0xFF;
    bool own = false, input = false;
    if (e.window != 0) {
        own = e.ownWindow;
        input = m_input->internalWinId() != 0 && quint64(m_input->internalWinId()) == e.window;
    } else {
        const QWidget *active = QApplication::activeWindow();
        own = active != nullptr;
        input = active == m_input;
    }
    c.ownWindow = own && (!input || vk == 0x1B || vk == 0x71);
    const quint64 window = e.window;
    c.foregroundWindow = [window] { return window != 0 ? window : KeyboardHook::foregroundWindow(); };
    c.comment = [window] {
        // Date and time as the system writes them, then the title of the window typed into.
        const QLocale system = QLocale::system();
        const QDateTime now = QDateTime::currentDateTime();
        return system.toString(now.date(), QLocale::ShortFormat) + QLatin1Char(' ')
               + now.time().toString(QStringLiteral("H:mm:ss")) + QLatin1Char(' ')
               + (window != 0 ? KeyboardHook::windowTitle(window) : KeyboardHook::foregroundTitle());
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
        m_unsaved = true;
        m_stamps->recordsAdded();
        if (m_journalOn && !m_journal.append(m_doc.records.last()) && !m_journalFailed) {
            m_journalFailed = true; // said once
            auto *box = new QMessageBox(QMessageBox::Warning, appTitle(),
                                        tr("Не удалось записать журнал %1").arg(m_journal.path()), QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open(); // not exec(): this is the hook
        }
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
    // ShowOpStat: shows the window (the Ctrl+Alt+O hotkey toggles it).
    m_live->show();
    m_live->raise();
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
    if (m_themeButton && o == m_themeButton->parentWidget() && e->type() == QEvent::Resize) {
        m_themeButton->move(m_themeButton->parentWidget()->width() - m_themeButton->width() - 6, 4);
        m_proofButton->move(m_themeButton->x() - m_proofButton->width() - 4, 4);
    }
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
    // The numbers are the shown strings read back, so the table has exactly the shown values. Written and
    // read in the C locale: the digits are the same in any locale, and the system one asks the OS for its
    // separators on every number (580 -> 190 ms on 500k, Windows).
    const QLocale loc = QLocale::c();
    const QString ms = QStringLiteral(", ") + Texts::units().ms;
    TableExport::Table table;
    table.header = {tr("Клавиша"), tr("Пауза") + ms, tr("Длительность") + ms};
    auto number = [&loc](const QString &text) {
        bool ok = false;
        const double v = loc.toDouble(text, &ok);
        return ok ? QVariant(v) : QVariant();
    };
    const auto all = KeyList::rows(m_model.klav, loc, -std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::infinity(), -1, 3, m_model.keyNames);
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
    connect(m_legend, &FloatingPanel::closed, this, [this] {
        m_legendOpen = false;
        updatePanelButtons();
    });
    connect(m_axisPanel, &FloatingPanel::closed, this, &MainWindow::updatePanelButtons);

    connect(m_graph, &GraphWidget::viewChanged, this, &MainWindow::graphMoved);
    connect(m_graph, &GraphWidget::axisMenuRequested, this, &MainWindow::showAxisMenu);
    connect(m_graph, &GraphWidget::autoLimitsChanged, this, [this] { m_axisPanel->setAutoLimits(m_graph->autoLimits()); });
    connect(m_graph, &GraphWidget::elementClicked, this, &MainWindow::scrollKlavogramToElement);
    connect(m_graph, &GraphWidget::klavogramSpanRequested, this, [this](int element) {
        // The klavogram is zoomed to span from its first element to the one under the mouse.
        const float from = float(kExtMilli * drawTimeOfElement(m_graph->klavogramFrom()));
        const float span = float(kExtMilli * (drawTimeOfElement(element) - 100)) - from;
        m_klav->setZoom(span > 0.01f ? float(m_klav->width()) / span : 300.0f);
        klavogramMoved();
    });
    connect(m_graphScroll, &QScrollBar::valueChanged, this, [this](int value) {
        if (m_graphFolded) {
            m_klav->setScrollMs(float(value)); // ScrollBar1Scroll: milliseconds of the klavogram
            klavogramMoved();
            return;
        }
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
    m_klav->setScrollMs(float(kExtMilli * (drawTimeOfElement(element) - 100)));
    klavogramMoved();
}

void MainWindow::showAxisPanel()
{
    m_axisPanel->show();
    m_axisPanel->raise();
    m_axisPanel->keepInside();
    updatePanelButtons();
}

void MainWindow::showLegend()
{
    m_legendOpen = true;
    if (!m_graphFolded) { // the folded graph shows it once it is unfolded
        m_legend->show();
        m_legend->raise();
        m_legend->keepInside();
    }
    updatePanelButtons();
}

void MainWindow::updatePanelButtons()
{
    m_axisButton->setEnabled(!m_axisPanel->isVisibleTo(this));
    m_legendButton->setEnabled(!m_legendOpen);
}

void MainWindow::showAxisMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    if (!m_axisPanel->isVisibleTo(this))
        menu.addAction(tr("Настройка оси Y"), this, &MainWindow::showAxisPanel);
    if (!m_legendOpen)
        menu.addAction(tr("Показать легенду"), this, &MainWindow::showLegend);
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

void MainWindow::graphPaneResized()
{
    // Panel1CanResize.
    const QList<int> sizes = m_leftSplit->sizes();
    const int pane = sizes.value(1), bar = m_graphScroll->sizeHint().height();
    if (pane < 100) {
        if (!m_graphFolded) {
            m_graphFolded = true;
            m_graph->hide();
            m_legend->hide();
            m_axisPanel->hide();
            updatePanelButtons();
        }
        if (pane != bar) // only the scroll bar is left; the klavogram takes the rest
            m_leftSplit->setSizes({sizes.value(0), bar, sizes.value(2) + pane - bar});
    } else if (m_graphFolded) {
        m_graphFolded = false;
        m_graph->show();
        m_legend->setVisible(m_legendOpen);
    }
    klavogramMoved();
}

void MainWindow::syncGraphScrollBar()
{
    if (m_graphFolded) {
        // FUN_0040687c for the folded graph: the scroll bar spans the klavogram in milliseconds.
        const float zoom = m_klav->zoom();
        const float page = float(m_klav->width()) / zoom;
        const float left = float(-m_klav->width()) / (zoom * 4.0f);
        const qint64 last = m_model.klav.isEmpty() ? 0 : m_model.klav.last().tDraw;
        const float end = std::max(0.0f, float(kExtMilli * last) + left);
        const QSignalBlocker blocker(m_graphScroll);
        m_graphScroll->setRange(int(3.0f * left), std::max(int(3.0f * left), int(end + page) - int(page)));
        m_graphScroll->setSingleStep(std::max(1, int(0.05f * page)));
        m_graphScroll->setPageStep(int(page));
        m_graphScroll->setValue(int(m_klav->scrollMs()));
        return;
    }
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
        m_klav->setScrollMs(float(kExtMilli * drawTimeOfElement(m_graph->klavogramFrom())));
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
    opt.keyNames = m_doc.platform;
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
    QVector<QStringList> rows;
    for (int i = 0; i < names.size(); ++i)
        if (m_mainOptions.value(i, true))
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
    if (m_histLabelsSerial != m_model.serial) {
        m_histLabels = Histograms::labelsFromRecords(m_model.records, m_model.keyNames);
        m_histLabelsSerial = m_model.serial;
    }
    source.label = m_histLabels;
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
    const FingerZones zones = m_schemes.zones(m_fingers->currentText());
    if (m_extraFingersSerial != m_model.serial || !(m_extraFingersZones == zones)) {
        m_extraFingers = fingerSeries(m_model, zones);
        m_extraFingersSerial = m_model.serial;
        m_extraFingersZones = zones;
    }
    m_extra->setSource(&m_model, m_extraFingers, b, e);
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
    for (const KeyListRow &row : KeyList::rows(m_model.klav, QLocale(), from, to, limit, m_keyDigits, m_model.keyNames))
        rows.append({row.pause, row.duration, row.key});
    setRows(m_keys, rows);
}

void MainWindow::setDocument(const TsfDocument &doc, const QString &title, bool damaged)
{
    m_doc = doc;
    m_unsaved = false;
    m_undoStamps.clear();
    keepRoomForRecording();
    m_stamps->attach(&m_doc);
    setTitle(title);
    m_damaged->setVisible(damaged);
    m_damaged->raise();
    recalculate();
    updateProof();
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
        doc.platform = currentKeyPlatform(); // a journal is written here
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
    // LoadTsf: an opened file is looked at, not added to - recording is switched off ("Вкл").
    // Opening a journal (0x42abcc) leaves it as it is.
    m_opening = true;
    m_capture->setChecked(false);
    m_opening = false;
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
    const QString path = JournalWriter(AppPaths::dataDir()).path();
    if (QFileInfo::exists(path))
        openFile(path);
    else
        QMessageBox::information(this, appTitle(), tr("Журнала за этот месяц нет: %1").arg(path));
}

void MainWindow::save()
{
    saveDocument(false);
}

bool MainWindow::saveDocument(bool block)
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
        return false;
    QString path = QFileDialog::getSaveFileName(this, {}, m_path, QStringLiteral("Typing statistics files (*.tsf)"));
    if (path.isEmpty())
        return false;
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
        if (to != 0) { // a part: the stamps cover the whole recording
            doc.stamps.clear();
            doc.stampCertificates.clear();
        }
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
        return false;
    }
    if (block)
        return true;
    const KeyRecords records = m_doc.records;
    m_doc = doc;
    m_doc.records = records;
    m_path = path;
    m_loaded = true;
    m_unsaved = false;
    setTitle(QFileInfo(path).fileName());
    return true;
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
    TsfDocument fresh;
    fresh.platform = currentKeyPlatform(); // recorded here
    setDocument(fresh, {}, false);
}
