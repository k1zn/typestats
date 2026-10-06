#include "ExtraStatsWindow.h"

#include "AppPaths.h"
#include "StringTableModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QToolButton *toolButton(QWidget *parent, int n, const QString &hint)
{
    auto *b = new QToolButton(parent);
    b->setIcon(QIcon(QStringLiteral(":/icons/Form3_SpeedButton%1.png").arg(n)));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(hint);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// A report-style list in Arial 15 px with grid lines, rows selected as a whole.
QTableView *reportView(StringTableModel *model)
{
    auto *view = new QTableView;
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(15);
    view->setFont(font);
    view->setModel(model);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view->horizontalHeader()->setHighlightSections(false);
    view->verticalHeader()->hide();
    view->verticalHeader()->setMinimumSectionSize(1);
    view->verticalHeader()->setDefaultSectionSize(QFontMetrics(font).height() + 1);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setWordWrap(false);
    view->setShowGrid(false);
    return view;
}

} // namespace

ExtraStatsWindow::ExtraStatsWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                          | Qt::WindowCloseButtonHint),
      m_templates(AppPaths::file(QStringLiteral("ExStats.ini")))
{
    setWindowTitle(tr("Дополнительная статистика"));
    setMinimumSize(216, 412); // Constraints of the form, less its frame

    m_toolBar = new QFrame;
    m_toolBar->setFrameStyle(int(QFrame::Panel) | int(QFrame::Raised));
    m_toolBar->setFixedHeight(36);
    QToolButton *copyButton = toolButton(m_toolBar, 1, tr("Копировать"));
    copyButton->setGeometry(4, 6, 23, 22);
    QToolButton *excelButton = toolButton(m_toolBar, 7, tr("Экспортировать в Excel"));
    excelButton->setGeometry(26, 6, 23, 22);
    QToolButton *saveButton = toolButton(m_toolBar, 2, tr("Сохранить"));
    saveButton->setGeometry(50, 6, 23, 22);
    m_averages = new QCheckBox(tr("Средние значения"), m_toolBar);
    m_averages->move(80, 1);
    m_lock = new QCheckBox(tr("Заблокировать"), m_toolBar);
    m_lock->move(80, 17);
    m_onTop = toolButton(m_toolBar, 4, tr("Поверх всех окон"));
    m_onTop->setCheckable(true);

    m_kindBox = new QGroupBox(tr("Тип статистики"));
    m_kindBox->setFixedHeight(121);
    const QString kinds[ExtraStats::KindCount] = {tr("Двухсимвольные сочетания"),    tr("Трёхсимвольные сочетания"),
                                                  tr("Четырёхсимвольные сочетания"), tr("Слова"),
                                                  tr("Слова с ошибками"),            tr("Предложения"),
                                                  tr("Шаблон")};
    for (int i = 0; i < ExtraStats::KindCount; ++i) {
        m_kinds[i] = new QRadioButton(kinds[i], m_kindBox);
        m_kinds[i]->move(8, 15 + i * 14);
        connect(m_kinds[i], &QRadioButton::toggled, this, [this](bool on) {
            if (on)
                computeNow(); // even when locked (RadioGroup1Click)
        });
    }
    m_kinds[ExtraStats::Words]->setChecked(true);

    // The template and its button, next to the last radio button.
    m_templatePanel = new QWidget(m_kindBox);
    m_template = new QComboBox(m_templatePanel);
    m_template->setEditable(true);
    m_template->setInsertPolicy(QComboBox::NoInsert);
    m_template->setCompleter(nullptr);
    QFont editFont(QStringLiteral("Arial"));
    editFont.setPixelSize(12);
    m_template->setFont(editFont);
    m_template->addItems(m_templates.items());
    m_template->setCurrentIndex(-1);
    m_template->clearEditText();
    m_templateButton = toolButton(m_templatePanel, 8, tr("Записать шаблон (Правой кнопкой - удалить)"));
    m_templateButton->installEventFilter(this);

    m_filterBox = new QGroupBox(tr("Фильтр по символам"));
    m_filterBox->setFixedHeight(91);
    const QString filters[3] = {tr("Только эти"), tr("Один из"), tr("Исключить")};
    for (int i = 0; i < 3; ++i) {
        m_filterOn[i] = new QCheckBox(filters[i], m_filterBox);
        m_filterOn[i]->move(8, 18 + i * 24);
        m_filterText[i] = new QLineEdit(m_filterBox);
        m_filterText[i]->setFont(editFont);
        connect(m_filterOn[i], &QCheckBox::toggled, this, &ExtraStatsWindow::compute);
        connect(m_filterText[i], &QLineEdit::textChanged, this, &ExtraStatsWindow::compute);
    }

    m_listModel = new StringTableModel(this);
    m_list = reportView(m_listModel);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->horizontalHeader()->setSectionsClickable(true);
    m_lowerModel = new StringTableModel(this);
    m_lowerList = reportView(m_lowerModel);
    m_lowerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_lowerList->horizontalHeader()->setSectionsClickable(false);
    m_lowerList->setMinimumHeight(100);
    m_list->setMinimumHeight(100);
    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);
    m_split->addWidget(m_list);
    m_split->addWidget(m_lowerList);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 0);
    m_lowerList->hide();

    m_status = new QLabel;
    m_status->setFrameStyle(int(QFrame::Panel) | int(QFrame::Sunken));
    m_status->setFixedHeight(19);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toolBar);
    layout->addWidget(m_kindBox);
    layout->addWidget(m_filterBox);
    layout->addWidget(m_split, 1);
    layout->addWidget(m_status);

    connect(copyButton, &QToolButton::clicked, this, &ExtraStatsWindow::copy);
    connect(excelButton, &QToolButton::clicked, this, &ExtraStatsWindow::exportRequested);
    connect(saveButton, &QToolButton::clicked, this, &ExtraStatsWindow::save);
    connect(m_onTop, &QToolButton::toggled, this, [this](bool on) {
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        show(); // changing the flags hides the window
    });
    connect(m_averages, &QCheckBox::toggled, this, &ExtraStatsWindow::averagesToggled);
    connect(m_lock, &QCheckBox::toggled, this, [this](bool on) {
        if (!on)
            computeNow();
    });
    connect(m_template, &QComboBox::editTextChanged, this, &ExtraStatsWindow::compute);
    connect(m_templateButton, &QToolButton::clicked, this, [this] {
        const QString pattern = m_template->currentText();
        if (m_templates.add(pattern))
            m_template->addItem(pattern);
    });
    connect(m_list->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int column) {
        m_sort.clickColumn(column, m_averages->isChecked());
        showRows();
    });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) { rowSelected(current.row()); });
    connect(m_lowerList->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) { occurrenceSelected(current.row()); });

    resize(300, 596);
    showRows();
}

