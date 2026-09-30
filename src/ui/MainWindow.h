#pragma once

#include "core/FingerZones.h"
#include "core/Journal.h"
#include "core/Recalc.h"
#include "core/TsfFile.h"
#include "platform/KeyboardHook.h"

#include <QElapsedTimer>
#include <QWidget>

class AxisPanel;
class GraphWidget;
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
class QTableWidget;
class QToolButton;
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

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    friend class TstUi;

    QWidget *createToolBar();
    QToolButton *toolButton(QWidget *panel, int n, int x, int y, int h, const QString &hint);
    void loadSettings();
    void saveSettings() const;

    RecalcOptions options() const;
    void recalculate();
    void updateStats();
    void updateKeyList();
    void klavogramMoved();
    void graphMoved();
    void syncGraphScrollBar();
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
    int m_labelRecord = -1; // the record whose label is under the mouse
    QString m_path; // empty: not saved yet
    TextModel m_model;
    FingerZoneSchemes m_schemes;

    TextView *m_text = nullptr;
    GraphWidget *m_graph = nullptr;
    LegendPanel *m_legend = nullptr;
    AxisPanel *m_axisPanel = nullptr;
    bool m_legendPlaced = false;
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

    KeyboardHook m_hook;
    Recorder m_recorder;
    JournalWriter m_journal;
    LiveStatsWindow *m_live = nullptr;
    bool m_needRecalc = false;   // keys were recorded since the text was built
    QElapsedTimer m_lastKey;
    bool m_livePending = false;  // the running statistics changed since they were shown
    int m_liveTicks = 0;
};
