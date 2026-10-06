#pragma once

#include "core/FingerZones.h"
#include "core/Histograms.h"
#include "core/Journal.h"
#include "core/Editing.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"
#include "export/TableExport.h"
#include "platform/KeyboardHook.h"

#include <QElapsedTimer>
#include <QPointer>
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
class QMessageBox;
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
    // At the first start: offers to open .tsf files in the program by a double click (asked once). An association
    // of this program whose exe has moved is brought up to date silently.
    void offerFileAssociation();
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
    friend class TstPerf;

    QWidget *createToolBar();
    // The theme button in the right corner of the toolbar: the dark theme of the remake on and off.
    void setDarkTheme(bool on);
    // What keeps colours of its own: the "damaged" bar, the legend, live statistics, the styles of the text.
    void updateThemeColors();
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
    void hookFailed(const QString &reason, const QString &command);
    void hookStarted();
    void showHookError();
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
    // N7Click / N8Click: the panels are only opened here (their red buttons close them); while one
    // is open its button is off and its menu item is not shown.
    void showAxisPanel();
    void showLegend();
    void updatePanelButtons();
    void showAxisMenu(const QPoint &globalPos);
    void setDocument(const TsfDocument &doc, const QString &title, bool damaged);
    // Room in the records for the keys to come, whenever the records are replaced.
    void keepRoomForRecording();
    // "Ts: ON - Typing statistics v… - file": the capture state goes first, as the original's
    // application title (Application->Title) shows it on the task bar.
    void setTitle(const QString &document);
    void updateTitle();

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
    bool saveDocument(bool block); // false: not saved (cancelled or failed)
    bool askToSave(); // on exit: false - stay

    void open();
    void openJournal();
    void save();
    void clear();

    TsfDocument m_doc;
    bool m_clean = true; // the recording is genuine: it is signed when saved (g_fileClean)
    bool m_loaded = false; // the recording came from a .tsf: its author and date are kept (g_fileLoaded)
    bool m_unsaved = false; // the records changed since they were opened, saved or cleared
    bool m_opening = false; // a .tsf is being opened: switching recording off adds no releases
    KeyRecords m_undo;     // the records before the last deletion
    Editing::ToUnicode m_toUnicode = &KeyboardHook::toUnicode; // the current layout (tests put their own)
    int m_labelRecord = -1; // the record whose label is under the mouse
    QString m_path; // empty: not saved yet
    QString m_titleDocument; // what the title names after the program: the file or the journal
    int m_keyDigits = 3;    // decimals of the times in the key list (DlitDigits)
    // Settings needed on every key and every update of the statistics; read by applySettings().
    bool m_globalOnOff = true, m_globalClear = true, m_autoComments = false, m_journalOn = false;
    QVector<bool> m_mainOptions; // MainOption0..16: rows of the statistics list
    TextModel m_model;
    Histograms::KeyLabel m_histLabels; // labelsFromRecords() of the model with this serial
    quint64 m_histLabelsSerial = 0;
    QVector<quint8> m_extraFingers; // fingerSeries() of the model with this serial and these zones
    quint64 m_extraFingersSerial = 0;
    FingerZones m_extraFingersZones;
    FingerZoneSchemes m_schemes;

    TextView *m_text = nullptr;
    GraphWidget *m_graph = nullptr;
    LegendPanel *m_legend = nullptr;
    AxisPanel *m_axisPanel = nullptr;
    bool m_legendPlaced = false;
    bool m_legendOpen = true; // the legend is not closed; the folded graph hides it all the same
    QToolButton *m_axisButton = nullptr;
    QToolButton *m_legendButton = nullptr;
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
    QToolButton *m_themeButton = nullptr;

    KeyboardHook m_hook;
    QString m_hookError;                     // why the hook does not run; empty when it does (or was not started)
    QString m_hookCommand;                   // the terminal command that fixes it, if any
    QPointer<QDialog> m_hookErrorBox;
    Recorder m_recorder;
    JournalWriter m_journal;
    bool m_journalFailed = false; // the journal could not be written: said once
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