void ExtraStatsWindow::setSource(const TextModel *model, const QVector<quint8> &fingers, int b, int e)
{
    m_model = model;
    m_fingers = fingers;
    m_b = b;
    m_e = e;
    compute();
}

bool ExtraStatsWindow::averages() const
{
    return m_averages->isChecked();
}

ExtraStats::Kind ExtraStatsWindow::kind() const
{
    for (int i = 0; i < ExtraStats::KindCount; ++i)
        if (m_kinds[i]->isChecked())
            return ExtraStats::Kind(i);
    return ExtraStats::Words;
}

ExtraStats::CharFilter ExtraStatsWindow::filter() const
{
    ExtraStats::CharFilter f;
    f.onlyOn = m_filterOn[0]->isChecked();
    f.anyOn = m_filterOn[1]->isChecked();
    f.excludeOn = m_filterOn[2]->isChecked();
    f.only = m_filterText[0]->text();
    f.any = m_filterText[1]->text();
    f.exclude = m_filterText[2]->text();
    return f;
}

void ExtraStatsWindow::compute()
{
    if (m_lock->isChecked())
        m_dirty = true;
    else
        computeNow();
}

void ExtraStatsWindow::computeNow()
{
    if (!isVisible())
        return; // computed when shown
    m_dirty = false;
    const Collected source{m_model, m_model ? m_model->serial : 0, m_fingers, m_b, m_e, kind(), m_template->currentText(), filter()};
    if (source != m_collected) {
        const std::optional<Collected> previous = m_collected;
        if (m_stash && m_stash->key == source) {
            // Back to the result before this one: the two change places.
            if (previous)
                m_stash->key = *previous;
            std::swap(m_occVersion, m_stash->occVersion);
            m_occ.swap(m_stash->occ);
            m_rows.swap(m_stash->rows);
            std::swap(m_sorted, m_stash->sorted);
            m_collected = source;
            if (!previous || previous->serial != source.serial)
                m_stash.reset();
        } else {
            // Kept only for the same model: the one of an older recalculation is of no use.
            if (previous && previous->serial == source.serial)
                m_stash = Stash{*previous, m_occVersion, std::move(m_occ), std::move(m_rows), m_sorted};
            else
                m_stash.reset();
            m_occ.clear();
            if (m_model && m_model->size() > 0)
                m_occ = ExtraStats::collect(*m_model, m_fingers, m_b, m_e, source.kind, source.pattern, source.filter);
            m_collected = source;
            m_occVersion = ++m_versions;
            m_rows.clear();
            m_sorted = {};
        }
    }
    showRows();
}

