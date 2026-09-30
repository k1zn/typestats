#pragma once

#include "core/FingerZones.h"
#include "core/Journal.h"
#include "core/Editing.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"
#include "export/TableExport.h"
#include "platform/KeyboardHook.h"

#include <QElapsedTimer>
#include <QWidget>

class AxisPanel;
class ExtraStatsWindow;
class GraphWidget;
class HistogramWindow;
class KlavogramWidget;
class LegendPanel;
class LiveStatsWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QScrollBar;
class QSpinBox;
class QSplitter;
class QSystemTrayIcon;
class QTableWidget;
class QToolButton;
class TextInputWindow;
class TextView;

// The main window (Form1); geometry from re/forms_geometry.txt.
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a .tsf file or a .tsj journal.
    bool openFile(const QString &path);
    // Installs the keyboard hook: from now on what is typed elsewhere is recorded.
    void startCapture();
    // Opens a form by name (`--show NAME`, for screenshots): settings, extra, hist, hist-fingers, hist-extra, kbd, about, input.
    void showForm(const QString &name);

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    friend class TstUi;

    QWidget *createToolBar();
    QToolButton *toolButton(QWidget *panel, int n, int x, int y, int h, const QString &hint);
    void loadSettings();
    void saveSettings() const;
    void showSettings();
    void applySettings();

    RecalcOptions options() const;
    void recalculate();
    void updateStats();
    void updateKeyList();
    void updateExtraStats();
    void showExtraStats();
    void updateHistograms();
    void showHistograms();
    void captureToggled(bool on);
    void showTextInput();
    void showHelpMenu();
    void selectPreset(const QString &name);
    void createPreset();
    void deletePreset();
    void restoreFromTray();
    // "Экспортировать в Excel": the key list of the whole recording / the list of the extra statistics.
    TableExport::Table keyTable() const;
    TableExport::Table extraTable() const;
    void exportTable(const TableExport::Table &table, bool chart);
    void zonesChanged();
    void editFingerZones();
    void createFingerZones();
    void deleteFingerZones();
    void scrollKlavogramToElement(int element);
    void klavogramMoved();
    void graphMoved();
    void syncGraphScrollBar();
    // Panel1 lower than 100 px: the graph folds away and its scroll bar moves the klavogram.
    void graphPaneResized();
    qint64 drawTimeOfElement(int element) const;
    void createGraphPanels();
    void toggleAxisPanel();
    void toggleLegend();
    void showAxisMenu(const QPoint &globalPos);
    void setDocument(const TsfDocument &doc, const QString &title, bool damaged);

    // Recording.
    void keyEvent(const HookEvent &e);
    void tick();
    void showLiveStats();

    // Editing and copying, see re/editing.md.
    void normalizeRecords();
    void selectionChanged();
    void deleteSelection();
    void removeNonText();
    void convertLayout();
    void undo();
    void mark();
    void editLabel(int record);
    void askLabel(int record);
    void removeLabel(int record);
    void copy(int kind);
    void textHovered(int textPos, const QPoint &globalPos);
    void showTextMenu(const QPoint &globalPos);
    void saveDocument(bool block);

    void open();
    void openJournal();
    void save();
    void clear();

    TsfDocument m_doc;
    bool m_clean = true; // the recording is genuine: it is signed when saved (g_fileClean)
    bool m_loaded = false; // the recording came from a .tsf: its author and date are kept (g_fileLoaded)
    KeyRecords m_undo;     // the records before the last deletion
    Editing::ToUnicode m_toUnicode = &KeyboardHook::toUnicode; // the current layout (tests put their own)
    int m_labelRecord = -1; // the record whose label is under the mouse
    QString m_path; // empty: not saved yet
    int m_keyDigits = 3;    // decimals of the times in the key list (DlitDigits)
    TextModel m_model;
    FingerZoneSchemes m_schemes;

    TextView *m_text = nullptr;
    GraphWidget *m_graph = nullptr;
    LegendPanel *m_legend = nullptr;
    AxisPanel *m_axisPanel = nullptr;
    bool m_legendPlaced = false;
    bool m_graphFolded = false;
    QWidget *m_graphPane = nullptr;
    QScrollBar *m_graphScroll = nullptr;
    KlavogramWidget *m_klav = nullptr;
    QTableWidget *m_stats = nullptr;
    QTableWidget *m_keys = nullptr;
    QLabel *m_damaged = nullptr;
    QSplitter *m_leftSplit = nullptr;
    QSplitter *m_mainSplit = nullptr;

    QCheckBox *m_capture = nullptr;
    QCheckBox *m_onlyText = nullptr;
    QCheckBox *m_byPauses = nullptr;
    QSpinBox *m_pause = nullptr;
    QComboBox *m_presets = nullptr;
    QComboBox *m_fingers = nullptr;
    QToolButton *m_saveButton = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QToolButton *m_blockButton = nullptr;
    QToolButton *m_newZonesButton = nullptr;
    QToolButton *m_newPresetButton = nullptr;
    QToolButton *m_helpButton = nullptr;

    KeyboardHook m_hook;
    Recorder m_recorder;
    JournalWriter m_journal;
    LiveStatsWindow *m_live = nullptr;
    ExtraStatsWindow *m_extra = nullptr;
    HistogramWindow *m_hist = nullptr;
    TextInputWindow *m_input = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_needRecalc = false;   // keys were recorded since the text was built
    QElapsedTimer m_lastKey;
    bool m_livePending = false;  // the running statistics changed since they were shown
    int m_liveTicks = 0;
};
