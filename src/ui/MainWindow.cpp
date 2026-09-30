#include "MainWindow.h"

#include "GraphPanels.h"
#include "GraphWidget.h"
#include "KlavogramWidget.h"
#include "TextView.h"
#include "Texts.h"
#include "core/Journal.h"
#include "core/KeyList.h"
#include "core/MainStats.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QTableWidget>
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
    list->horizontalHeader()->setFont(QApplication::font());
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

QFrame *bevel(QWidget *parent, int x, int y, int w, int h, QFrame::Shape shape)
{
    auto *f = new QFrame(parent);
    f->setFrameStyle(int(shape) | int(QFrame::Sunken));
    f->setGeometry(x, y, w, h);
    return f;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_schemes(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("FingerZones.ini")))
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
    m_stats->setFixedHeight(318);
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

    connect(m_text, &QTextEdit::selectionChanged, this, [this] {
        m_klav->scrollToPosition(m_text->selectionStart());
        updateStats();
        klavogramMoved();
    });
    connect(m_klav, &KlavogramWidget::viewChanged, this, &MainWindow::klavogramMoved);
    createGraphPanels();

    resize(876, 579);
    loadSettings();
    recalculate();
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

    auto action = [&](int n, int x, int y, int h, const QString &hint, void (MainWindow::*slot)()) {
        QToolButton *b = toolButton(bar, n, x, y, h, hint);
        if (slot) {
            b->setEnabled(true);
            connect(b, &QToolButton::clicked, this, slot);
        }
        return b;
    };
    // Upper row.
    action(1, 6, 4, 23, tr("Очистить (LCtrl+LWin)"), &MainWindow::clear);
    action(6, 30, 4, 23, tr("Прочитать"), &MainWindow::open);
    m_saveButton = action(5, 54, 4, 23, tr("Сохранить"), &MainWindow::save);
    action(27, 78, 4, 23, tr("Сохранить блок"), nullptr);
    action(26, 102, 4, 23, tr("Открыть журнал"), &MainWindow::openJournal);
    action(7, 126, 4, 23, tr("Экспортировать в Excel"), nullptr);
    action(12, 150, 4, 23, tr("Дополнительная статистика"), nullptr);
    action(19, 174, 4, 23, tr("Статистические гистограммы"), nullptr);
    action(20, 198, 4, 23, tr("Видео"), nullptr);
    action(22, 222, 4, 23, tr("Настройки..."), nullptr);
    action(4, 246, 4, 23, tr("Оперативная статистика"), nullptr);
    action(3, 270, 4, 23, tr("Справка"), nullptr);
    bevel(bar, 3, 29, 294, 2, QFrame::HLine);
    // Lower row.
    action(10, 6, 32, 22, tr("Удалить (Del)"), nullptr);
    action(11, 30, 32, 22, tr("Копировать (Ctrl+C)"), nullptr);
    action(13, 54, 32, 22, tr("Копировать без ошибок"), nullptr);
    action(14, 78, 32, 22, tr("Отменить (Ctrl+Z)"), nullptr);
    action(18, 102, 32, 22, tr("Удалить нетекстовые клавиши"), nullptr);
    action(15, 126, 32, 22, tr("Пометить (Ins)"), nullptr);
    action(21, 150, 32, 22, tr("Свойства видео"), nullptr);
    action(16, 174, 32, 22, tr("Настройка оси Y графиков"), &MainWindow::toggleAxisPanel);
    action(17, 198, 32, 22, tr("Легенда"), &MainWindow::toggleLegend);
    action(23, 222, 32, 22, tr("Ввод текста (F4)"), nullptr);
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
    connect(m_fingers, &QComboBox::currentTextChanged, this,
            [this](const QString &name) { m_klav->setZones(m_schemes.zones(name)); });
    toolButton(bar, 8, 744, 3, 23, tr("Создать пресет (Правой кнопкой - удалить)"));
    toolButton(bar, 9, 744, 27, 23, tr("Создать расстановку (Правой кнопкой - удалить)"));
    toolButton(bar, 2, 768, 27, 23, tr("Редактировать расстановку"));

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
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

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

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSettings();
    e->accept();
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
    return QWidget::eventFilter(o, e);
}

void MainWindow::createGraphPanels()
{
    m_legend = new LegendPanel(m_graph, this);
    m_axisPanel = new AxisPanel(this);
    m_axisPanel->move(121, 121);

    connect(m_graph, &GraphWidget::viewChanged, this, &MainWindow::graphMoved);
    connect(m_graph, &GraphWidget::axisMenuRequested, this, &MainWindow::showAxisMenu);
    connect(m_graph, &GraphWidget::autoLimitsChanged, this, [this] { m_axisPanel->setAutoLimits(m_graph->autoLimits()); });
    connect(m_graph, &GraphWidget::elementClicked, this, [this](int element) {
        // The klavogram starts at the clicked element.
        m_klav->setScrollMs(float(0.001L * (drawTimeOfElement(element) - 100)));
        klavogramMoved();
    });
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
}

void MainWindow::updateKeyList()
{
    if (!m_keys)
        return;
    const auto [from, to] = m_klav->visibleSpanUs();
    // As many rows as fit in the list.
    const int limit = std::max(1, m_keys->viewport()->height() / m_keys->verticalHeader()->defaultSectionSize());
    QVector<QStringList> rows;
    for (const KeyListRow &row : KeyList::rows(m_model.klav, QLocale(), from, to, limit))
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
        if (!Journal::read(path, doc.records)) {
            QMessageBox::warning(this, appTitle(), tr("Не удалось открыть файл %1").arg(path));
            return false;
        }
        m_path.clear(); // a journal is saved as a new .tsf
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
    QString path = QFileDialog::getSaveFileName(this, {}, m_path, QStringLiteral("Typing statistics files (*.tsf)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".tsf");
    // Only a recording that came with a valid signature keeps one; nothing here edits the records yet.
    if (!Tsf::write(path, m_doc, m_doc.signed_ && m_doc.signatureValid)) {
        QMessageBox::warning(this, appTitle(), tr("Не удалось сохранить файл %1").arg(path));
        return;
    }
    m_path = path;
    setWindowTitle(appTitle() + QStringLiteral(" - ") + QFileInfo(path).fileName());
}

void MainWindow::clear()
{
    m_path.clear();
    setDocument({}, {}, false);
}