void ExtraStatsWindow::showRows()
{
    const bool averages = m_averages->isChecked();
    const Sorted sorted{m_occVersion, averages, m_sort.mode, m_sort.descending};
    if (sorted != m_sorted) {
        m_rows = ExtraStats::rows(m_occ, averages, m_sort.mode, m_sort.descending);
        m_sorted = sorted;
    }
    {
        const QSignalBlocker blocker(m_list->selectionModel());
        m_listModel->setTable(m_sort.headers(averages, {tr("Скорость"), tr("Текст"), tr("Кол-во")}), int(m_rows.size()),
                              [this, point = QLocale().decimalPoint()](int row, int column) {
                                  const ExtraStats::Row &r = m_rows[row];
                                  return column == 0 ? ExtraStats::formatSpeed(r.speed, point)
                                         : column == 1 ? r.text
                                                       : QString::number(r.value);
                              });
    }
    layoutControls();
    m_status->setText(tr("Всего:") + QLatin1Char(' ') + QString::number(m_rows.size()));
    m_lower.clear();
    m_lowerModel->setTable({tr("Скорость"), tr("Текст")}, {});
    emit rowsChanged();
    // The first row gets the focus, and with it the klavogram goes to its occurrence.
    if (!m_rows.isEmpty())
        selectRow(0);
}

void ExtraStatsWindow::selectRow(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    {
        const QSignalBlocker blocker(m_list->selectionModel());
        m_list->selectRow(row);
    }
    rowSelected(row);
}

void ExtraStatsWindow::rowSelected(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (!m_averages->isChecked()) {
        emit elementSelected(m_rows[row].value);
        return;
    }
    // All the occurrences of the text, slowest first.
    const QString point = QLocale().decimalPoint();
    m_lower = ExtraStats::occurrences(m_occ, m_rows[row].text);
    QVector<QStringList> table;
    for (const ExtraStats::Occurrence &o : m_lower)
        table << QStringList{ExtraStats::formatSpeed(o.speed, point), o.text};
    {
        const QSignalBlocker blocker(m_lowerList->selectionModel());
        m_lowerModel->setTable({tr("Скорость"), tr("Текст")}, table);
        m_lowerList->selectRow(0);
    }
    layoutControls();
    occurrenceSelected(0);
}

void ExtraStatsWindow::occurrenceSelected(int row)
{
    if (row >= 0 && row < m_lower.size())
        emit elementSelected(m_lower[row].pos);
}

void ExtraStatsWindow::averagesToggled(bool on)
{
    m_lowerList->setVisible(on);
    m_sort.setAverages(on);
    computeNow();
    if (!isVisible())
        showRows(); // the columns change anyway
}

void ExtraStatsWindow::copy()
{
    // The texts of the selected rows, separated by spaces.
    QStringList texts;
    const QModelIndexList selected = m_list->selectionModel()->selectedRows();
    QVector<int> rows;
    for (const QModelIndex &index : selected)
        rows << index.row();
    std::sort(rows.begin(), rows.end());
    for (int row : rows)
        texts << m_rows[row].text;
    QApplication::clipboard()->setText(texts.join(QLatin1Char(' ')));
}

void ExtraStatsWindow::save()
{
    QString path = QFileDialog::getSaveFileName(this, {}, {}, QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, windowTitle(), tr("Не удалось сохранить файл %1").arg(path));
        return;
    }
    // UTF-8 with a signature (the original writes ANSI, which loses characters outside the code page).
    f.write("\xEF\xBB\xBF");
    f.write(ExtraStats::toText(m_rows, m_averages->isChecked(), QLocale(), {tr("Текст"), tr("Скорость"), tr("Кол-во")})
                .toUtf8());
}

bool ExtraStatsWindow::eventFilter(QObject *o, QEvent *e)
{
    // The right button on the template button removes the template.
    if (o == m_templateButton && e->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(e)->button() == Qt::RightButton) {
        const QString pattern = m_template->currentText();
        if (m_templates.remove(pattern)) {
            const QSignalBlocker blocker(m_template);
            m_template->removeItem(m_template->findText(pattern, Qt::MatchExactly | Qt::MatchCaseSensitive));
            m_template->setEditText(pattern);
        }
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void ExtraStatsWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_placed = true;
    layoutControls();
    emit shown();
}

void ExtraStatsWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutControls();
}

void ExtraStatsWindow::layoutControls()
{
    // FormResize: what is anchored to the right edge.
    const int w = width();
    m_onTop->setGeometry(w - 23, 6, 23, 22);
    m_templatePanel->setGeometry(108, 88, w - 108, 25);
    m_template->setGeometry(1, 1, w - 108 - 25, 23);
    m_templateButton->setGeometry(w - 108 - 24, 1, 23, 23);
    for (int i = 0; i < 3; ++i)
        m_filterText[i]->setGeometry(88, 16 + i * 24, w - 88 - 3, 23);
    // Speed 92, count 68, the text takes the rest.
    for (QTableView *view : {m_list, m_lowerList}) {
        QHeaderView *header = view->horizontalHeader();
        if (header->count() < 2)
            continue;
        header->resizeSection(0, 92);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        if (header->count() > 2)
            header->resizeSection(2, 68);
    }
}

void ExtraStatsWindow::loadSettings()
{
    const QSettings s;
    if (!s.contains(QStringLiteral("StatWinLeft")))
        return;
    move(s.value(QStringLiteral("StatWinLeft")).toInt(), s.value(QStringLiteral("StatWinTop")).toInt());
    resize(s.value(QStringLiteral("StatWinWidth"), 300).toInt(), s.value(QStringLiteral("StatWinHeight"), 596).toInt());
}

void ExtraStatsWindow::saveSettings() const
{
    if (!m_placed)
        return; // never shown: there is no position to keep
    QSettings s;
    s.setValue(QStringLiteral("StatWinLeft"), x());
    s.setValue(QStringLiteral("StatWinTop"), y());
    s.setValue(QStringLiteral("StatWinWidth"), width());
    s.setValue(QStringLiteral("StatWinHeight"), height());
}
